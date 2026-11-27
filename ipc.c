/* See LICENSE file for copyright and license details.
 *
 * IPC: a Unix socket where each line is a command (or several separated by
 * ';') and each reply is one line of JSON. "subscribe <events>" turns the
 * connection into an event stream, one JSON object per line. The socket is
 * $XDG_RUNTIME_DIR/dwm-<display>.sock (else /tmp/dwm-<uid>-<display>.sock),
 * published on the root window as _DWM_IPC_SOCKET and in $DWM_SOCKET for
 * the programs dwm starts. dwm-msg(1) is a client; socat/nc work too.
 *
 * Included by dwm.c: it uses dwm's state and functions directly.
 */
#include <sys/socket.h>
#include <sys/un.h>

#define IPCMAXCLIENTS  64
#define IPCBUFSIZE     4096

enum { EvFocus = 1, EvTag = 2, EvTask = 4, EvWindow = 8, EvTree = 16,
       EvMode = 32 };

typedef struct {
	int fd;
	unsigned int events;  /* subscribed events, 0 = commands */
	size_t len;
	char buf[IPCBUFSIZE];
} IpcClient;

typedef struct {
	char *s;
	size_t len, cap;
} Str;

static int ipcfd = -1;
static char ipcpath[sizeof(((struct sockaddr_un *)0)->sun_path)];
static IpcClient ipcclients[IPCMAXCLIENTS];
static int nipcclients;
/* last state sent as events (ipcstatechanged()) */
static Window ipclastsel;
static int ipclasttask = -1, ipclastmode = -1;
static unsigned int ipclasttags;

static void
sputn(Str *b, const char *s, size_t n)
{
	if (b->len + n + 1 > b->cap) {
		b->cap = MAX(b->cap * 2, b->len + n + 256);
		b->s = erealloc(b->s, b->cap);
	}
	memcpy(b->s + b->len, s, n);
	b->len += n;
	b->s[b->len] = '\0';
}

static void
sputs(Str *b, const char *s)
{
	sputn(b, s, strlen(s));
}

static void
sprintfs(Str *b, const char *fmt, ...)
{
	char tmp[512];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(tmp, sizeof tmp, fmt, ap);
	va_end(ap);
	if (n > 0)
		sputn(b, tmp, MIN((size_t)n, sizeof tmp - 1));
}

/* A JSON string literal. */
static void
sjson(Str *b, const char *s)
{
	char esc[8];

	sputs(b, "\"");
	for (; s && *s; s++) {
		unsigned char ch = (unsigned char)*s;

		if (ch == '"' || ch == '\\') {
			esc[0] = '\\';
			esc[1] = ch;
			sputn(b, esc, 2);
		} else if (ch < 0x20) {
			snprintf(esc, sizeof esc, "\\u%04x", ch);
			sputs(b, esc);
		} else
			sputn(b, (const char *)&ch, 1);
	}
	sputs(b, "\"");
}

static const char *
layoutname(int l)
{
	static const char *names[] = { "splith", "splitv", "stacked", "tabbed" };

	return l >= 0 && l < (int)LENGTH(names) ? names[l] : "splith";
}

static int
clienttag(Client *c)
{
	return (c->tags & TAGMASK) == TAGMASK ? 0 : ffs((int)c->tags);
}

static void
jsonclient(Str *b, Client *c)
{
	sprintfs(b, "{\"id\":%lu,\"title\":", (unsigned long)c->win);
	sjson(b, c->name);
	sputs(b, ",\"class\":");
	sjson(b, c->class);
	sputs(b, ",\"instance\":");
	sjson(b, c->instance);
	sputs(b, ",\"task\":");
	sjson(b, c->task >= 0 && c->task < ntasks ? tasknames[c->task] : "");
	sprintfs(b, ",\"tasknum\":%d,\"tag\":%d,\"monitor\":%d,\"floating\":%s,"
		"\"fullscreen\":%s,\"minimized\":%s,\"urgent\":%s,\"focused\":%s}",
		c->task + 1, clienttag(c), c->mon->num,
		c->isfloating && !c->isfullscreen ? "true" : "false",
		c->isfullscreen ? "true" : "false", c->minimized ? "true" : "false",
		c->isurgent ? "true" : "false", c == selmon->sel ? "true" : "false");
}

static void
jsonnode(Str *b, Node *n)
{
	Node *ch;
	int first = 1;

	if (n->leaf) {
		if (n->c)
			jsonclient(b, n->c);
		else
			sprintfs(b, "{\"id\":%lu}", (unsigned long)n->win);
		return;
	}
	sprintfs(b, "{\"layout\":\"%s\",\"fact\":%.3f,\"nodes\":[", layoutname(n->layout), n->fact);
	for (ch = n->first; ch; ch = ch->next) {
		if (!first)
			sputs(b, ",");
		first = 0;
		jsonnode(b, ch);
	}
	sputs(b, "]}");
}

static void
ipcget(Str *b, const char *what)
{
	Monitor *m;
	Client *c;
	unsigned int i, occ, urg;
	int t, n, first = 1;

	if (!strcmp(what, "tasks")) {
		sputs(b, "[");
		for (t = 0; t < ntasks; t++) {
			for (n = 0, urg = 0, m = mons; m; m = m->next)
				for (c = m->clients; c; c = c->next)
					if (c->task == t) {
						n++;
						urg |= c->isurgent;
					}
			sprintfs(b, "%s{\"num\":%d,\"name\":", t ? "," : "", t + 1);
			sjson(b, tasknames[t]);
			sprintfs(b, ",\"focused\":%s,\"urgent\":%s,\"windows\":%d}",
				t == seltask ? "true" : "false", urg ? "true" : "false", n);
		}
		sputs(b, "]");
	} else if (!strcmp(what, "tags")) {
		occ = urg = 0;
		for (c = selmon->clients; c; c = c->next)
			if (c->task == seltask) {
				if ((c->tags & TAGMASK) != TAGMASK)
					occ |= c->tags;
				if (c->isurgent)
					urg |= c->tags;
			}
		sputs(b, "[");
		for (i = 0; i < NTAGS; i++) {
			sprintfs(b, "%s{\"num\":%u,\"name\":", i ? "," : "", i + 1);
			sjson(b, tags[i]);
			sprintfs(b, ",\"focused\":%s,\"occupied\":%s,\"urgent\":%s}",
				selmon->tagset[selmon->seltags] & (1u << i) ? "true" : "false",
				occ & (1u << i) ? "true" : "false", urg & (1u << i) ? "true" : "false");
		}
		sputs(b, "]");
	} else if (!strcmp(what, "windows")) {
		sputs(b, "[");
		for (m = mons; m; m = m->next)
			for (c = m->clients; c; c = c->next) {
				if (!first)
					sputs(b, ",");
				first = 0;
				jsonclient(b, c);
			}
		sputs(b, "]");
	} else if (!strcmp(what, "tree")) {
		/* the workspace shown on each monitor */
		sputs(b, "[");
		for (m = mons; m; m = m->next) {
			sprintfs(b, "%s{\"monitor\":%d,\"task\":", m == mons ? "" : ",", m->num);
			sjson(b, tasknames[seltask]);
			sprintfs(b, ",\"tag\":%u,\"tree\":", curtag(m) + 1);
			jsonnode(b, TREE(m));
			sputs(b, "}");
		}
		sputs(b, "]");
	} else if (!strcmp(what, "focused")) {
		if (selmon->sel)
			jsonclient(b, selmon->sel);
		else
			sputs(b, "null");
	} else
		sputs(b, "null");
}

static int
dirarg(const char *s)
{
	if (!strcmp(s, "left"))  return DirLeft;
	if (!strcmp(s, "right")) return DirRight;
	if (!strcmp(s, "up"))    return DirUp;
	if (!strcmp(s, "down"))  return DirDown;
	return -1;
}

/* Task given as its number (1-based) or its label. */
static int
taskarg(const char *s)
{
	char *end;
	long v = strtol(s, &end, 10);
	int t;

	if (*s && !*end)
		return v >= 1 && v <= ntasks ? (int)v - 1 : -1;
	for (t = 0; t < ntasks; t++)
		if (!strcmp(tasknames[t], s))
			return t;
	return -1;
}

static int
tagarg(const char *s)
{
	char *end;
	long v = strtol(s, &end, 10);

	return *s && !*end && v >= 1 && v <= (long)NTAGS ? (int)v - 1 : -1;
}

/* A new task named `name` after the active one, without the bar prompt. */
static int
ipcnewtask(const char *name)
{
	int pos = seltask + 1;

	if (ntasks >= MAXTASKS || !*name || taskarg(name) >= 0)
		return 0;
	taskinsert(pos);
	copytaskname(tasknames[pos], name);
	savetasks();
	updatedesktops();
	switchtask(pos);
	return 1;
}

/* Run one command; NULL on success, else the error. Queries write their
 * result to *data. */
static const char *
ipccommand(char *line, Str *data, unsigned int *subscribe)
{
	char *argv[8], *save, *rest;
	int argc = 0, v;
	Client *c = selmon->sel;
	Arg a = {0};

	while (*line == ' ' || *line == '\t')
		line++;
	/* exec and renametask take the rest of the line as is */
	if (!strncmp(line, "exec ", 5)) {
		for (rest = line + 5; *rest == ' '; rest++);
		if (!*rest)
			return "exec needs a command";
		a.v = (const char *[]){ "/bin/sh", "-c", rest, NULL };
		spawn(&a);
		return NULL;
	}
	for (argv[argc] = strtok_r(line, " \t", &save); argv[argc] && argc < 7;
	     argv[++argc] = strtok_r(NULL, " \t", &save));
	argv[argc] = NULL;
	if (!argc)
		return "empty command";
	/* task names may have spaces: the arguments of these are one */
	if (argc > 2 && (!strcmp(argv[0], "task") || !strcmp(argv[0], "movetotask")
	|| !strcmp(argv[0], "newtask") || !strcmp(argv[0], "renametask"))) {
		for (v = 2; v < argc; v++)
			argv[v][-1] = ' ';
		argc = 2;
	}

#define IS(S) (!strcmp(argv[0], (S)))
#define NEED(N) do { if (argc < (N) + 1) return "missing argument"; } while (0)
	if (IS("get_tasks") || IS("get_tags") || IS("get_windows") || IS("get_tree")
	|| IS("get_focused")) {
		ipcget(data, argv[0] + 4);
	} else if (IS("subscribe")) {
		NEED(1);
		for (v = 1; v < argc; v++) {
			if (!strcmp(argv[v], "focus"))       *subscribe |= EvFocus;
			else if (!strcmp(argv[v], "tag"))    *subscribe |= EvTag;
			else if (!strcmp(argv[v], "task"))   *subscribe |= EvTask;
			else if (!strcmp(argv[v], "window")) *subscribe |= EvWindow;
			else if (!strcmp(argv[v], "tree"))   *subscribe |= EvTree;
			else if (!strcmp(argv[v], "mode"))   *subscribe |= EvMode;
			else if (!strcmp(argv[v], "all"))    *subscribe |= ~0u;
			else return "unknown event";
		}
	} else if (IS("view")) {
		NEED(1);
		if ((v = tagarg(argv[1])) < 0)
			return "no such tag";
		if (selmon->tagset[selmon->seltags] != (1u << v)) {
			a.ui = 1u << v;
			view(&a);
		}
	} else if (IS("tag")) {
		NEED(1);
		if ((v = tagarg(argv[1])) < 0)
			return "no such tag";
		if (!c)
			return "no focused window";
		a.ui = 1u << v;
		tag(&a);
	} else if (IS("task")) {
		NEED(1);
		if ((v = taskarg(argv[1])) < 0)
			return "no such task";
		switchtask(v);
	} else if (IS("movetotask")) {
		NEED(1);
		if ((v = taskarg(argv[1])) < 0)
			return "no such task";
		if (!c)
			return "no focused window";
		a.i = v;
		movetotask(&a);
	} else if (IS("newtask")) {
		NEED(1);
		if (!ipcnewtask(argv[1]))
			return "cannot create the task";
	} else if (IS("renametask")) {
		NEED(1);
		if (taskarg(argv[1]) >= 0 && taskarg(argv[1]) != seltask)
			return "name in use";
		copytaskname(tasknames[seltask], argv[1]);
		savetasks();
		updatedesktops();
		drawbars();
	} else if (IS("deltask")) {
		if (ntasks <= 1)
			return "the last task stays";
		deltask(&a);
	} else if (IS("focus")) {
		NEED(1);
		if ((v = dirarg(argv[1])) >= 0) {
			a.i = v;
			focusdir(&a);
		} else if (!strcmp(argv[1], "urgent"))
			focusurgent(&a);
		else if (!strcmp(argv[1], "mode_toggle"))
			focusmode(&a);
		else if (!strcmp(argv[1], "next") || !strcmp(argv[1], "prev")) {
			a.i = argv[1][0] == 'n' ? 1 : -1;
			focusstack(&a);
		} else if (!strcmp(argv[1], "id")) {
			NEED(2);
			if (!(c = wintoclient((Window)strtoul(argv[2], NULL, 0))) || c->win != (Window)strtoul(argv[2], NULL, 0))
				return "no such window";
			focusclient(c);
		} else
			return "unknown focus target";
	} else if (IS("move")) {
		NEED(1);
		if ((v = dirarg(argv[1])) < 0)
			return "unknown direction";
		a.i = v;
		movedir(&a);
	} else if (IS("split")) {
		NEED(1);
		if (!strcmp(argv[1], "h") || !strcmp(argv[1], "horizontal"))
			a.i = SplitH;
		else if (!strcmp(argv[1], "v") || !strcmp(argv[1], "vertical"))
			a.i = SplitV;
		else
			return "split h or v";
		split(&a);
	} else if (IS("layout")) {
		NEED(1);
		if (!strcmp(argv[1], "toggle")) {
			togglesplit(&a);
			return NULL;
		}
		for (v = 0; v < 4 && strcmp(argv[1], layoutname(v)); v++);
		if (v == 4 && !strcmp(argv[1], "stacking"))
			v = Stacked;
		if (v == 4)
			return "unknown layout";
		a.i = v;
		containerlayout(&a);
	} else if (IS("floating")) {
		if (!c)
			return "no focused window";
		togglefloating(&a);
	} else if (IS("fullscreen")) {
		if (!c)
			return "no focused window";
		togglefullscr(&a);
	} else if (IS("minimize")) {
		if (!c)
			return "no focused window";
		setminimized(c, 1);
	} else if (IS("kill")) {
		if (!c)
			return "no focused window";
		killclient(&a);
	} else if (IS("resize")) {
		NEED(2);
		v = !strcmp(argv[1], "grow") ? 1 : !strcmp(argv[1], "shrink") ? -1 : 0;
		if (!v || (strcmp(argv[2], "width") && strcmp(argv[2], "height")))
			return "resize grow|shrink width|height";
		if (!c || c->isfullscreen)
			return "no focused window";
		if (c->isfloating) {
			if (argv[2][0] == 'w')
				resize(c, c->x, c->y, MAX(1, c->w + 10 * v), c->h, 1);
			else
				resize(c, c->x, c->y, c->w, MAX(1, c->h + 10 * v), 1);
		} else
			resizetiled(c, argv[2][0] == 'w' ? v : 0, argv[2][0] == 'h' ? v : 0);
	} else if (IS("mode")) {
		NEED(1);
		v = !strcmp(argv[1], "resize") ? ModeResize : !strcmp(argv[1], "move") ? ModeMove
			: !strcmp(argv[1], "default") ? ModeNormal : -1;
		if (v < 0)
			return "unknown mode";
		if (v != wmmode) {
			a.i = v == ModeNormal ? wmmode : v;
			setmode(&a);
		}
	} else if (IS("overview")) {
		overview(&a);
	} else if (IS("restart")) {
		restart(&a);
	} else if (IS("exit")) {
		quit(&a);
	} else
		return "unknown command";
#undef IS
#undef NEED
	return NULL;
}

static void
ipcdrop(int i)
{
	close(ipcclients[i].fd);
	ipcclients[i] = ipcclients[--nipcclients];
}

/* Write all of s or drop the client: a reader that does not keep up with
 * the event stream is disconnected rather than blocking the WM. */
static int
ipcsend(int i, const char *s, size_t n)
{
	ssize_t w;

	while (n > 0) {
		w = send(ipcclients[i].fd, s, n, MSG_NOSIGNAL);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0) {
			ipcdrop(i);
			return 0;
		}
		s += w;
		n -= w;
	}
	return 1;
}

static void
ipcevent(unsigned int ev, const char *name, Str *payload)
{
	Str b = {0};
	int i;

	if (!nipcclients)
		return;
	sprintfs(&b, "{\"event\":\"%s\"", name);
	if (payload && payload->len) {
		sputs(&b, ",");
		sputn(&b, payload->s, payload->len);
	}
	sputs(&b, "}\n");
	for (i = nipcclients - 1; i >= 0; i--)
		if (ipcclients[i].events & ev)
			ipcsend(i, b.s, b.len);
	free(b.s);
}

/* New or closed window, title change: "change" is new, close or title. */
static void
ipcwindowevent(Client *c, const char *change)
{
	Str p = {0};

	if (!nipcclients || !stateready || !c->mon || !c->frame)
		return;
	sputs(&p, "\"change\":");
	sjson(&p, change);
	sputs(&p, ",\"window\":");
	jsonclient(&p, c);
	ipcevent(EvWindow, "window", &p);
	free(p.s);
}

/* Called from savestate(): focus, task, tag and mode changes as events. */
static void
ipcstatechanged(int treechanged)
{
	Str p = {0};
	Window sel = selmon->sel ? selmon->sel->win : 0;
	unsigned int tagset = selmon->tagset[selmon->seltags];

	if (!nipcclients) {
		ipclastsel = sel;
		ipclasttask = seltask;
		ipclasttags = tagset;
		ipclastmode = wmmode;
		return;
	}
	if (seltask != ipclasttask) {
		sputs(&p, "\"task\":");
		ipcget(&p, "tasks");
		ipcevent(EvTask, "task", &p);
		p.len = 0;
	}
	if (seltask != ipclasttask || tagset != ipclasttags) {
		sputs(&p, "\"tags\":");
		ipcget(&p, "tags");
		ipcevent(EvTag, "tag", &p);
		p.len = 0;
	}
	if (sel != ipclastsel) {
		sputs(&p, "\"window\":");
		ipcget(&p, "focused");
		ipcevent(EvFocus, "focus", &p);
		p.len = 0;
	}
	if (wmmode != ipclastmode) {
		sprintfs(&p, "\"mode\":\"%s\"", wmmode == ModeResize ? "resize"
			: wmmode == ModeMove ? "move" : "default");
		ipcevent(EvMode, "mode", &p);
		p.len = 0;
	}
	if (treechanged) {
		sputs(&p, "\"tree\":");
		ipcget(&p, "tree");
		ipcevent(EvTree, "tree", &p);
	}
	free(p.s);
	ipclastsel = sel;
	ipclasttask = seltask;
	ipclasttags = tagset;
	ipclastmode = wmmode;
}

/* Handle the complete lines a client sent. */
static void
ipcread(int i)
{
	IpcClient *cl = &ipcclients[i];
	Str reply = {0}, data = {0};
	char *line, *nl, *cmd, *save;
	const char *err;
	ssize_t r;
	unsigned int sub;
	int ok, fd;

	r = recv(cl->fd, cl->buf + cl->len, sizeof cl->buf - 1 - cl->len, 0);
	if (r <= 0) {
		if (r < 0 && (errno == EAGAIN || errno == EINTR))
			return;
		ipcdrop(i);
		return;
	}
	cl->len += r;
	cl->buf[cl->len] = '\0';
	fd = cl->fd;
	while ((nl = strchr(cl->buf, '\n')) || cl->len >= sizeof cl->buf - 1) {
		if (!nl) /* an overlong line: drop it */
			nl = cl->buf + cl->len - 1;
		*nl = '\0';
		line = ecalloc(1, strlen(cl->buf) + 1);
		strcpy(line, cl->buf);
		memmove(cl->buf, nl + 1, cl->len - (nl + 1 - cl->buf) + 1);
		cl->len -= nl + 1 - cl->buf;
		if (line[0] && line[strlen(line) - 1] == '\r')
			line[strlen(line) - 1] = '\0';
		/* several commands: "a; b"; exec takes the rest of the line */
		ok = 1;
		err = NULL;
		sub = 0;
		data.len = 0;
		if (data.s)
			data.s[0] = '\0';
		for (cmd = line; cmd && *cmd && !err; cmd = save) {
			save = strncmp(cmd + strspn(cmd, " \t"), "exec ", 5) ? strchr(cmd, ';') : NULL;
			if (save)
				*save++ = '\0';
			if (!*(cmd + strspn(cmd, " \t")))
				continue;
			err = ipccommand(cmd, &data, &sub);
		}
		free(line);
		ok = !err;
		reply.len = 0;
		if (data.len)
			sputn(&reply, data.s, data.len);
		else {
			sprintfs(&reply, "{\"success\":%s", ok ? "true" : "false");
			if (err) {
				sputs(&reply, ",\"error\":");
				sjson(&reply, err);
			}
			sputs(&reply, "}");
		}
		sputs(&reply, "\n");
		/* the client may have been dropped or moved by a command that
		 * sent events: find it again */
		for (i = 0; i < nipcclients && ipcclients[i].fd != fd; i++);
		if (i == nipcclients || !ipcsend(i, reply.s, reply.len))
			break;
		cl = &ipcclients[i];
		cl->events |= sub;
		XFlush(dpy);
	}
	free(reply.s);
	free(data.s);
}

static void
ipcaccept(void)
{
	int fd;

	while ((fd = accept(ipcfd, NULL, NULL)) >= 0) {
		if (nipcclients >= IPCMAXCLIENTS) {
			close(fd);
			continue;
		}
		fcntl(fd, F_SETFD, FD_CLOEXEC);
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
		memset(&ipcclients[nipcclients], 0, sizeof ipcclients[nipcclients]);
		ipcclients[nipcclients++].fd = fd;
	}
}

/* Add the IPC sockets to a select() set; returns the highest fd. */
static int
ipcfds(fd_set *fds, int maxfd)
{
	int i;

	if (ipcfd < 0)
		return maxfd;
	FD_SET(ipcfd, fds);
	maxfd = MAX(maxfd, ipcfd);
	for (i = 0; i < nipcclients; i++) {
		FD_SET(ipcclients[i].fd, fds);
		maxfd = MAX(maxfd, ipcclients[i].fd);
	}
	return maxfd;
}

static void
ipchandle(fd_set *fds)
{
	int i, fd;

	if (ipcfd < 0)
		return;
	if (FD_ISSET(ipcfd, fds))
		ipcaccept();
	for (i = nipcclients - 1; i >= 0; i--) {
		fd = ipcclients[i].fd;
		if (FD_ISSET(fd, fds)) {
			ipcread(i);
			XFlush(dpy);
		}
		if (i > nipcclients)
			i = nipcclients;
	}
}

static void
ipcsetup(void)
{
	struct sockaddr_un sa;
	const char *rt = getenv("XDG_RUNTIME_DIR"), *disp = DisplayString(dpy), *d;
	char name[64];
	size_t k = 0;

	/* "host:1.0" -> "host_1.0" */
	for (d = disp; d && *d && k + 1 < sizeof name; d++)
		if (*d != ':' || d != disp)
			name[k++] = (*d == '/' || *d == ':') ? '_' : *d;
	name[k] = '\0';
	if (rt && *rt)
		snprintf(ipcpath, sizeof ipcpath, "%s/dwm-%s.sock", rt, name);
	else
		snprintf(ipcpath, sizeof ipcpath, "/tmp/dwm-%ld-%s.sock", (long)getuid(), name);
	if ((ipcfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "dwm: ipc socket: %s\n", strerror(errno));
		return;
	}
	fcntl(ipcfd, F_SETFD, FD_CLOEXEC);
	fcntl(ipcfd, F_SETFL, fcntl(ipcfd, F_GETFL) | O_NONBLOCK);
	memset(&sa, 0, sizeof sa);
	sa.sun_family = AF_UNIX;
	snprintf(sa.sun_path, sizeof sa.sun_path, "%s", ipcpath);
	unlink(ipcpath); /* left by a crashed dwm, or the one before a restart */
	if (bind(ipcfd, (struct sockaddr *)&sa, sizeof sa) < 0 || listen(ipcfd, 16) < 0) {
		fprintf(stderr, "dwm: ipc %s: %s\n", ipcpath, strerror(errno));
		close(ipcfd);
		ipcfd = -1;
		return;
	}
	chmod(ipcpath, 0600);
	setenv("DWM_SOCKET", ipcpath, 1);
	XChangeProperty(dpy, root, XInternAtom(dpy, "_DWM_IPC_SOCKET", False),
		XInternAtom(dpy, "UTF8_STRING", False), 8, PropModeReplace,
		(unsigned char *)ipcpath, strlen(ipcpath));
}

static void
ipccleanup(void)
{
	while (nipcclients)
		ipcdrop(nipcclients - 1);
	if (ipcfd >= 0) {
		close(ipcfd);
		unlink(ipcpath);
		ipcfd = -1;
	}
}
