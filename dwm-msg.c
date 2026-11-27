/* See LICENSE file for copyright and license details.
 *
 * dwm-msg: send a command to dwm's IPC socket and print the reply (one line
 * of JSON). "subscribe <events>" keeps printing the events. The socket is
 * -s, else $DWM_SOCKET, else the _DWM_IPC_SOCKET property on the root
 * window of $DISPLAY.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

static void
usage(void)
{
	fputs("usage: dwm-msg [-s socket] command [args...]\n"
	      "  e.g. dwm-msg get_tree, dwm-msg focus left, dwm-msg subscribe focus tag\n",
	      stderr);
	exit(2);
}

/* _DWM_IPC_SOCKET on the root window, or NULL. */
static char *
rootsocket(void)
{
	Display *dpy;
	Atom type;
	int format;
	unsigned long n, after;
	unsigned char *p = NULL;
	char *path = NULL;

	if (!(dpy = XOpenDisplay(NULL)))
		return NULL;
	if (XGetWindowProperty(dpy, DefaultRootWindow(dpy),
		XInternAtom(dpy, "_DWM_IPC_SOCKET", True), 0, 1024, False, AnyPropertyType,
		&type, &format, &n, &after, &p) == Success && p) {
		if (format == 8 && n > 0 && (path = malloc(n + 1))) {
			memcpy(path, p, n);
			path[n] = '\0';
		}
		XFree(p);
	}
	XCloseDisplay(dpy);
	return path;
}

int
main(int argc, char *argv[])
{
	struct sockaddr_un sa;
	char *path = NULL, *cmd, buf[65536];
	size_t len = 0, cap;
	ssize_t r;
	int fd, i, subscribe, status = 0, gotreply = 0, linestart = 1;
	FILE *in;

	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		if (!strcmp(argv[i], "-s") && i + 1 < argc)
			path = argv[++i];
		else
			usage();
	}
	if (i >= argc)
		usage();
	if (!path && (path = getenv("DWM_SOCKET")) && !*path)
		path = NULL;
	if (!path && !(path = rootsocket())) {
		fputs("dwm-msg: no dwm IPC socket (is dwm running on $DISPLAY?)\n", stderr);
		return 1;
	}

	/* the command: the remaining arguments joined by spaces */
	for (cap = 2, subscribe = i; subscribe < argc; subscribe++)
		cap += strlen(argv[subscribe]) + 1;
	if (!(cmd = malloc(cap)))
		return 1;
	cmd[0] = '\0';
	for (subscribe = i; subscribe < argc; subscribe++) {
		if (subscribe > i)
			strcat(cmd, " ");
		strcat(cmd, argv[subscribe]);
	}
	strcat(cmd, "\n");
	subscribe = !strncmp(cmd, "subscribe", 9);

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		perror("dwm-msg: socket");
		return 1;
	}
	memset(&sa, 0, sizeof sa);
	sa.sun_family = AF_UNIX;
	snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
	if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
		fprintf(stderr, "dwm-msg: %s: %s\n", path, strerror(errno));
		return 1;
	}
	for (len = strlen(cmd), cap = 0; cap < len; cap += r)
		if ((r = write(fd, cmd + cap, len - cap)) <= 0) {
			perror("dwm-msg: write");
			return 1;
		}
	if (!(in = fdopen(fd, "r")))
		return 1;
	/* the reply, then (subscribe) the events, one line each */
	while (fgets(buf, sizeof buf, in)) {
		fputs(buf, stdout);
		fflush(stdout);
		if (!gotreply && linestart && !strncmp(buf, "{\"success\":false", 16))
			status = 1;
		/* a long reply comes in several pieces */
		linestart = strchr(buf, '\n') != NULL;
		if (!gotreply && linestart) {
			gotreply = 1;
			if (!subscribe || status)
				break;
		}
	}
	if (!gotreply) {
		fputs("dwm-msg: no reply\n", stderr);
		status = 1;
	}
	return status;
}
