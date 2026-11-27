/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "icontheme.h"
#include "util.h"

/* macros */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
/* ONVIEW: on the viewed tags of the active task (listed in the taskbar);
 * ISVISIBLE: also not minimized, i.e. shown, tiled and focusable */
#define ONVIEW(C)               ((cleaningup || (C)->task == seltask) \
                               && ((C)->tags & (C)->mon->tagset[(C)->mon->seltags]))
#define ISVISIBLE(C)            (ONVIEW(C) && (cleaningup || !(C)->minimized))
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w)
#define HEIGHT(X)               ((X)->h)
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define NTAGS                   ((unsigned int)LENGTH(tags))
#define PT(T)                   ((unsigned int)seltask * NTAGS + (T)) /* per task x tag index */
#define MAXTASKS                32
#define TASKNAMELEN             64
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)
#define MFACT(M)                ((M)->mfacts[PT(curtag(M))])
#define NMASTER(M)              ((M)->nmasters[PT(curtag(M))])

/* restart state (see savestate()) */
#define STATEMAGIC1             0x64776d31L /* "dwm1": before tasks */
#define STATEMAGIC              0x64776d32L /* "dwm2" */
#define STATEHEAD1              4  /* magic, ntags, nmons, selmon */
#define MONSTATE1               6  /* num, seltags, tagset[2], sel, showbar */
#define STATEHEAD               7  /* magic, ntags, nmons, selmon, ntasks, seltask, prevtask */
#define MONSTATE                2  /* num, showbar */
#define VIEWSTATE               4  /* per task: seltags, tagset[2], sel */
#define TAGSTATE                5  /* sellt, lt[2], mfact, nmaster */
#define CLIENTSTATE1            5  /* tags, mon, floating, cfact, order */
#define CLIENTSTATE2            6  /* ... plus task */
#define CLIENTSTATE             7  /* ... plus minimized */

/* _NET_WM_MOVERESIZE directions */
#define MOVERESIZE_SIZE_TOPLEFT      0
#define MOVERESIZE_SIZE_LEFT         7
#define MOVERESIZE_MOVE              8
#define MOVERESIZE_SIZE_KEYBOARD     9
#define MOVERESIZE_MOVE_KEYBOARD     10

#define SYSTEM_TRAY_REQUEST_DOCK    0
/* XEMBED messages */
#define XEMBED_EMBEDDED_NOTIFY      0
#define XEMBED_WINDOW_ACTIVATE      1
#define XEMBED_FOCUS_IN             4
#define XEMBED_MODALITY_ON         10
#define XEMBED_MAPPED              (1 << 0)
#define XEMBED_WINDOW_ACTIVATE      1
#define XEMBED_WINDOW_DEACTIVATE    2
#define VERSION_MAJOR               0
#define VERSION_MINOR               0
#define XEMBED_EMBEDDED_VERSION (VERSION_MAJOR << 16) | VERSION_MINOR

/* enums */
enum { CurNormal, CurResize, CurMove, CurLast }; /* cursor */
enum { SchemeNorm, SchemeSel, SchemeUrg, SchemeTagEmpty, SchemeTagOcc,
       SchemeSeparator, SchemeAccent, SchemeTask, SchemeTaskSel, SchemeTaskUrg,
       SchemeIcon, SchemeDot }; /* color schemes */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetSystemTray, NetSystemTrayOP, NetSystemTrayOrientation, NetSystemTrayOrientationHorz,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, NetDwmTab, NetWmIcon,
       NetWMDesktop, NetCurrentDesktop, NetNumberOfDesktops, NetDesktopNames,
       NetCloseWindow, NetWMMoveResize, NetFrameExtents, NetRequestFrameExtents,
       NetWMStateDemandsAttention, NetWMWindowTypeUtility, NetWMWindowTypeSplash,
       NetWMWindowTypeToolbar, NetWMWindowTypeNotification, NetWMStateHidden,
       NetLast }; /* EWMH atoms */
enum { DwmClientState, DwmState, MotifWMHints, DwmLast }; /* non-EWMH atoms */
enum { Manager, Xembed, XembedInfo, XLast }; /* Xembed atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMChangeState, WMLast }; /* default atoms */
enum { ClkTaskCell, ClkTagBar, ClkTaskBar, ClkStatusText, ClkClientWin, ClkBorder, ClkTitleBar, ClkTab, ClkRootWin, ClkLast }; /* clicks */
enum { ModeNormal, ModeResize, ModeMove }; /* binding modes */
enum { DirLeft, DirDown, DirUp, DirRight }; /* directions */
enum { RLeft = 1, RRight = 2, RTop = 4, RBottom = 8 }; /* resize anchors */

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int click;
	unsigned int mask;
	unsigned int button;
	void (*func)(const Arg *arg);
	const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
	char name[256];
	char class[128];
	char instance[128];
	float mina, maxa;
	int x, y, w, h;
	int oldx, oldy, oldw, oldh;
	int natx, naty, natw, nath; /* geometry requested when mapped */
	int basew, baseh, incw, inch, maxw, maxh, minw, minh, hintsvalid;
	int bw, oldbw;
	float cfact; /* share of its column/row in the tiling layouts */
	long savedstate[CLIENTSTATE]; /* last _DWM_CLIENT_STATE written */
	int statevalid;
	unsigned int tags;
	int task; /* index in tasknames */
	int minimized; /* hidden from its tags until restored (taskbar) */
	int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen;
	int mapped; /* frame currently mapped by showhide() */
	int notitle; /* _MOTIF_WM_HINTS: no decorations (client-side ones) */
	int th; /* title bar height currently applied (0 = none) */
	int titlemapped;
	long extents[4]; /* last _NET_FRAME_EXTENTS published */
	int argb; /* 32-bit client: the frame uses its visual */
	Colormap cmap; /* frame colormap for argb clients */
	uint32_t *icon; /* 16x16 premultiplied ARGB, NULL = no icon */
	/* taskbar cache: title truncated to taskcap px and the item width
	 * without the focus frame; taskcap < 0 = invalid */
	int taskcap, taskw;
	char tasktitle[320];
	Client *next;
	Client *snext;
	Monitor *mon;
	Window win;
	Window frame;
	Window title;
};

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

/* what a monitor shows inside one task, saved while another task is active */
typedef struct {
	unsigned int tagset[2];
	unsigned int seltags;
	Window sel;
} TaskView;

struct Monitor {
	char ltsymbol[16];
	float *mfacts;         /* per task x tag (PT()) */
	int *nmasters;         /* per task x tag */
	int num;
	int by;               /* bar geometry */
	int mx, my, mw, mh;   /* screen size */
	int wx, wy, ww, wh;   /* window area  */
	unsigned int seltags;
	unsigned int *sellt;   /* active layout slot per task x tag */
	TaskView taskview[MAXTASKS]; /* per task; the active one lives in tagset/seltags/sel */
	unsigned int tagset[2];
	int showbar;
	int topbar;
	Client *clients;
	Client *sel;
	Client *stack;
	Monitor *next;
	Window barwin;
	const Layout **lt;     /* two remembered layouts per task x tag: lt[PT(tag) * 2 + slot] */
	/* bar hit-testing caches, filled by drawbar() */
	int taskcellx[MAXTASKS + 1];
	int tagx[33];
	int statusx;
	int barw;
	int barwx, barwy, barww; /* barwin geometry last set by resizebarwin() */
	int taskn;
	int *taskx;            /* listcap entries */
	Client **taskclients;
	/* tabbed layout state */
	Client *tabsel;
	Client **tabclients;   /* listcap entries */
	int *tabx;
	int *tabw;
	int tabn;
	int listcap;
};

typedef struct {
	const char *class;
	const char *instance;
	const char *title;
	unsigned int tags;
	int isfloating;
	int monitor;
} Rule;

typedef struct {
	Window win;
	Client *icons;
} Systray;

/* function declarations */
static void applyrules(Client *c);
static int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
static void attach(Client *c);
static void attachstack(Client *c);
static void buttonpress(XEvent *e);
static void centerclient(Client *c);
static void checkotherwm(void);
static void cleanup(void);
static void cleanupmon(Monitor *mon);
static void clientmessage(XEvent *e);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon(void);
static const Layout *curlayout(Monitor *m);
static unsigned int curtag(Monitor *m);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static Monitor *dirtomon(int dir);
static void drawbar(Monitor *m);
static void drawbars(void);
static int drawmode(int x);
static int drawseparator(int x);
static int drawtag(Monitor *m, unsigned int i, int x, unsigned int occ, unsigned int urg);
static void drawclienttitles(Client *c);
static void drawtitle(Client *c, int focused);
static void drawtask(Monitor *m, Client *c, int *x, int cap);
static void drawtasks(Monitor *m, int x, int right);
static void expose(XEvent *e);
static void focus(Client *c);
static void focusdir(const Arg *arg);
static void focusin(XEvent *e);
static void focusmode(const Arg *arg);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static void floatnatural(Client *c);
static void focustab(const Arg *arg);
static void focustask(const Arg *arg);
static Atom getatomprop(Client *c, Atom prop);
static int getrootptr(int *x, int *y);
static long getstate(Window w);
static unsigned int getsystraywidth();
static int gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *c, int focused);
static void grabkeys(void);
static void incnmaster(const Arg *arg);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void closeclient(Client *c);
static void manage(Window w, XWindowAttributes *wa, int adopt);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static int modekey(XEvent *e, KeySym keysym);
static void monocle(Monitor *m);
static void motionnotify(XEvent *e);
static void movedir(const Arg *arg);
static void movemouse(const Arg *arg);
static void dragmove(Client *c);
static void dragresize(Client *c, int anchor);
static void swapclients(Monitor *m, Client *a, Client *b);
static Client *nexttiled(Client *c);
static int outlineadd(XRectangle *rects, int n, int max, int x, int y, int w, int h);
static void outlinecreate(void);
static void outlinehide(void);
static int outlinerects(XRectangle *rects, int max, int width, int height,
	int containerw, const XRectangle *tab, int t);
static void outlineshape(int width, int height, int containerw, const XRectangle *tab);
static int outlinetabrect(Client *c, XRectangle *tab);
static void outlineupdate(void);
static void place(Client *c, int x, int y, int w, int h);
static void pop(Client *c);
static void grabmodekeys(int grab);
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
static void restart(const Arg *arg);
static void savestate(void);
static void loadstate(void);
static int readclientstate(Window w, long *st);
static Monitor *recttomon(int x, int y, int w, int h);
static void removesystrayicon(Client *i);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizebarwin(Monitor *m);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void resizerequest(XEvent *e);
static void restack(Monitor *m);
static void run(void);
static void scan(void);
static int sendevent(Window w, Atom proto, int m, long d0, long d1, long d2, long d3, long d4);
static void sendmon(Client *c, Monitor *m);
static void setclientstate(Client *c, long state);
static int taskwidth(Client *c, int cap);
static int textwidth(const char *s);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setminimized(Client *c, int minimized);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setmode(const Arg *arg);
static void setup(void);
static void seturgent(Client *c, int urg);
static void showhide(Client *c);
static void spawn(const Arg *arg);
static Monitor *systraytomon(Monitor *m);
static void tabbed(Monitor *m);
static void tabselect(Monitor *m);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *m);
static void tilev(Monitor *m);
static void togglebar(const Arg *arg);
static void titleclick(const Arg *arg);
static void togglefullscr(const Arg *arg);
static void togglefloating(const Arg *arg);
static void togglesplit(const Arg *arg);
static void freeicon(Client *c);
static void unfocus(Client *c, int setfocus);
static void unmanage(Client *c, int destroyed);
static void truncatechars(const char *src, char *dst, size_t dstsz, int maxchars);
static void truncatepx(const char *src, char *dst, size_t dstsz, int maxpx);
static void unmapnotify(XEvent *e);
static void updatebarpos(Monitor *m);
static void updatebars(void);
static void updateclass(Client *c);
static void updateicon(Client *c);
static void updateclientlist(void);
static int updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void updatesystray(void);
static void updatesystrayicongeom(Client *i, int w, int h);
static void updatesystrayiconstate(Client *i, XPropertyEvent *ev);
static void updatetitle(Client *c);
static void updatewindowtype(Client *c);
static void updatewmhints(Client *c);
static void view(const Arg *arg);
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);
static Client *wintosystrayicon(Window w);
static int xerror(Display *d, XErrorEvent *ee);
static int xerrordummy(Display *d, XErrorEvent *ee);
static int xerrorstart(Display *d, XErrorEvent *ee);
static void zoom(const Arg *arg);
/* tasks: groups of workspaces, each with its own tags 1..10 */
static void cycletask(const Arg *arg);
static void deltask(const Arg *arg);
static int drawtaskcell(Monitor *m, int i, int x, const unsigned int *occ);
static void initpertag(Monitor *m, unsigned int idx);
static void lasttask(const Arg *arg);
static void loadtasks(void);
static void movetotask(const Arg *arg);
static void newtask(const Arg *arg);
static void promptend(int commit);
static void promptkey(XEvent *e);
static void renametask(const Arg *arg);
static void savetasks(void);
static void switchtask(int t);
static void taskinsert(int pos);
static void taskremove(int pos);
static void updatedesktops(void);
static void viewtask(const Arg *arg);

/* variables */
static Systray *systray = NULL;
static const char broken[] = "broken";
static char stext[256];
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static int bh;               /* bar height */
static int lrpad;            /* sum of left and right padding for text */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent *) = {
	[ButtonPress] = buttonpress,
	[ClientMessage] = clientmessage,
	[ConfigureRequest] = configurerequest,
	[ConfigureNotify] = configurenotify,
	[DestroyNotify] = destroynotify,
	[Expose] = expose,
	[FocusIn] = focusin,
	[KeyPress] = keypress,
	[MappingNotify] = mappingnotify,
	[MapRequest] = maprequest,
	[MotionNotify] = motionnotify,
	[PropertyNotify] = propertynotify,
	[ResizeRequest] = resizerequest,
	[UnmapNotify] = unmapnotify
};
static Atom wmatom[WMLast], netatom[NetLast], xatom[XLast], dwmatom[DwmLast];
static int running = 1;
static int restartwm;      /* re-exec after cleanup() */
static int cleaningup;     /* cleanup() runs: keep the saved state */
static int stateready;     /* scan() finished: state may be saved */
static char (*tasknames)[TASKNAMELEN];
static int ntasks = 0, seltask = 0, prevtask = 0;
static int renaming = -1;  /* task whose label is being edited in the bar */
static char promptbuf[TASKNAMELEN];
static XIM xim;
static XIC xic;
static int sigpipe[2] = { -1, -1 };
static volatile sig_atomic_t pendingsig;
static int loadedselmon = -1;
static Window loadedsel[32]; /* focused client per monitor, from _DWM_STATE */
static int wmmode = ModeNormal;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static Monitor *mons, *selmon;
static Window root, wmcheckwin;
/* focus outline: shaped, input-transparent window above the focused frame */
static Window outlinewin = None;
static int outlinemapped;
static int outlinex, outliney, outlinew, outlineh;
static int outlinetabvalid;
static XRectangle outlinetab;

/* configuration, allows nested code to access above variables */
#include "config.h"

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

/* function implementations */

/* Title bar height for c if it is (not) floating: none when fullscreen or
 * when the client draws its own decorations, except as the tiled member of
 * a tabbed layout, whose title bar is the shared tab strip. */
static int
titlehfor(Client *c, int floating)
{
	if (c->isfullscreen)
		return 0;
	if (c->notitle && !(c->mon && curlayout(c->mon)->arrange == tabbed && !floating))
		return 0;
	return titleheight;
}

static int
titleh(Client *c)
{
	return titlehfor(c, c->isfloating);
}

/* Room in m's taskbar/tab caches for every client on m (plus the end
 * marker of taskx). There is no fixed limit on the number of windows. */
static void
ensuremoncap(Monitor *m)
{
	Client *c;
	int n = 1;

	for (c = m->clients; c; c = c->next)
		n++;
	if (n <= m->listcap)
		return;
	n = MAX(n, 2 * m->listcap);
	n = MAX(n, 32);
	m->taskx = erealloc(m->taskx, n * sizeof *m->taskx);
	m->taskclients = erealloc(m->taskclients, n * sizeof *m->taskclients);
	m->tabclients = erealloc(m->tabclients, n * sizeof *m->tabclients);
	m->tabx = erealloc(m->tabx, n * sizeof *m->tabx);
	m->tabw = erealloc(m->tabw, n * sizeof *m->tabw);
	m->listcap = n;
}

/* A scheme pixel for c's frame: opaque alpha on a 32-bit (argb) frame. */
static unsigned long
framepixel(Client *c, unsigned long pixel)
{
	return c->argb ? pixel | 0xff000000UL : pixel;
}

static void
setframeborder(Client *c, int schemeidx)
{
	XSetWindowBorder(dpy, c->frame, framepixel(c, scheme[schemeidx][ColBorder].pixel));
}

static void
updatemotifhints(Client *c)
{
	Atom type;
	int format;
	unsigned long n, after;
	unsigned char *p = NULL;
	long *h;

	c->notitle = 0;
	if (XGetWindowProperty(dpy, c->win, dwmatom[MotifWMHints], 0, 5, False,
		AnyPropertyType, &type, &format, &n, &after, &p) != Success || !p)
		return;
	h = (long *)p;
	/* flags & MWM_HINTS_DECORATIONS, decorations without ALL or TITLE */
	if (format == 32 && n >= 3 && (h[0] & 2) && !(h[2] & (1 | 8)))
		c->notitle = 1;
	XFree(p);
}

/* _NET_WM_STATE as dwm maintains it: fullscreen and demands-attention. */
static void
updatenetstate(Client *c)
{
	Atom st[3];
	int n = 0;

	if (c->minimized)
		st[n++] = netatom[NetWMStateHidden];
	if (c->isfullscreen)
		st[n++] = netatom[NetWMFullscreen];
	if (c->isurgent)
		st[n++] = netatom[NetWMStateDemandsAttention];
	XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
		PropModeReplace, (unsigned char *)st, n);
}

static void
setframeextents(Window w, int bw, int th, long *cache)
{
	long ext[4] = { bw, bw, bw + th, bw };

	if (cache && !memcmp(cache, ext, sizeof ext))
		return;
	if (cache)
		memcpy(cache, ext, sizeof ext);
	XChangeProperty(dpy, w, netatom[NetFrameExtents], XA_CARDINAL, 32,
		PropModeReplace, (unsigned char *)ext, 4);
}

void
applyrules(Client *c)
{
	const char *class, *instance;
	unsigned int i;
	const Rule *r;
	Monitor *m;

	/* rule matching */
	c->isfloating = 0;
	c->tags = 0;
	class    = c->class[0]    ? c->class    : broken;
	instance = c->instance[0] ? c->instance : broken;

	for (i = 0; i < LENGTH(rules); i++) {
		r = &rules[i];
		if ((!r->title || strstr(c->name, r->title))
		&& (!r->class || strstr(class, r->class))
		&& (!r->instance || strstr(instance, r->instance)))
		{
			c->isfloating = r->isfloating;
			c->tags |= r->tags;
			for (m = mons; m && m->num != r->monitor; m = m->next);
			if (m)
				c->mon = m;
		}
	}
	c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];
}

void
updateclass(Client *c)
{
	XClassHint ch = { NULL, NULL };

	if (XGetClassHint(dpy, c->win, &ch)) {
		if (ch.res_class)
			snprintf(c->class, sizeof c->class, "%s", ch.res_class);
		if (ch.res_name)
			snprintf(c->instance, sizeof c->instance, "%s", ch.res_name);
	}
	if (ch.res_class)
		XFree(ch.res_class);
	if (ch.res_name)
		XFree(ch.res_name);
}

int
applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
	int baseismin;
	Monitor *m = c->mon;
	int bw = c->bw;
	int th = titleh(c);
	int cw, ch;

	/* *w and *h are the frame's outer size (border + title + client). */
	cw = MAX(1, *w - 2 * bw);
	ch = MAX(1, *h - 2 * bw - th);

	if (interact) {
		if (*x > sw)
			*x = sw - *w;
		if (*y > sh)
			*y = sh - *h;
		if (*x + *w < 0)
			*x = 0;
		if (*y + *h < 0)
			*y = 0;
	} else {
		if (*x >= m->wx + m->ww)
			*x = m->wx + m->ww - *w;
		if (*y >= m->wy + m->wh)
			*y = m->wy + m->wh - *h;
		if (*x + *w <= m->wx)
			*x = m->wx;
		if (*y + *h <= m->wy)
			*y = m->wy;
	}

	if (resizehints || c->isfloating || !curlayout(m)->arrange) {
		if (!c->hintsvalid)
			updatesizehints(c);
		/* see last two sentences in ICCCM 4.1.2.3 */
		baseismin = c->basew == c->minw && c->baseh == c->minh;
		if (!baseismin) { /* temporarily remove base dimensions */
			cw -= c->basew;
			ch -= c->baseh;
		}
		cw = MAX(1, cw);
		ch = MAX(1, ch);
		/* adjust for aspect limits */
		if (c->mina > 0 && c->maxa > 0) {
			if (c->maxa < (float)cw / ch)
				cw = ch * c->maxa + 0.5;
			else if (c->mina < (float)ch / cw)
				ch = cw * c->mina + 0.5;
		}
		if (baseismin) { /* increment calculation requires this */
			cw -= c->basew;
			ch -= c->baseh;
		}
		cw = MAX(1, cw);
		ch = MAX(1, ch);
		/* adjust for increment value */
		if (c->incw)
			cw -= cw % c->incw;
		if (c->inch)
			ch -= ch % c->inch;
		/* restore base dimensions */
		cw = MAX(cw + c->basew, c->minw);
		ch = MAX(ch + c->baseh, c->minh);
		if (c->maxw)
			cw = MIN(cw, c->maxw);
		if (c->maxh)
			ch = MIN(ch, c->maxh);
	}
	cw = MAX(1, cw);
	ch = MAX(1, ch);
	*w = cw + 2 * bw;
	*h = ch + 2 * bw + th;
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void
arrange(Monitor *m)
{
	if (m) {
		if (curlayout(m)->arrange == tabbed)
			tabselect(m);
		showhide(m->stack);
	} else for (m = mons; m; m = m->next) {
		if (curlayout(m)->arrange == tabbed)
			tabselect(m);
		showhide(m->stack);
	}
	if (m) {
		arrangemon(m);
		restack(m);
	} else for (m = mons; m; m = m->next)
		arrangemon(m);
	outlineupdate();
	savestate();
}

void
arrangemon(Monitor *m)
{
	snprintf(m->ltsymbol, sizeof(m->ltsymbol), "%s", curlayout(m)->symbol);
	if (curlayout(m)->arrange)
		curlayout(m)->arrange(m);
}

void
attach(Client *c)
{
	c->next = c->mon->clients;
	c->mon->clients = c;
}

void
attachstack(Client *c)
{
	c->snext = c->mon->stack;
	c->mon->stack = c;
}

void
buttonpress(XEvent *e)
{
	unsigned int i, click;
	Arg arg = {0};
	Client *c;
	Monitor *m;
	XButtonPressedEvent *ev = &e->xbutton;

	click = ClkRootWin;
	if (renaming >= 0)
		promptend(1);
	/* focus monitor if necessary */
	if ((m = wintomon(ev->window)) && m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
	}
	if (ev->window == selmon->barwin) {
		for (i = 0; i < (unsigned int)ntasks; i++)
			if (ev->x >= selmon->taskcellx[i] && ev->x < selmon->taskcellx[i + 1]) {
				click = ClkTaskCell;
				arg.i = (int)i;
				break;
			}
		for (i = 0; click == ClkRootWin && i < LENGTH(tags); i++)
			if (ev->x >= selmon->tagx[i] && ev->x < selmon->tagx[i + 1]) {
				click = ClkTagBar;
				arg.ui = 1 << i;
				break;
			}
		if (click == ClkRootWin) {
			for (i = 0; i < (unsigned int)selmon->taskn; i++)
				if (ev->x >= selmon->taskx[i] && ev->x < selmon->taskx[i + 1]) {
					click = ClkTaskBar;
					arg.i = i;
					break;
				}
			if (i == (unsigned int)selmon->taskn && selmon->statusx >= 0
			&& ev->x >= selmon->statusx && ev->x < selmon->barw)
				click = ClkStatusText;
		}
	} else if ((c = wintoclient(ev->window))) {
		focus(c);
		restack(selmon);
		if (ev->window == c->title) {
			if (curlayout(c->mon)->arrange == tabbed
			&& c == c->mon->tabsel && !c->isfloating) {
				for (i = 0; i < (unsigned int)c->mon->tabn; i++)
					if (ev->x >= c->mon->tabx[i]
					&& ev->x < c->mon->tabx[i] + c->mon->tabw[i]) {
						click = ClkTab;
						arg.i = (int)i;
						break;
					}
				if (i == (unsigned int)c->mon->tabn)
					click = ClkTitleBar;
			} else
				click = ClkTitleBar;
		} else if (ev->window == c->frame)
			click = ClkBorder;
		else {
			if (ev->window == c->win)
				XAllowEvents(dpy, ReplayPointer, CurrentTime);
			click = ClkClientWin;
		}
	}
	for (i = 0; i < LENGTH(buttons); i++)
		if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
		&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
			buttons[i].func(click == ClkTaskCell || click == ClkTagBar || click == ClkTaskBar || click == ClkTab
				? &arg : &buttons[i].arg);
}

void
checkotherwm(void)
{
	xerrorxlib = XSetErrorHandler(xerrorstart);
	/* this causes an error if some other window manager is running */
	XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
	XSync(dpy, False);
	XSetErrorHandler(xerror);
	XSync(dpy, False);
}

void
cleanup(void)
{
	Arg a = {.ui = ~0};
	Layout foo = { "", NULL };
	Monitor *m;
	size_t i;

	cleaningup = 1;
	view(&a);
	for (m = mons; m; m = m->next)
		for (i = 0; i < MAXTASKS * NTAGS; i++)
			m->lt[i * 2 + m->sellt[i]] = &foo;
	for (m = mons; m; m = m->next)
		while (m->stack)
			unmanage(m->stack, 0);
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	while (mons)
		cleanupmon(mons);

	if (showsystray) {
		XUnmapWindow(dpy, systray->win);
		XDestroyWindow(dpy, systray->win);
		free(systray);
	}

	if (outlinewin) {
		XDestroyWindow(dpy, outlinewin);
		outlinewin = None;
		outlinemapped = 0;
	}

	for (i = 0; i < CurLast; i++)
		drw_cur_free(drw, cursor[i]);
	for (i = 0; i < LENGTH(colors); i++)
		drw_scm_free(drw, scheme[i], 3);
	free(scheme);
	XDestroyWindow(dpy, wmcheckwin);
	if (xic)
		XDestroyIC(xic);
	if (xim)
		XCloseIM(xim);
	free(tasknames);
	drw_free(drw);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

void
cleanupmon(Monitor *mon)
{
	Monitor *m;

	if (mon == mons)
		mons = mons->next;
	else {
		for (m = mons; m && m->next != mon; m = m->next);
		m->next = mon->next;
	}
	XUnmapWindow(dpy, mon->barwin);
	XDestroyWindow(dpy, mon->barwin);
	free(mon->lt);
	free(mon->sellt);
	free(mon->mfacts);
	free(mon->nmasters);
	free(mon->taskx);
	free(mon->taskclients);
	free(mon->tabclients);
	free(mon->tabx);
	free(mon->tabw);
	free(mon);
}

void
clientmessage(XEvent *e)
{
	XWindowAttributes wa;
	XSetWindowAttributes swa;
	XClientMessageEvent *cme = &e->xclient;
	Client *c = wintoclient(cme->window);

	if (showsystray && cme->window == systray->win && cme->message_type == netatom[NetSystemTrayOP]) {
		/* add systray icons */
		if (cme->data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) {
			if (!(c = (Client *)calloc(1, sizeof(Client))))
				die("fatal: could not malloc() %u bytes\n", sizeof(Client));
			if (!(c->win = cme->data.l[2])) {
				free(c);
				return;
			}
			c->mon = selmon;
			c->next = systray->icons;
			systray->icons = c;
			if (!XGetWindowAttributes(dpy, c->win, &wa)) {
				/* use sane defaults */
				wa.width = bh;
				wa.height = bh;
				wa.border_width = 0;
			}
			c->x = c->oldx = c->y = c->oldy = 0;
			c->w = c->oldw = wa.width;
			c->h = c->oldh = wa.height;
			c->oldbw = wa.border_width;
			c->bw = 0;
			c->isfloating = True;
			/* reuse tags field as mapped status */
			c->tags = 1;
			updatesizehints(c);
			updatesystrayicongeom(c, wa.width, wa.height);
			XAddToSaveSet(dpy, c->win);
			XSelectInput(dpy, c->win, StructureNotifyMask | PropertyChangeMask | ResizeRedirectMask);
			XReparentWindow(dpy, c->win, systray->win, 0, 0);
			/* use parents background color */
			swa.background_pixel  = scheme[SchemeNorm][ColBg].pixel;
			XChangeWindowAttributes(dpy, c->win, CWBackPixel, &swa);
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_EMBEDDED_NOTIFY, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
			/* FIXME not sure if I have to send these events, too */
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_FOCUS_IN, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_WINDOW_ACTIVATE, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
			sendevent(c->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_MODALITY_ON, 0 , systray->win, XEMBED_EMBEDDED_VERSION);
			XSync(dpy, False);
			resizebarwin(selmon);
			updatesystray();
			setclientstate(c, NormalState);
		}
		return;
	}

	if (cme->message_type == netatom[NetRequestFrameExtents]) {
		/* asked before the window is mapped: the default frame */
		if (!c)
			setframeextents(cme->window, borderpx, titleheight, NULL);
		return;
	}
	if (cme->message_type == netatom[NetCurrentDesktop]) {
		Arg a;

		/* desktop = task * NTAGS + tag. A pager asking for the current
		 * desktop must not trigger the back-and-forth of view(). */
		if (cme->data.l[0] >= 0 && cme->data.l[0] < (long)(ntasks * NTAGS)) {
			switchtask((int)(cme->data.l[0] / NTAGS));
			if ((1u << (cme->data.l[0] % NTAGS)) != selmon->tagset[selmon->seltags]) {
				a.ui = 1u << (cme->data.l[0] % NTAGS);
				view(&a);
			}
		}
		return;
	}
	if (!c)
		return;
	if (cme->message_type == netatom[NetWMDesktop]) {
		unsigned long d = (unsigned long)cme->data.l[0] & 0xFFFFFFFFUL;
		unsigned int newtags = d == 0xFFFFFFFFUL ? TAGMASK
			: d < ntasks * NTAGS ? 1u << (d % NTAGS) : 0;
		int newtask = d == 0xFFFFFFFFUL || d >= ntasks * NTAGS
			? c->task : (int)(d / NTAGS);

		if (newtags && (newtags != c->tags || newtask != c->task)) {
			c->tags = newtags;
			c->task = newtask;
			focus(NULL);
			arrange(c->mon);
		}
		return;
	}
	if (cme->message_type == netatom[NetCloseWindow]) {
		closeclient(c);
		return;
	}
	if (cme->message_type == netatom[NetWMMoveResize]) {
		long dir = cme->data.l[2];
		Arg a;

		if (c->isfullscreen || !ISVISIBLE(c))
			return;
		if (c != selmon->sel) {
			focus(c);
			restack(c->mon);
		}
		if (dir == MOVERESIZE_MOVE)
			dragmove(c);
		else if (dir >= MOVERESIZE_SIZE_TOPLEFT && dir <= MOVERESIZE_SIZE_LEFT) {
			static const int anchors[] = {
				RTop|RLeft, RTop, RTop|RRight, RRight,
				RBottom|RRight, RBottom, RBottom|RLeft, RLeft
			};
			dragresize(c, anchors[dir]);
		} else if (dir == MOVERESIZE_SIZE_KEYBOARD || dir == MOVERESIZE_MOVE_KEYBOARD) {
			a.i = dir == MOVERESIZE_SIZE_KEYBOARD ? ModeResize : ModeMove;
			if (wmmode != a.i)
				setmode(&a);
		}
		return;
	}
	if (cme->message_type == netatom[NetWMState]) {
		Atom a1 = (Atom)cme->data.l[1], a2 = (Atom)cme->data.l[2];

		if (a1 == netatom[NetWMStateHidden] || a2 == netatom[NetWMStateHidden])
			setminimized(c, cme->data.l[0] == 1
				|| (cme->data.l[0] == 2 && !c->minimized));
		if (a1 == netatom[NetWMFullscreen] || a2 == netatom[NetWMFullscreen])
			setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
				|| (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
		if (a1 == netatom[NetWMStateDemandsAttention]
		|| a2 == netatom[NetWMStateDemandsAttention]) {
			int urg = cme->data.l[0] == 1
				|| (cme->data.l[0] == 2 && !c->isurgent);

			/* the focused window never needs attention */
			if (urg != c->isurgent && !(urg && c == selmon->sel)) {
				seturgent(c, urg);
				drawbars();
			}
		}
	} else if (cme->message_type == netatom[NetActiveWindow]) {
		/* i3's focus_on_window_activation smart: focus a window that is
		 * on a visible tag, mark any other one urgent. */
		if (c == selmon->sel)
			return;
		if (ONVIEW(c) && c->minimized)
			setminimized(c, 0);
		else if (ISVISIBLE(c)) {
			focus(c);
			restack(c->mon);
		} else if (!c->isurgent)
			seturgent(c, 1);
	} else if (cme->message_type == wmatom[WMChangeState]) {
		/* ICCCM iconify request (a client's own minimize button) */
		if (cme->data.l[0] == IconicState)
			setminimized(c, 1);
	}
}

void
configure(Client *c)
{
	XConfigureEvent ce;
	int th = c->th;

	ce.type = ConfigureNotify;
	ce.display = dpy;
	ce.event = c->win;
	ce.window = c->win;
	ce.x = c->x + c->bw;
	ce.y = c->y + c->bw + th;
	ce.width = MAX(1, c->w - 2 * c->bw);
	ce.height = MAX(1, c->h - 2 * c->bw - th);
	ce.border_width = 0;
	ce.above = None;
	ce.override_redirect = False;
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void
configurenotify(XEvent *e)
{
	Monitor *m;
	Client *c;
	XConfigureEvent *ev = &e->xconfigure;
	int dirty;

	/* TODO: updategeom handling sucks, needs to be simplified */
	if (ev->window == root) {
		if (ev->width <= 0 || ev->height <= 0)
			return;

		dirty = (sw != ev->width || sh != ev->height);
		sw = ev->width;
		sh = ev->height;
		if (updategeom() || dirty) {
			drw_resize(drw, sw, bh);
			updatebars();
			for (m = mons; m; m = m->next) {
				for (c = m->clients; c; c = c->next)
					if (c->isfullscreen)
						resizeclient(c, m->mx, m->my, m->mw, m->mh);
				resizebarwin(m);
			}
			focus(NULL);
			arrange(NULL);
		}
	}
}

void
configurerequest(XEvent *e)
{
	Client *c;
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;

	if ((c = wintoclient(ev->window))) {
		if (ev->window != c->win) {
			/* request from our own frame/title; ignore */
		} else if (c->isfloating || !curlayout(c->mon)->arrange) {
			int th = titleh(c);
			int cw = c->w - 2 * c->bw;
			int ch = c->h - 2 * c->bw - c->th;

			if (ev->value_mask & CWWidth)
				cw = ev->width;
			if (ev->value_mask & CWHeight)
				ch = ev->height;
			cw = MAX(1, cw);
			ch = MAX(1, ch);
			resize(c, c->x, c->y, cw + 2 * c->bw, ch + 2 * c->bw + th, 0);
			if (ev->value_mask & (CWX|CWY))
				configure(c);
		} else
			configure(c);
	} else {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = ev->border_width;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	}
	XSync(dpy, False);
}

Monitor *
createmon(void)
{
	Monitor *m;
	size_t i;

	m = ecalloc(1, sizeof(Monitor));
	m->tagset[0] = m->tagset[1] = 1;
	m->showbar = showbar;
	m->topbar = topbar;
	/* sized for MAXTASKS so adding or removing a task never reallocates */
	m->lt = ecalloc(MAXTASKS * NTAGS * 2, sizeof *m->lt);
	m->sellt = ecalloc(MAXTASKS * NTAGS, sizeof *m->sellt);
	m->mfacts = ecalloc(MAXTASKS * NTAGS, sizeof *m->mfacts);
	m->nmasters = ecalloc(MAXTASKS * NTAGS, sizeof *m->nmasters);
	for (i = 0; i < MAXTASKS * NTAGS; i++)
		initpertag(m, i);
	for (i = 0; i < MAXTASKS; i++)
		m->taskview[i].tagset[0] = m->taskview[i].tagset[1] = 1;
	snprintf(m->ltsymbol, sizeof(m->ltsymbol), "%s", layouts[0].symbol);
	return m;
}

/* Default layouts and factors of one task x tag slot. */
void
initpertag(Monitor *m, unsigned int idx)
{
	m->mfacts[idx] = mfact;
	m->nmasters[idx] = nmaster;
	m->sellt[idx] = 0;
	m->lt[idx * 2] = &layouts[0];
	m->lt[idx * 2 + 1] = &layouts[1 % LENGTH(layouts)];
}

/* Layouts are remembered per tag so each workspace keeps its own. When
 * several tags are visible the first one owns the monitor's layout. */
const Layout *
curlayout(Monitor *m)
{
	unsigned int t = PT(curtag(m));

	return m->lt[t * 2 + m->sellt[t]];
}

unsigned int
curtag(Monitor *m)
{
	unsigned int i, tagset = m->tagset[m->seltags];

	for (i = 0; i < LENGTH(tags); i++)
		if (tagset & (1u << i))
			return i;
	return 0;
}

void
destroynotify(XEvent *e)
{
	Client *c;
	XDestroyWindowEvent *ev = &e->xdestroywindow;

	if ((c = wintoclient(ev->window)) && ev->window == c->win)
		unmanage(c, 1);
	else if ((c = wintosystrayicon(ev->window))) {
		removesystrayicon(c);
		resizebarwin(selmon);
		updatesystray();
	}
}

void
detach(Client *c)
{
	Client **tc;

	for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

void
detachstack(Client *c)
{
	Client **tc, *t;

	for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;

	if (c == c->mon->sel) {
		for (t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext);
		c->mon->sel = t;
	}
}

Monitor *
dirtomon(int dir)
{
	Monitor *m = NULL;

	if (dir > 0) {
		if (!(m = selmon->next))
			m = mons;
	} else if (selmon == mons)
		for (m = mons; m->next; m = m->next);
	else
		for (m = mons; m->next != selmon; m = m->next);
	return m;
}

static int
textwidth(const char *s)
{
	return s && *s ? drw_fontset_getwidth(drw, s) : 0;
}

static size_t
utf8charlen(const unsigned char *s)
{
	if (s[0] < 0x80)
		return 1;
	if ((s[0] & 0xE0) == 0xC0)
		return 2;
	if ((s[0] & 0xF0) == 0xE0)
		return 3;
	if ((s[0] & 0xF8) == 0xF0)
		return 4;
	return 1;
}

/* Character-count truncation, exactly like taskbar.py's truncate(). */
void
truncatechars(const char *src, char *dst, size_t dstsz, int maxchars)
{
	static const char ellipsis[] = "\xe2\x80\xa6";
	size_t i = 0;
	int n = 0;

	if (!dstsz)
		return;
	/* maxchars counts the ellipsis too, like polybar's truncate(). */
	if (maxchars > 0)
		maxchars--;
	while (src[i] && n < maxchars) {
		size_t l = utf8charlen((const unsigned char *)src + i);
		if (i + l > dstsz - 1)
			break;
		i += l;
		n++;
	}
	if (!src[i]) {
		snprintf(dst, dstsz, "%s", src);
		return;
	}
	if (i + strlen(ellipsis) > dstsz - 1)
		i = dstsz - strlen(ellipsis) - 1;
	memcpy(dst, src, i);
	strcpy(dst + i, ellipsis);
}

/* Pixel-width truncation, like taskbar.py's truncate_px(). The prefix
 * width grows with its length, so the longest prefix that fits next to the
 * ellipsis is found by a binary search over the UTF-8 character boundaries
 * (O(log n) text measurements instead of one per character). */
void
truncatepx(const char *src, char *dst, size_t dstsz, int maxpx)
{
	static const char ellipsis[] = "\xe2\x80\xa6";
	char candidate[320];
	size_t bounds[320], nb = 0, i = 0, best = 0;
	int lo, hi, mid, ellw;

	if (!dstsz)
		return;
	if (maxpx <= 0 || textwidth(src) <= maxpx) {
		snprintf(dst, dstsz, "%s", src);
		return;
	}
	ellw = textwidth(ellipsis);
	while (src[i] && nb < LENGTH(bounds)) {
		size_t l = utf8charlen((const unsigned char *)src + i);
		if (i + l >= sizeof candidate)
			break;
		i += l;
		bounds[nb++] = i;
	}
	lo = 0;
	hi = (int)nb - 1;
	while (lo <= hi) {
		mid = (lo + hi) / 2;
		memcpy(candidate, src, bounds[mid]);
		candidate[bounds[mid]] = '\0';
		if (textwidth(candidate) + ellw <= maxpx) {
			best = bounds[mid];
			lo = mid + 1;
		} else
			hi = mid - 1;
	}
	if (!best || best + strlen(ellipsis) > dstsz - 1) {
		dst[0] = '\0';
		return;
	}
	memcpy(dst, src, best);
	strcpy(dst + best, ellipsis);
}

static void
freeicon(Client *c)
{
	free(c->icon);
	c->icon = NULL;
	c->taskcap = -1;
}

/* Box-filter the chosen _NET_WM_ICON source down/up to 16x16 and
 * premultiply it.  Premultiplied pixels composite over the scheme
 * background without dark fringes on transparent icon corners. */
static void
scaleicon(const unsigned long *src, int srcw, int srch, uint32_t *dst)
{
	unsigned long p;
	unsigned long long sa, sr, sg, sb;
	unsigned int a, r, g, b, count;
	int x, y, sx, sy, x0, x1, y0, y1;

	if (srcw == ICONSIZE && srch == ICONSIZE) {
		for (y = 0; y < ICONSIZE; y++) {
			for (x = 0; x < ICONSIZE; x++) {
				p = src[y * srcw + x];
				a = (p >> 24) & 0xff;
				r = (p >> 16) & 0xff;
				g = (p >> 8) & 0xff;
				b = p & 0xff;
				dst[y * ICONSIZE + x] = (a << 24)
					| ((r * a / 255) << 16)
					| ((g * a / 255) << 8)
					| (b * a / 255);
			}
		}
		return;
	}
	for (y = 0; y < ICONSIZE; y++) {
		y0 = y * srch / ICONSIZE;
		y1 = (y + 1) * srch / ICONSIZE;
		if (y1 <= y0)
			y1 = y0 + 1;
		for (x = 0; x < ICONSIZE; x++) {
			x0 = x * srcw / ICONSIZE;
			x1 = (x + 1) * srcw / ICONSIZE;
			if (x1 <= x0)
				x1 = x0 + 1;
			sa = sr = sg = sb = 0;
			count = 0;
			for (sy = y0; sy < y1; sy++) {
				for (sx = x0; sx < x1; sx++) {
					p = src[sy * srcw + sx];
					a = (p >> 24) & 0xff;
					sa += a;
					sr += (unsigned long long)((p >> 16) & 0xff) * a;
					sg += (unsigned long long)((p >> 8) & 0xff) * a;
					sb += (unsigned long long)(p & 0xff) * a;
					count++;
				}
			}
			if (!count) {
				dst[y * ICONSIZE + x] = 0;
				continue;
			}
			a = sa / count;
			r = sr / ((unsigned long long)count * 255);
			g = sg / ((unsigned long long)count * 255);
			b = sb / ((unsigned long long)count * 255);
			dst[y * ICONSIZE + x] = (a << 24) | (r << 16) | (g << 8) | b;
		}
	}
}

static uint32_t *
readicon(Window w)
{
	Atom actual;
	int format, have = 0, bestgood = 0, bestw = 0, besth = 0;
	unsigned long i, n, after, best = 0, bestarea = 0, iw, ih, area;
	unsigned char *data = NULL;
	unsigned long *prop;
	uint32_t *icon;

	if (XGetWindowProperty(dpy, w, netatom[NetWmIcon], 0, 1 << 20,
			False, XA_CARDINAL, &actual, &format, &n, &after,
			&data) != Success)
		return NULL;
	if (!data || actual != XA_CARDINAL || format != 32 || after) {
		if (data)
			XFree(data);
		return NULL;
	}
	prop = (unsigned long *)data;
	for (i = 0; i + 2 <= n; ) {
		int good;

		iw = prop[i];
		ih = prop[i + 1];
		i += 2;
		if (!iw || !ih || iw > 512 || ih > 512)
			break;
		if (iw > (n - i) / ih)
			break;
		area = iw * ih;
		good = iw >= ICONSIZE && ih >= ICONSIZE;
		if (!have || (good && !bestgood)
		|| (good == bestgood && ((good && area < bestarea)
		|| (!good && area > bestarea)))) {
			best = i;
			bestw = iw;
			besth = ih;
			bestarea = area;
			bestgood = good;
			have = 1;
		}
		i += area;
	}
	if (!have) {
		XFree(data);
		return NULL;
	}
	icon = ecalloc(ICONSIZE * ICONSIZE, sizeof(uint32_t));
	scaleicon((const unsigned long *)(prop + best), bestw, besth, icon);
	XFree(data);
	return icon;
}

/* Theme icons by class/instance, looked up once per class: the lookup
 * touches the disk. A NULL icon is cached too. */
typedef struct ThemeIcon ThemeIcon;
struct ThemeIcon {
	char key[257];
	uint32_t *icon;
	ThemeIcon *next;
};
static ThemeIcon *themeicons;

static uint32_t *
themeicon(Client *c)
{
	ThemeIcon *t;
	unsigned long *argb;
	uint32_t *icon;
	char key[257];
	int w, h;

	if (!c->class[0] && !c->instance[0])
		return NULL;
	snprintf(key, sizeof key, "%s\n%s", c->class, c->instance);
	for (t = themeicons; t && strcmp(t->key, key); t = t->next);
	if (!t) {
		t = ecalloc(1, sizeof *t);
		snprintf(t->key, sizeof t->key, "%s", key);
		if ((argb = iconthemelookup(c->class, c->instance, &w, &h))) {
			t->icon = ecalloc(ICONSIZE * ICONSIZE, sizeof(uint32_t));
			scaleicon(argb, w, h, t->icon);
			free(argb);
		}
		t->next = themeicons;
		themeicons = t;
	}
	if (!t->icon)
		return NULL;
	icon = ecalloc(ICONSIZE * ICONSIZE, sizeof(uint32_t));
	memcpy(icon, t->icon, ICONSIZE * ICONSIZE * sizeof(uint32_t));
	return icon;
}

/* _NET_WM_ICON, or the icon theme's icon for the window class. */
static void
updateicon(Client *c)
{
	freeicon(c);
	if (!(c->icon = readicon(c->win)))
		c->icon = themeicon(c);
	c->taskcap = -1;
}


/* Tag cell: " N" plus the occupied dot, with the 1px orange frame when
 * selected (matches workspaces.py's styled()+framed() output). */
int
drawtag(Monitor *m, unsigned int i, int x, unsigned int occ, unsigned int urg)
{
	int selected = m->tagset[m->seltags] & (1 << i);
	int urgent = urg & (1 << i);
	int occupied = occ & (1 << i);
	int numw, dotw, w, tx;
	char num[32];

	snprintf(num, sizeof num, " %s", tags[i]);
	numw = textwidth(num);
	dotw = textwidth("\xc2\xb7");
	w = numw + dotw + (selected ? 2 : 0);

	if (selected) {
		drw_setscheme(drw, scheme[SchemeSel]);
		drw_rect(drw, x, 0, w, bh, 1, 1);
		tx = x + 1;
	} else {
		tx = x;
		if (urgent) {
			drw_setscheme(drw, scheme[SchemeUrg]);
			drw_rect(drw, x, 0, w, bh, 1, 1);
		}
	}

	drw_setscheme(drw, scheme[selected ? SchemeSel : urgent ? SchemeUrg
			: occupied ? SchemeTagOcc : SchemeTagEmpty]);
	drw_text(drw, tx, 0, numw, bh, 0, num, 0);

	if (occupied) {
		drw_setscheme(drw, scheme[selected ? SchemeSel : urgent ? SchemeUrg : SchemeDot]);
		drw_text(drw, tx + numw, 0, dotw, bh, 0, "\xc2\xb7", 0);
	}

	if (selected) {
		drw_setscheme(drw, scheme[SchemeAccent]);
		drw_rect(drw, x, 0, w, 1, 1, 0);
		drw_rect(drw, x, bh - 1, w, 1, 1, 0);
		drw_rect(drw, x, 0, 1, bh, 1, 0);
		drw_rect(drw, x + w - 1, 0, 1, bh, 1, 0);
	}
	return w;
}

/* Task cell, left of the tags: same look as a tag cell, with the task's
 * label. occ[i] has bit 0 set when the task has windows, bit 1 when one of
 * them is urgent. The cell being renamed shows the edit buffer. */
int
drawtaskcell(Monitor *m, int i, int x, const unsigned int *occ)
{
	int editing = i == renaming && m == selmon;
	int selected = i == seltask || editing;
	int urgent = !editing && (occ[i] & 2);
	int occupied = !editing && (occ[i] & 1);
	int numw, dotw, w, tx;
	char label[TASKNAMELEN], text[TASKNAMELEN + 8];

	if (editing)
		snprintf(text, sizeof text, " %s_", promptbuf);
	else {
		truncatechars(tasknames[i], label, sizeof label, 16);
		snprintf(text, sizeof text, " %s", label);
	}
	numw = textwidth(text);
	dotw = textwidth("\xc2\xb7");
	w = numw + dotw + (selected ? 2 : 0);

	if (selected) {
		drw_setscheme(drw, scheme[SchemeSel]);
		drw_rect(drw, x, 0, w, bh, 1, 1);
		tx = x + 1;
	} else {
		tx = x;
		if (urgent) {
			drw_setscheme(drw, scheme[SchemeUrg]);
			drw_rect(drw, x, 0, w, bh, 1, 1);
		}
	}

	drw_setscheme(drw, scheme[selected ? SchemeSel : urgent ? SchemeUrg
			: occupied ? SchemeTagOcc : SchemeTagEmpty]);
	drw_text(drw, tx, 0, numw, bh, 0, text, 0);

	if (occupied) {
		drw_setscheme(drw, scheme[selected ? SchemeSel : urgent ? SchemeUrg : SchemeDot]);
		drw_text(drw, tx + numw, 0, dotw, bh, 0, "\xc2\xb7", 0);
	}

	if (selected) {
		drw_setscheme(drw, scheme[SchemeAccent]);
		drw_rect(drw, x, 0, w, 1, 1, 0);
		drw_rect(drw, x, bh - 1, w, 1, 1, 0);
		drw_rect(drw, x, 0, 1, bh, 1, 0);
		drw_rect(drw, x + w - 1, 0, 1, bh, 1, 0);
	}
	return w;
}

int
drawmode(int x)
{
	const char *mode = wmmode == ModeResize ? " resize " : " move ";
	int w = textwidth(mode);

	drw_setscheme(drw, scheme[SchemeSel]);
	drw_text(drw, x, 0, w, bh, 0, mode, 0);
	return x + w;
}

int
drawseparator(int x)
{
	int w = textwidth("|");

	x += 6;
	drw_setscheme(drw, scheme[SchemeSeparator]);
	drw_text(drw, x, 0, w, bh, 0, "|", 0);
	return x + w + 6;
}

/* Title bar of one client, drawn into the shared drw buffer and copied to
 * the frame's title window. i3: 20px, icon + centered title. */
void
drawtitle(Client *c, int focused)
{
	char raw[512], title[512];
	int th = c->th;
	int tw, pad = 8, avail, textw, iconx, textx;
	int urgent = c->isurgent;
	unsigned int seltitle = urgent ? SchemeUrg : focused ? SchemeSel : SchemeNorm;
	Client *t, **list;
	int n = 0, i, idx = -1, cellw, tx, twi, narrow;
	int istabbed;

	if (!c->title || th <= 0)
		return;
	tw = c->w - 2 * c->bw;
	if (tw <= 0)
		return;
	istabbed = curlayout(c->mon)->arrange == tabbed;

	if (istabbed && c == c->mon->tabsel && !c->isfloating) {
		ensuremoncap(c->mon);
		list = c->mon->tabclients;
		for (t = nexttiled(c->mon->clients); t; t = nexttiled(t->next))
			list[n++] = t;
		if (n > 1) {
			cellw = tw / n;
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_rect(drw, 0, 0, tw, th, 1, 1);
			for (i = 0; i < n; i++) {
				t = list[i];
				tx = i * cellw;
				twi = (i == n - 1) ? tw - tx : cellw;
				c->mon->tabx[i] = tx;
				c->mon->tabw[i] = twi;
				if (t == c->mon->tabsel) {
					idx = i;
					drw_setscheme(drw, scheme[SchemeSel]);
				} else
					drw_setscheme(drw, scheme[SchemeNorm]);
				drw_rect(drw, tx, 0, twi, th, 1, 1);
				if (i > 0) {
					drw_setscheme(drw, scheme[SchemeSeparator]);
					drw_rect(drw, tx, 0, 1, th, 1, 0);
				}
				truncatechars(t->name, raw, sizeof raw, 64);
				narrow = !t->icon || twi < 6 + ICONSIZE + icongap + 2 + 6;
				if (narrow) {
					textx = tx + 6;
					avail = twi - 12;
				} else {
					textx = tx + 6 + ICONSIZE + icongap;
					avail = twi - (6 + ICONSIZE + icongap) - 6;
				}
				if (avail < 1)
					title[0] = '\0';
				else if (textwidth(raw) > avail)
					truncatepx(raw, title, sizeof title, avail);
				else
					snprintf(title, sizeof title, "%s", raw);
				if (!narrow && t->icon)
					drw_icon(drw, tx + 6, (th - ICONSIZE) / 2, t->icon,
						&scheme[t == c->mon->tabsel
						? SchemeSel : SchemeNorm][ColBg]);
				textw = textwidth(title);
				if (textw) {
					drw_setscheme(drw, scheme[t == c->mon->tabsel
						? SchemeSel : SchemeNorm]);
					drw_text(drw, textx, 0, textw, th, 0, title, 0);
				}
			}
			c->mon->tabn = n;
			if (idx >= 0) {
				long tab[4] = {
					(idx == 0) ? 0 : c->bw + c->mon->tabx[idx],
					0,
					c->mon->tabw[idx]
						+ ((idx == 0) ? c->bw : 0)
						+ ((idx == n - 1) ? c->bw : 0),
					c->bw + titleheight
				};
				XChangeProperty(dpy, c->frame, netatom[NetDwmTab],
					XA_CARDINAL, 32, PropModeReplace,
					(unsigned char *)tab, 4);
			}
			drw_map(drw, c->title, 0, 0, tw, th);
			return;
		}
		c->mon->tabn = 0;
	}
	XDeleteProperty(dpy, c->frame, netatom[NetDwmTab]);

	/* single window title: icon (when present) + text, no empty slot */
	truncatechars(c->name, raw, sizeof raw, 64);
	iconx = pad;
	textx = pad + (c->icon ? ICONSIZE + icongap : 0);
	avail = tw - textx - pad;
	if (avail < 1)
		title[0] = '\0';
	else if (textwidth(raw) > avail)
		truncatepx(raw, title, sizeof title, avail);
	else
		snprintf(title, sizeof title, "%s", raw);

	drw_setscheme(drw, scheme[seltitle]);
	drw_rect(drw, 0, 0, tw, th, 1, 1);
	if (c->icon)
		drw_icon(drw, iconx, (th - ICONSIZE) / 2, c->icon,
			&scheme[seltitle][ColBg]);
	textw = textwidth(title);
	if (textw)
		drw_text(drw, textx, 0, textw, th, 0, title, 0);
	drw_map(drw, c->title, 0, 0, tw, th);
}

/* Redraw whatever title window actually shows c: in tabbed mode a hidden
 * tiled client's icon must repaint the shared tab strip, not its own title. */
static void
drawclienttitles(Client *c)
{
	Client *t = c;

	if (curlayout(c->mon)->arrange == tabbed && !c->isfloating
	&& c->mon->tabsel)
		t = c->mon->tabsel;
	drawtitle(t, t == c->mon->sel);
}

/* Taskbar title of c for a pixel budget (0 = no cap), cached per client:
 * drawbar() runs on every focus or title change and the fitting search
 * below asks for the same widths again and again. */
static const char *
tasktitle(Client *c, int cap)
{
	char raw[320];

	if (c->taskcap != cap) {
		truncatechars(c->name, raw, sizeof raw, 28);
		if (cap > 0)
			truncatepx(raw, c->tasktitle, sizeof c->tasktitle, cap);
		else
			snprintf(c->tasktitle, sizeof c->tasktitle, "%s", raw);
		c->taskw = textwidth(" ") + (c->icon ? ICONSIZE + icongap : 0)
			+ textwidth(c->tasktitle) + textwidth(" ");
		c->taskcap = cap;
	}
	return c->tasktitle;
}

/* Width of one taskbar item; cap is the title's pixel budget (0 = no cap). */
int
taskwidth(Client *c, int cap)
{
	tasktitle(c, cap);
	/* 1px focus frame on each side */
	return c->taskw + (c == c->mon->sel ? 2 : 0);
}

void
drawtask(Monitor *m, Client *c, int *x, int cap)
{
	int focused = c == m->sel;
	int urgent = c->isurgent;
	int w = taskwidth(c, cap);
	int x0 = *x, tx;
	int seltask = urgent ? SchemeTaskUrg : focused ? SchemeTaskSel
		: c->minimized ? SchemeTagEmpty : SchemeTask; /* minimized: dimmed */
	const char *title = tasktitle(c, cap);

	if (focused || urgent) {
		drw_setscheme(drw, scheme[seltask]);
		drw_rect(drw, x0, 0, w, bh, 1, 1);
	}

	tx = x0 + (focused ? 1 : 0);
	drw_setscheme(drw, scheme[seltask]);
	drw_text(drw, tx, 0, textwidth(" "), bh, 0, " ", 0);
	tx += textwidth(" ");
	if (c->icon) {
		drw_icon(drw, tx, (bh - ICONSIZE) / 2, c->icon,
			&scheme[seltask][ColBg]);
		tx += ICONSIZE + icongap;
	}
	drw_text(drw, tx, 0, textwidth(title), bh, 0, title, 0);
	tx += textwidth(title);
	drw_text(drw, tx, 0, textwidth(" "), bh, 0, " ", 0);

	if (focused) {
		drw_setscheme(drw, scheme[SchemeAccent]);
		drw_rect(drw, x0, 0, w, 1, 1, 0);
		drw_rect(drw, x0, bh - 1, w, 1, 1, 0);
		drw_rect(drw, x0, 0, 1, bh, 1, 0);
		drw_rect(drw, x0 + w - 1, 0, 1, bh, 1, 0);
	}
	*x = x0 + w;
}

void
drawtasks(Monitor *m, int x, int right)
{
	Client *c, **list;
	int n = 0, shown, i, avail, total, sepw, cap = 0, hidden = 0;
	int minpx = 56;

	m->taskn = 0;
	ensuremoncap(m);
	list = m->taskclients;
	for (c = m->clients; c; c = c->next)
		if (ONVIEW(c))
			list[n++] = c;
	if (!n)
		return;

	sepw = textwidth("  |  ");
	avail = right - x;
	if (avail <= 0)
		return;

	total = (n - 1) * sepw;
	for (i = 0; i < n; i++)
		total += taskwidth(list[i], 0);

	if (total > avail) {
		int lo = minpx, hi = 400, best = -1;

		while (lo <= hi) {
			int mid = (lo + hi) / 2, t = (n - 1) * sepw;
			for (i = 0; i < n; i++)
				t += taskwidth(list[i], mid);
			if (t <= avail) {
				best = mid;
				lo = mid + 1;
			} else
				hi = mid - 1;
		}
		if (best >= minpx)
			cap = best;
		else {
			/* Not even minimum titles fit: drop items and show a +N chip. */
			shown = n;
			while (shown > 0) {
				char chip[32];
				int t = (shown - 1) * sepw;
				for (i = 0; i < shown; i++)
					t += taskwidth(list[i], minpx);
				if (shown < n) {
					snprintf(chip, sizeof chip, "+%d", n - shown);
					t += sepw + textwidth(chip);
				}
				if (t <= avail)
					break;
				shown--;
			}
			if (!shown)
				return;
			hidden = n - shown;
			n = shown;
			cap = minpx;
		}
	}

	shown = n;
	for (i = 0; i < shown; i++) {
		m->taskx[i] = x;
		drawtask(m, list[i], &x, cap);
		m->taskn = i + 1;
		if (i + 1 < shown || hidden) {
			drw_setscheme(drw, scheme[SchemeSeparator]);
			drw_text(drw, x, 0, sepw, bh, 0, "  |  ", 0);
			x += sepw;
		}
	}
	/* Right edge of the last task, used by buttonpress' hit test. */
	m->taskx[shown] = x;
	if (hidden) {
		char chip[32];
		snprintf(chip, sizeof chip, "+%d", hidden);
		drw_setscheme(drw, scheme[SchemeIcon]);
		drw_text(drw, x, 0, textwidth(chip), bh, 0, chip, 0);
	}
}

void
drawbar(Monitor *m)
{
	int x, tw = 0, traysw = 0, barw;
	unsigned int i, occ = 0, urg = 0, taskocc[MAXTASKS] = {0};
	Client *c;
	Monitor *om;

	if (!m->showbar)
		return;

	if (showsystray && systray && m == systraytomon(m) && !systrayonleft)
		traysw = getsystraywidth();
	resizebarwin(m);
	barw = m->ww - traysw;
	if (barw < 0)
		barw = 0;
	m->barw = barw;
	m->statusx = -1;

	drw_setscheme(drw, scheme[SchemeNorm]);
	drw_rect(drw, 0, 0, barw, bh, 1, 1);

	/* a task is marked occupied/urgent from any monitor */
	for (om = mons; om; om = om->next)
		for (c = om->clients; c; c = c->next)
			if (c->task >= 0 && c->task < MAXTASKS)
				taskocc[c->task] |= c->isurgent ? 3 : 1;
	for (c = m->clients; c; c = c->next) {
		if (c->task != seltask)
			continue;
		/* A window on every tag (e.g. Chrome's screen-sharing indicator,
		 * which asks for _NET_WM_DESKTOP 0xFFFFFFFF) says nothing about
		 * which tags are in use: don't mark them all occupied. */
		if ((c->tags & TAGMASK) != TAGMASK)
			occ |= c->tags;
		if (c->isurgent)
			urg |= c->tags;
	}

	/* Status text (date), left of the systray, drawn first so tags can
	 * overdraw it, exactly like stock dwm. */
	if (m == selmon) {
		tw = textwidth(stext);
		x = barw - tw - 4;
		if (x < 0)
			x = 0;
		drw_setscheme(drw, scheme[SchemeNorm]);
		drw_text(drw, x, 0, tw + 8, bh, 0, stext, 0);
		m->statusx = x;
	}

	x = 0;
	for (i = 0; i < (unsigned int)ntasks; i++) {
		m->taskcellx[i] = x;
		x += drawtaskcell(m, i, x, taskocc);
	}
	m->taskcellx[ntasks] = x;
	x = drawseparator(x);

	for (i = 0; i < LENGTH(tags); i++) {
		m->tagx[i] = x;
		x += drawtag(m, i, x, occ, urg);
	}
	m->tagx[LENGTH(tags)] = x;

	if (wmmode != ModeNormal)
		x = drawmode(x);
	x = drawseparator(x);

	drawtasks(m, x, barw);

	drw_map(drw, m->barwin, 0, 0, barw, bh);
}

void
drawbars(void)
{
	Monitor *m;

	for (m = mons; m; m = m->next)
		drawbar(m);
}

void
expose(XEvent *e)
{
	Client *c;
	Monitor *m;
	XExposeEvent *ev = &e->xexpose;

	if (ev->count != 0)
		return;
	if ((c = wintoclient(ev->window)) && ev->window == c->title)
		drawtitle(c, c == selmon->sel);
	else if ((m = wintomon(ev->window))) {
		drawbar(m);
		if (m == selmon)
			updatesystray();
	}
}

void
focus(Client *c)
{
	if (!c || !ISVISIBLE(c))
		for (c = selmon->stack; c && !ISVISIBLE(c); c = c->snext);
	if (selmon->sel && selmon->sel != c)
		unfocus(selmon->sel, 0);
	if (c) {
		if (c->mon != selmon)
			selmon = c->mon;
		if (c->isurgent)
			seturgent(c, 0);
		detachstack(c);
		attachstack(c);
		grabbuttons(c, 1);
		setframeborder(c, c->isurgent ? SchemeUrg : SchemeSel);
		selmon->sel = c;
		if (curlayout(c->mon)->arrange == tabbed && !c->isfloating)
			c->mon->tabsel = c;
		/* XSetInputFocus fails with BadMatch on a window that is not
		 * viewable, and hidden clients have their frame unmapped (another
		 * tag, another tab). Map it before giving it the keyboard, or the
		 * focus falls back to PointerRoot once the old frame is hidden. */
		if (!c->mapped)
			arrange(c->mon);
		setfocus(c);
	} else {
		selmon->sel = NULL;
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	drawbars();
	if (c)
		drawtitle(c, 1);
	outlineupdate();
	savestate();
}

void
focusdir(const Arg *arg)
{
	Client *c, *best = NULL;
	int sx, sy, bestscore = 0;

	if (!selmon->sel)
		return;

	/* In tabbed/stacking layouts every tiled frame has the same rectangle,
	 * so geometry cannot choose a neighbour. Left/right cycles the visible
	 * tiling order instead, i.e. the tabs. */
	if (arg->i == DirLeft || arg->i == DirRight) {
		const Layout *l = curlayout(selmon);

		if (l->arrange == tabbed || l->arrange == monocle) {
			Client *cur = !selmon->sel->isfloating ? selmon->sel : selmon->tabsel;
			unsigned int n = 0, idx = 0;

			for (c = nexttiled(selmon->clients); c; c = nexttiled(c->next), n++)
				if (c == cur)
					idx = n;
			if (n == 0)
				return;
			idx = (arg->i == DirLeft) ? (idx + n - 1) % n : (idx + 1) % n;
			for (c = nexttiled(selmon->clients); idx--; c = nexttiled(c->next));
			focus(c);
			restack(selmon);
			return;
		}
	}

	sx = selmon->sel->x + selmon->sel->w / 2;
	sy = selmon->sel->y + selmon->sel->h / 2;
	for (c = selmon->clients; c; c = c->next) {
		int cx, cy, dx, dy, maind, orth, score;

		if (c == selmon->sel || !ISVISIBLE(c))
			continue;
		cx = c->x + c->w / 2;
		cy = c->y + c->h / 2;
		dx = cx - sx;
		dy = cy - sy;
		switch (arg->i) {
		case DirLeft:  if (dx >= 0) continue; maind = -dx; orth = abs(dy); break;
		case DirRight: if (dx <= 0) continue; maind = dx;  orth = abs(dy); break;
		case DirUp:    if (dy >= 0) continue; maind = -dy; orth = abs(dx); break;
		default:       if (dy <= 0) continue; maind = dy;  orth = abs(dx); break;
		}
		score = maind + 2 * orth;
		if (!best || score < bestscore) {
			best = c;
			bestscore = score;
		}
	}
	if (best) {
		focus(best);
		restack(selmon);
	}
}

void
focusmode(const Arg *arg)
{
	Client *c, *cur = selmon->sel;

	if (!cur)
		return;
	for (c = selmon->stack; c; c = c->snext)
		if (c != cur && ISVISIBLE(c) && c->isfloating != cur->isfloating)
			break;
	if (c) {
		focus(c);
		restack(selmon);
	}
}

void
focustask(const Arg *arg)
{
	Client *c, *t;

	if (arg->i < 0 || arg->i >= selmon->taskn)
		return;
	c = selmon->taskclients[arg->i];
	if (!c)
		return;
	for (t = selmon->clients; t && t != c; t = t->next);
	if (!t || !ONVIEW(c))
		return;
	/* like a Windows taskbar: the focused window minimizes, a minimized
	 * one comes back, any other one gets the focus */
	if (c->minimized)
		setminimized(c, 0);
	else if (c == selmon->sel)
		setminimized(c, 1);
	else {
		focus(c);
		restack(selmon);
	}
}

/* Minimize: hide the window, the layout goes on without it. Restore: it
 * comes back in its place in the window order (floating windows with
 * their geometry, fullscreen ones still fullscreen) and gets the focus. */
void
setminimized(Client *c, int minimized)
{
	minimized = minimized ? 1 : 0;
	if (c->minimized == minimized)
		return;
	c->minimized = minimized;
	setclientstate(c, minimized ? IconicState : NormalState);
	updatenetstate(c);
	if (minimized) {
		if (c->mon->tabsel == c)
			c->mon->tabsel = NULL;
		if (c->mon->sel == c && c->mon != selmon) {
			Client *s;

			for (s = c->mon->stack; s && !ISVISIBLE(s); s = s->snext);
			c->mon->sel = s;
		}
		focus(NULL);
		arrange(c->mon);
	} else if (ISVISIBLE(c)) {
		focus(c);
		arrange(c->mon);
		restack(c->mon);
	} else {
		arrange(c->mon);
		drawbars();
	}
	savestate();
}

void
focustab(const Arg *arg)
{
	Client *c;

	if (arg->i < 0 || arg->i >= selmon->tabn)
		return;
	c = selmon->tabclients[arg->i];
	if (!c || !ISVISIBLE(c))
		return;
	focus(c);
	restack(selmon);
}

/* there are some broken focus acquiring clients needing extra handling */
void
focusin(XEvent *e)
{
	XFocusChangeEvent *ev = &e->xfocus;

	if (selmon->sel && ev->window != selmon->sel->win)
		setfocus(selmon->sel);
}

void
focusmon(const Arg *arg)
{
	Monitor *m;

	if (!mons->next)
		return;
	if ((m = dirtomon(arg->i)) == selmon)
		return;
	unfocus(selmon->sel, 0);
	selmon = m;
	focus(NULL);
}

void
focusstack(const Arg *arg)
{
	Client *c = NULL, *i;

	if (!selmon->sel || (selmon->sel->isfullscreen && lockfullscreen))
		return;
	if (arg->i > 0) {
		for (c = selmon->sel->next; c && !ISVISIBLE(c); c = c->next);
		if (!c)
			for (c = selmon->clients; c && !ISVISIBLE(c); c = c->next);
	} else {
		for (i = selmon->clients; i != selmon->sel; i = i->next)
			if (ISVISIBLE(i))
				c = i;
		if (!c)
			for (; i; i = i->next)
				if (ISVISIBLE(i))
					c = i;
	}
	if (c) {
		focus(c);
		restack(selmon);
	}
}

/* Put c back at the size/position the client requested when it was mapped,
 * like i3's floating_enable(): the tiled cell is usually larger than
 * floating_maximum_size, so keeping it left no room to grow in resize mode. */
void
floatnatural(Client *c)
{
	Monitor *m = c->mon;
	int x = c->natx, y = c->naty, w = c->natw, h = c->nath;

	if (w <= 0 || h <= 0)
		return;
	w = MIN(w, floatmaxw);
	h = MIN(h, floatmaxh);
	if (x + w > m->wx + m->ww)
		x = m->wx + m->ww - w;
	if (y + h > m->wy + m->wh)
		y = m->wy + m->wh - h;
	x = MAX(x, m->wx);
	y = MAX(y, m->wy);
	resize(c, x, y, w, h, 0);
}

/* Center c on its monitor's work area. Windows that do not tile always open
 * here, so the position the client asked for is ignored. The size comes from
 * the size hints (a fixed-size prompt must be centered at its hinted size) and
 * resize() may clamp it further; re-center after it ran to keep the final
 * geometry centered. */
void
centerclient(Client *c)
{
	Monitor *m = c->mon;
	int w = c->w, h = c->h, x = c->x, y = c->y;

	if (!m)
		return;
	applysizehints(c, &x, &y, &w, &h, 0);
	w = MIN(w, m->ww);
	h = MIN(h, m->wh);
	x = m->wx + (m->ww - w) / 2;
	y = m->wy + (m->wh - h) / 2;
	resize(c, x, y, w, h, 0);
	x = m->wx + (m->ww - c->w) / 2;
	y = m->wy + (m->wh - c->h) / 2;
	if (x != c->x || y != c->y)
		resize(c, x, y, c->w, c->h, 0);
}

Atom
getatomprop(Client *c, Atom prop)
{
	int format;
	unsigned long nitems, dl;
	unsigned char *p = NULL;
	Atom da, atom = None;

	/* FIXME getatomprop should return the number of items and a pointer to
	 * the stored data instead of this workaround */
	Atom req = XA_ATOM;
	if (prop == xatom[XembedInfo])
		req = xatom[XembedInfo];

	if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, req,
		&da, &format, &nitems, &dl, &p) == Success && p) {
		if (nitems > 0 && format == 32)
			atom = *(long *)p;
		if (da == xatom[XembedInfo] && nitems == 2 && format == 32)
			atom = ((long *)p)[1];
		XFree(p);
	}
	return atom;
}

unsigned int
getsystraywidth()
{
	unsigned int w = 0;
	Client *i;
	if(showsystray && systray)
		for(i = systray->icons; i; w += i->w + systrayspacing, i = i->next) ;
	return w ? w + systrayspacing : 1;
}

int
getrootptr(int *x, int *y)
{
	int di;
	unsigned int dui;
	Window dummy;

	return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long
getstate(Window w)
{
	int format;
	long result = -1;
	unsigned char *p = NULL;
	unsigned long n, extra;
	Atom real;

	if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
		&real, &format, &n, &extra, &p) != Success)
		return -1;
	if (n != 0 && format == 32)
		result = *(long *)p;
	XFree(p);
	return result;
}

int
gettextprop(Window w, Atom atom, char *text, unsigned int size)
{
	char **list = NULL;
	int n;
	XTextProperty name;

	if (!text || size == 0)
		return 0;
	text[0] = '\0';
	if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
		return 0;
	if (name.encoding == XA_STRING) {
		snprintf(text, size, "%s", (char *)name.value);
	} else if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success) {
		if (n > 0 && *list)
			snprintf(text, size, "%s", *list);
		XFreeStringList(list);
	}
	XFree(name.value);
	return 1;
}

void
grabbuttons(Client *c, int focused)
{
	updatenumlockmask();
	{
		unsigned int i, j;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		if (!focused)
			XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
				BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
		for (i = 0; i < LENGTH(buttons); i++)
			if (buttons[i].click == ClkClientWin)
				for (j = 0; j < LENGTH(modifiers); j++)
					XGrabButton(dpy, buttons[i].button,
						buttons[i].mask | modifiers[j],
						c->win, False, BUTTONMASK,
						GrabModeAsync, GrabModeSync, None, None);
	}
}

void
grabkeys(void)
{
	updatenumlockmask();
	{
		unsigned int i, j, k;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		int start, end, skip;
		KeySym *syms;

		XUngrabKey(dpy, AnyKey, AnyModifier, root);
		XDisplayKeycodes(dpy, &start, &end);
		syms = XGetKeyboardMapping(dpy, start, end - start + 1, &skip);
		if (!syms)
			return;
		for (k = start; k <= end; k++)
			for (i = 0; i < LENGTH(keys); i++)
				/* skip modifier codes, we do that ourselves */
				if (keys[i].keysym == syms[(k - start) * skip])
					for (j = 0; j < LENGTH(modifiers); j++)
						XGrabKey(dpy, k,
							 keys[i].mod | modifiers[j],
							 root, True,
							 GrabModeAsync, GrabModeAsync);
		XFree(syms);
	}
}

void
incnmaster(const Arg *arg)
{
	NMASTER(selmon) = MAX(NMASTER(selmon) + arg->i, 0);
	arrange(selmon);
}

#ifdef XINERAMA
static int
isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info)
{
	while (n--)
		if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
		&& unique[n].width == info->width && unique[n].height == info->height)
			return 0;
	return 1;
}
#endif /* XINERAMA */

void
keypress(XEvent *e)
{
	unsigned int i;
	KeySym keysym;
	XKeyEvent *ev;

	if (renaming >= 0) {
		promptkey(e);
		return;
	}
	ev = &e->xkey;
	keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
	if (wmmode != ModeNormal && modekey(e, keysym))
		return;
	for (i = 0; i < LENGTH(keys); i++)
		if (keysym == keys[i].keysym
		&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
		&& keys[i].func)
			keys[i].func(&(keys[i].arg));
}

void
closeclient(Client *c)
{
	if (!c)
		return;
	if (!sendevent(c->win, wmatom[WMDelete], NoEventMask, wmatom[WMDelete], CurrentTime, 0 , 0, 0)) {
		XGrabServer(dpy);
		XSetErrorHandler(xerrordummy);
		XSetCloseDownMode(dpy, DestroyAll);
		XKillClient(dpy, c->win);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
}

void
killclient(const Arg *arg)
{
	closeclient(selmon->sel);
}

void
manage(Window w, XWindowAttributes *wa, int adopt)
{
	Client *c, *t = NULL;
	Window trans = None;
	XSetWindowAttributes fwa, twa;
	int th, depth = DefaultDepth(dpy, screen);
	Visual *visual = DefaultVisual(dpy, screen);
	unsigned long fmask = CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWEventMask;
	long st[CLIENTSTATE];

	c = ecalloc(1, sizeof(Client));
	c->win = w;
	c->cfact = 1.0;
	c->taskcap = -1;
	c->extents[0] = -1; /* nothing published yet */
	c->oldbw = wa->border_width;
	c->bw = borderpx;

	updatetitle(c);
	updateclass(c);
	updateicon(c);
	if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
		c->mon = t->mon;
		c->tags = t->tags;
		c->task = t->task;
	} else {
		c->mon = selmon;
		c->task = seltask;
		applyrules(c);
	}
	updatewindowtype(c);
	/* dwm restart: the previous instance left tags, monitor, floating
	 * state and size factor on the window. */
	if (adopt && readclientstate(w, st)) {
		Monitor *m;

		if (st[0] & TAGMASK)
			c->tags = st[0] & TAGMASK;
		for (m = mons; m && m->num != st[1]; m = m->next);
		if (m) {
			c->mon = m;
			if (!(c->tags & TAGMASK))
				c->tags = m->tagset[m->seltags];
		}
		c->isfloating = st[2] ? 1 : 0;
		if (st[3] > 0)
			c->cfact = st[3] / 1000.0;
		c->task = st[5] >= 0 && st[5] < ntasks ? (int)st[5] : 0;
		c->minimized = st[6] ? 1 : 0;
	}
	updatemotifhints(c);
	updatesizehints(c);
	th = titlehfor(c, c->isfloating || trans != None || c->isfixed);
	c->th = th;

	/* The frame is the layout unit: X border + title bar + client. */
	c->x = c->oldx = wa->x + wa->border_width - borderpx;
	c->y = c->oldy = wa->y + wa->border_width - borderpx - th;
	c->w = c->oldw = wa->width + 2 * borderpx;
	c->h = c->oldh = wa->height + 2 * borderpx + th;
	if (c->isfloating) {
		c->w = MIN(c->w, floatmaxw);
		c->h = MIN(c->h, floatmaxh);
	}
	if (c->x + c->w > c->mon->wx + c->mon->ww)
		c->x = c->mon->wx + c->mon->ww - c->w;
	if (c->y + c->h > c->mon->wy + c->mon->wh)
		c->y = c->mon->wy + c->mon->wh - c->h;
	c->x = MAX(c->x, c->mon->wx);
	c->y = MAX(c->y, c->mon->wy);
	/* i3 floats a window at the geometry it asked for when it was mapped,
	 * not at the tiled cell it happened to occupy. */
	c->natx = c->x;
	c->naty = c->y;
	c->natw = c->w;
	c->nath = c->h;

	/* A 32-bit (ARGB) client keeps its alpha channel only inside a frame
	 * of the same depth and visual; everything else uses the defaults. */
	if (wa->depth == 32 && wa->visual) {
		c->argb = 1;
		depth = 32;
		visual = wa->visual;
		c->cmap = XCreateColormap(dpy, root, visual, AllocNone);
		fwa.colormap = c->cmap;
		fmask |= CWColormap;
	}
	fwa.override_redirect = True;
	fwa.background_pixel = framepixel(c, scheme[SchemeNorm][ColBorder].pixel);
	fwa.border_pixel = framepixel(c, scheme[SchemeNorm][ColBorder].pixel);
	fwa.event_mask = ButtonPressMask|SubstructureRedirectMask;
	c->frame = XCreateWindow(dpy, root, c->x, c->y,
		c->w - 2 * c->bw, c->h - 2 * c->bw, c->bw,
		depth, InputOutput, visual, fmask, &fwa);

	/* The title bar is drawn by drw at the default depth. */
	twa.background_pixel = scheme[SchemeNorm][ColBg].pixel;
	twa.border_pixel = 0;
	twa.colormap = DefaultColormap(dpy, screen);
	twa.event_mask = ButtonPressMask|ExposureMask;
	c->title = XCreateWindow(dpy, c->frame, 0, 0, c->w - 2 * c->bw,
		MAX(th, 1), 0, DefaultDepth(dpy, screen), InputOutput,
		DefaultVisual(dpy, screen),
		CWBackPixel|CWBorderPixel|CWColormap|CWEventMask, &twa);
	XDefineCursor(dpy, c->title, cursor[CurNormal]->cursor);

	XAddToSaveSet(dpy, w);
	XReparentWindow(dpy, w, c->frame, 0, th);
	XSetWindowBorderWidth(dpy, w, 0);
	XMoveResizeWindow(dpy, w, 0, th, wa->width, wa->height);
	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	grabbuttons(c, 0);

	updatesizehints(c);
	updatewmhints(c);
	if (!c->isfloating)
		c->isfloating = c->oldstate = trans != None || c->isfixed;
	/* A window that does not tile opens centered, whatever position it asked
	 * for. scan() adopts already placed clients on restart: leave them. */
	if (!adopt && !c->isfullscreen
	&& (c->isfloating || !curlayout(c->mon)->arrange))
		centerclient(c);
	/* Clients that map already fullscreen (players, games): the frame
	 * exists now, so setfullscreen() can cover the monitor. */
	if (getatomprop(c, netatom[NetWMState]) == netatom[NetWMFullscreen])
		setfullscreen(c, 1);
	if (c->isfloating)
		XRaiseWindow(dpy, c->frame);

	attach(c);
	attachstack(c);
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
		(unsigned char *) &(c->win), 1);
	setclientstate(c, c->minimized ? IconicState : NormalState);
	if (c->minimized)
		updatenetstate(c);
	if (c->mon == selmon)
		unfocus(selmon->sel, 0);
	c->mon->sel = c;

	XMapWindow(dpy, c->win);
	if (c->th > 0 && !c->isfullscreen) {
		XMapWindow(dpy, c->title);
		c->titlemapped = 1;
	}
	setframeextents(c->win, c->bw, c->th, c->extents);
	arrange(c->mon);
	drawtitle(c, c == selmon->sel);
	focus(NULL);
}

void
mappingnotify(XEvent *e)
{
	XMappingEvent *ev = &e->xmapping;

	XRefreshKeyboardMapping(ev);
	if (ev->request == MappingKeyboard)
		grabkeys();
}

void
maprequest(XEvent *e)
{
	static XWindowAttributes wa;
	XMapRequestEvent *ev = &e->xmaprequest;

	Client *i;
	if ((i = wintosystrayicon(ev->window))) {
		sendevent(i->win, netatom[Xembed], StructureNotifyMask, CurrentTime, XEMBED_WINDOW_ACTIVATE, 0, systray->win, XEMBED_EMBEDDED_VERSION);
		resizebarwin(selmon);
		updatesystray();
	}

	if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect)
		return;
	if (!wintoclient(ev->window))
		manage(ev->window, &wa, 0);
}

void
tabselect(Monitor *m)
{
	Client *c, *first = NULL;

	if (!m || curlayout(m)->arrange != tabbed)
		return;
	for (c = nexttiled(m->clients); c; c = nexttiled(c->next)) {
		if (!first)
			first = c;
		if (c == m->sel) {
			m->tabsel = c;
			return;
		}
	}
	if (m->tabsel && m->tabsel->mon == m && ISVISIBLE(m->tabsel)
	&& !m->tabsel->isfloating)
		return;
	m->tabsel = first;
}

void
tabbed(Monitor *m)
{
	Client *c;

	tabselect(m);
	for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
		place(c, m->wx + gappo, m->wy + gappo,
			m->ww - 2 * gappo, m->wh - 2 * gappo);
}

void
monocle(Monitor *m)
{
	unsigned int n = 0;
	Client *c;

	for (c = m->clients; c; c = c->next)
		if (ISVISIBLE(c))
			n++;
	if (n > 0) /* override layout symbol */
		snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);
	for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
		resize(c, m->wx + gappo, m->wy + gappo,
			m->ww - 2 * gappo, m->wh - 2 * gappo, 0);
}

void
motionnotify(XEvent *e)
{
	static Monitor *mon = NULL;
	Monitor *m;
	XMotionEvent *ev = &e->xmotion;

	if (ev->window != root)
		return;
	if ((m = recttomon(ev->x_root, ev->y_root, 1, 1)) != mon && mon) {
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
	}
	mon = m;
}

/* Swap two nodes in the tiling order list. */
void
swapclients(Monitor *m, Client *a, Client *b)
{
	Client **pa, **pb, *anext, *bnext;

	if (a == b)
		return;
	for (pa = &m->clients; *pa && *pa != a; pa = &(*pa)->next);
	for (pb = &m->clients; *pb && *pb != b; pb = &(*pb)->next);
	if (!*pa || !*pb)
		return;
	if (a->next == b) {
		*pa = b;
		a->next = b->next;
		b->next = a;
	} else if (b->next == a) {
		*pb = a;
		b->next = a->next;
		a->next = b;
	} else {
		anext = a->next;
		bnext = b->next;
		*pa = b;
		b->next = anext;
		*pb = a;
		a->next = bnext;
	}
}

void
movedir(const Arg *arg)
{
	Client *c, *prev = NULL, *next = NULL;

	if (!selmon->sel)
		return;
	c = selmon->sel;
	if (c->isfloating) {
		int x = c->x, y = c->y;

		switch (arg->i) {
		case DirLeft:  x -= 10; break;
		case DirRight: x += 10; break;
		case DirUp:    y -= 10; break;
		default:       y += 10; break;
		}
		resize(c, x, y, c->w, c->h, 1);
		return;
	}
	for (c = nexttiled(selmon->clients); c && c != selmon->sel; c = nexttiled(c->next))
		prev = c;
	if (!c)
		return;
	next = nexttiled(c->next);
	c = (arg->i == DirLeft || arg->i == DirUp) ? prev : next;
	if (!c)
		return;
	swapclients(selmon, selmon->sel, c);
	arrange(selmon);
}

void
titleclick(const Arg *arg)
{
	/* i3: dragging the title bar of a floating window moves it; a tiled
	 * window only needs the focus (Mod+drag is bound to movemouse). */
	if (selmon->sel && selmon->sel->isfloating && !selmon->sel->isfullscreen)
		movemouse(arg);
}

/* Wait for the drag's ButtonRelease; false when no button is held any more
 * (a _NET_WM_MOVERESIZE that arrived after a quick click). */
static int
buttonheld(void)
{
	int di;
	unsigned int mask;
	Window dummy;

	if (!XQueryPointer(dpy, root, &dummy, &dummy, &di, &di, &di, &di, &mask))
		return 0;
	return (mask & (Button1Mask|Button2Mask|Button3Mask|Button4Mask|Button5Mask)) != 0;
}

/* Tiled client under the root position x,y on m, other than c. */
static Client *
tiledat(Monitor *m, Client *c, int x, int y)
{
	Client *t;

	for (t = nexttiled(m->clients); t; t = nexttiled(t->next))
		if (t != c && x >= t->x && x < t->x + t->w && y >= t->y && y < t->y + t->h)
			return t;
	return NULL;
}

/* Drag c with the pointer. A floating window (or any window in the floating
 * layout) follows the pointer; a tiled one stays tiled and, on release,
 * swaps places with the tiled window under the pointer, or moves to the
 * monitor under it (i3 drag-to-reposition). The focus outline follows the
 * drop target while dragging. */
void
dragmove(Client *c)
{
	int x, y, ocx, ocy, nx, ny, tiled;
	Client *t;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!c || c->isfullscreen) /* no support moving fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
	tiled = !c->isfloating && curlayout(c->mon)->arrange;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
		return;
	if (!getrootptr(&x, &y) || !buttonheld()) {
		XUngrabPointer(dpy, CurrentTime);
		return;
	}
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / refreshrate))
				continue;
			lasttime = ev.xmotion.time;
			if (tiled)
				break;

			nx = ocx + (ev.xmotion.x - x);
			ny = ocy + (ev.xmotion.y - y);
			if (abs(selmon->wx - nx) < snap)
				nx = selmon->wx;
			else if (abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < snap)
				nx = selmon->wx + selmon->ww - WIDTH(c);
			if (abs(selmon->wy - ny) < snap)
				ny = selmon->wy;
			else if (abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c))) < snap)
				ny = selmon->wy + selmon->wh - HEIGHT(c);
			resize(c, nx, ny, c->w, c->h, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
	if (tiled) {
		x = ev.xbutton.x_root;
		y = ev.xbutton.y_root;
		m = recttomon(x, y, 1, 1);
		if (m != c->mon) {
			sendmon(c, m);
			selmon = m;
			focus(c);
			restack(m);
		} else if ((t = tiledat(m, c, x, y))) {
			swapclients(m, c, t);
			arrange(m);
		}
		return;
	}
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
}

void
movemouse(const Arg *arg)
{
	dragmove(selmon->sel);
}

Client *
nexttiled(Client *c)
{
	for (; c && (c->isfloating || !ISVISIBLE(c)); c = c->next);
	return c;
}

/* One outline edge, in overlay-local coordinates. */
static int
outlineadd(XRectangle *rects, int n, int max, int x, int y, int w, int h)
{
	if (w <= 0 || h <= 0 || n >= max)
		return n;
	if (x < 0 || y < 0 || x + w > SHRT_MAX || y + h > SHRT_MAX)
		return n;
	rects[n].x = x;
	rects[n].y = y;
	rects[n].width = w;
	rects[n].height = h;
	return n + 1;
}

/* Edges of the hollow outline relative to the overlay window. `containerw`
 * is the frame's outer width and `tab` is the focused tab relative to the
 * frame's outer rect (NULL for a plain frame). This is the same geometry as
 * i3-focus-outline's outline_rects(): the top edge stays high over the
 * focused tab and drops to the content top over the inactive tabs, with
 * vertical steps at the tab boundaries. */
static int
outlinerects(XRectangle *rects, int max, int width, int height,
	int containerw, const XRectangle *tab, int t)
{
	int n = 0, tableft, tabright, contenttop, lowery, lefttop, righttop;

	if (width <= 2 * t || height <= 2 * t)
		return 0;

	if (!tab) {
		n = outlineadd(rects, n, max, 0, 0, width, t);
		n = outlineadd(rects, n, max, 0, height - t, width, t);
		n = outlineadd(rects, n, max, 0, t, t, height - 2 * t);
		n = outlineadd(rects, n, max, width - t, t, t, height - 2 * t);
		return n;
	}

	contenttop = t + tab->y + tab->height;
	lowery = contenttop - t;
	tableft = t + tab->x;
	tabright = tableft + tab->width;

	/* Top edge over the focused tab. */
	n = outlineadd(rects, n, max, tableft, 0, tab->width, t);

	if (tab->x > 0) {
		/* Lowered top edge over the inactive tabs on the left and the
		 * step up to the focused tab. */
		n = outlineadd(rects, n, max, 0, lowery, tableft, t);
		n = outlineadd(rects, n, max, tableft - t, 0, t, contenttop);
	}
	if (tab->x + tab->width < containerw) {
		n = outlineadd(rects, n, max, tabright, 0, t, contenttop);
		n = outlineadd(rects, n, max, tabright, lowery,
			width - tabright, t);
	}

	/* The vertical edges start at the level of their adjacent top edge. */
	lefttop = (tab->x > 0) ? lowery : 0;
	righttop = (tab->x + tab->width < containerw) ? lowery : 0;
	n = outlineadd(rects, n, max, 0, lefttop, t, height - lefttop);
	n = outlineadd(rects, n, max, width - t, righttop, t, height - righttop);
	n = outlineadd(rects, n, max, 0, height - t, width, t);

	return n;
}

static void
outlineshape(int width, int height, int containerw, const XRectangle *tab)
{
	XRectangle rects[8];
	int n;

	n = outlinerects(rects, LENGTH(rects), width, height, containerw, tab,
		(int)outlinepx);
	XShapeCombineRectangles(dpy, outlinewin, ShapeBounding, 0, 0,
		rects, n, ShapeSet, Unsorted);
}

/* Tab rectangle of `c` relative to its frame's outer rect, or 0 when the
 * frame has no shared tab strip. Mirrors the geometry drawtitle() publishes
 * as _DWM_TAB. */
static int
outlinetabrect(Client *c, XRectangle *tab)
{
	Monitor *m = c->mon;
	Client *t;
	int n = 0, idx = -1, tw, cellw, tx, twi;

	if (!m || !c->title || c->isfullscreen || c->isfloating
	|| titleheight <= 0 || curlayout(m)->arrange != tabbed)
		return 0;
	tw = c->w - 2 * c->bw;
	if (tw <= 0)
		return 0;
	for (t = nexttiled(m->clients); t; t = nexttiled(t->next), n++)
		if (t == c)
			idx = n;
	if (n <= 1)
		return 0;
	cellw = tw / n;
	if (idx < 0)
		return 0;
	tx = idx * cellw;
	twi = (idx == n - 1) ? tw - tx : cellw;
	tab->x = (idx == 0) ? 0 : c->bw + tx;
	tab->y = 0;
	tab->width = twi + ((idx == 0) ? c->bw : 0)
		+ ((idx == n - 1) ? c->bw : 0);
	tab->height = c->bw + titleheight;
	return 1;
}

static void
outlinecreate(void)
{
	XSetWindowAttributes swa;
	int major = 0, minor = 0, eventbase, errorbase;

	if (!outlinepx)
		return;
	if (!XShapeQueryExtension(dpy, &eventbase, &errorbase)) {
		fprintf(stderr, "dwm: XShape missing, focus outline disabled\n");
		return;
	}
	if (!XShapeQueryVersion(dpy, &major, &minor)
	|| major < 1 || (major == 1 && minor < 1)) {
		fprintf(stderr, "dwm: XShape 1.1 (ShapeInput) missing, focus outline disabled\n");
		return;
	}

	swa.override_redirect = True;
	swa.background_pixel = scheme[SchemeAccent][ColFg].pixel;
	swa.border_pixel = 0;
	swa.event_mask = NoEventMask;
	outlinewin = XCreateWindow(dpy, root, 0, 0, 1, 1, 0, CopyFromParent,
		InputOutput, CopyFromParent,
		CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask, &swa);
	if (!outlinewin) {
		fprintf(stderr, "dwm: could not create the focus outline window\n");
		return;
	}
	/* Empty input shape: clicks go through the outline to the frame. */
	XShapeCombineRectangles(dpy, outlinewin, ShapeInput, 0, 0, NULL, 0,
		ShapeSet, Unsorted);
	XStoreName(dpy, outlinewin, "dwm-focus-outline");
}

static void
outlinehide(void)
{
	if (outlinewin && outlinemapped) {
		XUnmapWindow(dpy, outlinewin);
		outlinemapped = 0;
	}
	outlinetabvalid = 0;
}

/* Keep the outline around selmon->sel: its frame's outer rect plus the tab
 * notch, hidden while the client is fullscreen. Only the width of the frame
 * is used, so this works on every monitor and while moving/resizing. */
static void
outlineupdate(void)
{
	XWindowChanges wc;
	XRectangle tab;
	Client *c;
	int hastab, t = (int)outlinepx;
	int x, y, w, h;

	if (!outlinewin)
		return;
	c = selmon ? selmon->sel : NULL;
	if (!c || !c->mapped || !ISVISIBLE(c) || c->isfullscreen
	|| c->w < 1 || c->h < 1
	|| c->w + 2 * t > SHRT_MAX || c->h + 2 * t > SHRT_MAX) {
		outlinehide();
		return;
	}

	x = c->x - t;
	y = c->y - t;
	w = c->w + 2 * t;
	h = c->h + 2 * t;
	hastab = outlinetabrect(c, &tab);

	if (!outlinemapped || x != outlinex || y != outliney
	|| w != outlinew || h != outlineh || hastab != outlinetabvalid
	|| (hastab && (tab.x != outlinetab.x || tab.y != outlinetab.y
	|| tab.width != outlinetab.width || tab.height != outlinetab.height))) {
		XMoveResizeWindow(dpy, outlinewin, x, y, w, h);
		outlineshape(w, h, c->w, hastab ? &tab : NULL);
		outlinex = x;
		outliney = y;
		outlinew = w;
		outlineh = h;
		outlinetabvalid = hastab;
		if (hastab)
			outlinetab = tab;
	}

	if (!outlinemapped) {
		XMapWindow(dpy, outlinewin);
		outlinemapped = 1;
	}

	/* Stack immediately above the focused frame, so floating windows,
	 * menus and notifications stacked higher stay visible. */
	wc.sibling = c->frame;
	wc.stack_mode = Above;
	XConfigureWindow(dpy, outlinewin, CWSibling | CWStackMode, &wc);
}

void
pop(Client *c)
{
	detach(c);
	attach(c);
	focus(c);
	arrange(c->mon);
}

void
propertynotify(XEvent *e)
{
	Client *c;
	Window trans;
	XPropertyEvent *ev = &e->xproperty;

	if ((c = wintosystrayicon(ev->window))) {
		if (ev->atom == XA_WM_NORMAL_HINTS) {
			updatesizehints(c);
			updatesystrayicongeom(c, c->w, c->h);
		}
		else
			updatesystrayiconstate(c, ev);
		resizebarwin(selmon);
		updatesystray();
	}

	if ((ev->window == root) && (ev->atom == XA_WM_NAME))
		updatestatus();
	else if ((c = wintoclient(ev->window)) && ev->atom == netatom[NetWmIcon]) {
		updateicon(c); /* deleted: the theme icon, if any */
		drawclienttitles(c);
		drawbar(c->mon);
	}
	else if (ev->state == PropertyDelete)
		return; /* ignore */
	else if ((c = wintoclient(ev->window))) {
		switch(ev->atom) {
		default: break;
		case XA_WM_TRANSIENT_FOR:
			if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
				(c->isfloating = (wintoclient(trans)) != NULL))
				arrange(c->mon);
			break;
		case XA_WM_NORMAL_HINTS:
			c->hintsvalid = 0;
			updatesizehints(c);
			/* GCR/gnome-keyring prompts advertise a fixed size after
			 * mapping. A tiled cell is then smaller than the client and
			 * clips the bottom buttons. Float fixed-size windows and
			 * center them at the hinted size. */
			if (c->isfixed) {
				int fw = MIN(c->minw + 2 * c->bw, c->mon->ww);
				int fh = MIN(c->minh + 2 * c->bw + titlehfor(c, 1), c->mon->wh);

				if (!c->isfloating) {
					c->isfloating = 1;
					centerclient(c);
					arrange(c->mon);
				} else if (c->w != fw || c->h != fh) {
					centerclient(c);
					arrange(c->mon);
				}
			}
			break;
		case XA_WM_HINTS:
			updatewmhints(c);
			setframeborder(c, c->isurgent ? SchemeUrg
				: c == selmon->sel ? SchemeSel : SchemeNorm);
			drawclienttitles(c);
			drawbars();
			break;
		}
		if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
			updatetitle(c);
			drawbar(c->mon);
		}
		if (ev->atom == XA_WM_CLASS) {
			updateclass(c);
			updateicon(c);
			drawtitle(c, c == selmon->sel);
			drawbar(c->mon);
		}
		if (ev->atom == netatom[NetWMWindowType])
			updatewindowtype(c);
		if (ev->atom == dwmatom[MotifWMHints]) {
			int old = c->notitle;

			updatemotifhints(c);
			if (old != c->notitle) {
				resize(c, c->x, c->y, c->w, c->h, 0);
				arrange(c->mon);
			}
		}
	}
}

void
quit(const Arg *arg)
{
	running = 0;
}

/* Leave run(), clean up (clients go back to the root with their state saved
 * in properties) and exec the dwm binary again: a freshly installed one is
 * picked up through PATH. */
void
restart(const Arg *arg)
{
	restartwm = 1;
	running = 0;
}

static long
layoutindex(const Layout *l)
{
	long i;

	for (i = 0; i < (long)LENGTH(layouts); i++)
		if (l == &layouts[i])
			return i;
	return 0;
}

/* _DWM_CLIENT_STATE and the EWMH _NET_WM_DESKTOP of one client. Both are
 * kept up to date (not only at restart) so even a crashed dwm can be
 * restored by the next one. */
static void
setclientprops(Client *c, long order)
{
	long st[CLIENTSTATE], desk;

	st[0] = c->tags;
	st[1] = c->mon->num;
	st[2] = c->isfullscreen ? c->oldstate : c->isfloating;
	st[3] = (long)(c->cfact * 1000 + 0.5);
	st[4] = order;
	st[5] = c->task;
	st[6] = c->minimized;
	if (c->statevalid && !memcmp(st, c->savedstate, sizeof st))
		return;
	memcpy(c->savedstate, st, sizeof st);
	c->statevalid = 1;
	XChangeProperty(dpy, c->win, dwmatom[DwmClientState], XA_CARDINAL, 32,
		PropModeReplace, (unsigned char *)st, CLIENTSTATE);
	desk = (c->tags & TAGMASK) == TAGMASK ? 0xFFFFFFFFL
		: (long)(c->task * NTAGS + ffs((int)c->tags) - 1);
	XChangeProperty(dpy, c->win, netatom[NetWMDesktop], XA_CARDINAL, 32,
		PropModeReplace, (unsigned char *)&desk, 1);
}

static int
readclientstate(Window w, long *st)
{
	Atom type;
	int format, ok = 0;
	unsigned long n, after;
	unsigned char *p = NULL;

	if (XGetWindowProperty(dpy, w, dwmatom[DwmClientState], 0, CLIENTSTATE,
		False, XA_CARDINAL, &type, &format, &n, &after, &p) == Success && p) {
		/* older dwm: CLIENTSTATE1 without task (0), CLIENTSTATE2
		 * without minimized (0) */
		if (type == XA_CARDINAL && format == 32
		&& (n == CLIENTSTATE || n == CLIENTSTATE2 || n == CLIENTSTATE1)) {
			memset(st, 0, CLIENTSTATE * sizeof(long));
			memcpy(st, p, n * sizeof(long));
			ok = 1;
		}
		XFree(p);
	}
	return ok;
}

/* What monitor m shows inside task t: live for the active task. */
static TaskView
taskviewof(Monitor *m, int t)
{
	TaskView tv;

	if (t != seltask)
		return m->taskview[t];
	tv.tagset[0] = m->tagset[0];
	tv.tagset[1] = m->tagset[1];
	tv.seltags = m->seltags;
	tv.sel = m->sel ? m->sel->win : 0;
	return tv;
}

/* Tasks, monitor tags, layouts, factors and the focused clients on the root
 * window (_DWM_STATE), client tags/task/floating/order on each client, plus
 * the EWMH desktop properties. Only changed values are written. The task
 * labels live in ~/.dwm/tasks (savetasks()). */
void
savestate(void)
{
	static long *prev;
	static size_t prevn;
	static long prevdesk = -1;
	Monitor *m;
	Client *c;
	TaskView tv;
	long *buf, order, desk;
	size_t n, k, nmons = 0;
	unsigned int t;
	int task;

	if (!stateready || cleaningup || !selmon)
		return;
	for (m = mons; m; m = m->next)
		nmons++;
	n = STATEHEAD + nmons * (MONSTATE + ntasks * (VIEWSTATE + TAGSTATE * NTAGS));
	buf = ecalloc(n, sizeof(long));
	buf[0] = STATEMAGIC;
	buf[1] = NTAGS;
	buf[2] = nmons;
	buf[3] = selmon->num;
	buf[4] = ntasks;
	buf[5] = seltask;
	buf[6] = prevtask;
	k = STATEHEAD;
	for (m = mons; m; m = m->next) {
		buf[k++] = m->num;
		buf[k++] = m->showbar;
		for (task = 0; task < ntasks; task++) {
			tv = taskviewof(m, task);
			buf[k++] = tv.seltags;
			buf[k++] = tv.tagset[0];
			buf[k++] = tv.tagset[1];
			buf[k++] = (long)tv.sel;
		}
		for (t = 0; t < ntasks * NTAGS; t++) {
			buf[k++] = m->sellt[t];
			buf[k++] = layoutindex(m->lt[t * 2]);
			buf[k++] = layoutindex(m->lt[t * 2 + 1]);
			buf[k++] = (long)(m->mfacts[t] * 1000 + 0.5);
			buf[k++] = m->nmasters[t];
		}
		for (order = 0, c = m->clients; c; c = c->next, order++)
			setclientprops(c, order);
	}
	if (n != prevn || memcmp(buf, prev, n * sizeof(long))) {
		XChangeProperty(dpy, root, dwmatom[DwmState], XA_CARDINAL, 32,
			PropModeReplace, (unsigned char *)buf, n);
		free(prev);
		prev = buf;
		prevn = n;
	} else
		free(buf);
	desk = seltask * NTAGS + curtag(selmon);
	if (desk != prevdesk) {
		XChangeProperty(dpy, root, netatom[NetCurrentDesktop], XA_CARDINAL, 32,
			PropModeReplace, (unsigned char *)&desk, 1);
		prevdesk = desk;
	}
}

static void
loadtagstate(Monitor *m, unsigned int idx, const long *st)
{
	long v;

	m->sellt[idx] = st[0] & 1;
	if (st[1] >= 0 && st[1] < (long)LENGTH(layouts))
		m->lt[idx * 2] = &layouts[st[1]];
	if (st[2] >= 0 && st[2] < (long)LENGTH(layouts))
		m->lt[idx * 2 + 1] = &layouts[st[2]];
	v = st[3];
	if (v >= 50 && v <= 950)
		m->mfacts[idx] = v / 1000.0;
	if (st[4] >= 0 && st[4] < 64)
		m->nmasters[idx] = (int)st[4];
}

static void
loadview(TaskView *tv, long seltags, long tagset0, long tagset1, long sel)
{
	tv->seltags = seltags & 1;
	if (tagset0 & TAGMASK)
		tv->tagset[0] = tagset0 & TAGMASK;
	if (tagset1 & TAGMASK)
		tv->tagset[1] = tagset1 & TAGMASK;
	tv->sel = (Window)sel;
}

/* Apply _DWM_STATE to the monitors before scan() adopts the clients. The
 * "dwm1" state of a dwm without tasks is read as task 0. */
void
loadstate(void)
{
	Atom type;
	int format, task, savedtasks, v1;
	unsigned long n, after, k, monlen, head;
	unsigned char *p = NULL;
	long *st, nmons, i;
	unsigned int t;
	Monitor *m;

	if (XGetWindowProperty(dpy, root, dwmatom[DwmState], 0, 1 << 20, False,
		XA_CARDINAL, &type, &format, &n, &after, &p) != Success || !p)
		return;
	st = (long *)p;
	if (type != XA_CARDINAL || format != 32 || n < STATEHEAD1
	|| (st[0] != STATEMAGIC && st[0] != STATEMAGIC1)
	|| st[1] != (long)NTAGS
	|| (st[0] == STATEMAGIC && (n < STATEHEAD || st[4] < 1 || st[4] > MAXTASKS))) {
		XFree(p);
		return;
	}
	v1 = st[0] == STATEMAGIC1;
	nmons = st[2];
	loadedselmon = (int)st[3];
	savedtasks = v1 ? 1 : (int)st[4];
	head = v1 ? STATEHEAD1 : STATEHEAD;
	monlen = v1 ? MONSTATE1 + TAGSTATE * NTAGS
		: MONSTATE + savedtasks * (VIEWSTATE + TAGSTATE * NTAGS);
	if (!v1) {
		seltask = st[5] >= 0 && st[5] < ntasks ? (int)st[5] : 0;
		prevtask = st[6] >= 0 && st[6] < ntasks ? (int)st[6] : seltask;
	}
	k = head;
	for (i = 0; i < nmons && k + monlen <= n; i++, k += monlen) {
		for (m = mons; m && m->num != st[k]; m = m->next);
		if (!m)
			continue;
		if (v1) {
			loadview(&m->taskview[0], st[k + 1], st[k + 2], st[k + 3], st[k + 4]);
			m->showbar = st[k + 5] ? 1 : 0;
			for (t = 0; t < NTAGS; t++)
				loadtagstate(m, t, st + k + MONSTATE1 + t * TAGSTATE);
		} else {
			const long *vs = st + k + MONSTATE;
			const long *ts = vs + savedtasks * VIEWSTATE;

			m->showbar = st[k + 1] ? 1 : 0;
			for (task = 0; task < savedtasks && task < ntasks; task++) {
				loadview(&m->taskview[task], vs[task * VIEWSTATE],
					vs[task * VIEWSTATE + 1], vs[task * VIEWSTATE + 2],
					vs[task * VIEWSTATE + 3]);
				for (t = 0; t < NTAGS; t++)
					loadtagstate(m, task * NTAGS + t,
						ts + (task * NTAGS + t) * TAGSTATE);
			}
		}
		/* the active task's view is the monitor's live state */
		m->tagset[0] = m->taskview[seltask].tagset[0];
		m->tagset[1] = m->taskview[seltask].tagset[1];
		m->seltags = m->taskview[seltask].seltags;
		if (m->num >= 0 && m->num < (int)LENGTH(loadedsel))
			loadedsel[m->num] = m->taskview[seltask].sel;
		updatebarpos(m);
	}
	XFree(p);
}

/* After scan(): give the focus back to the clients focused before. */
static void
restorefocus(void)
{
	Monitor *m;
	Client *c;

	for (m = mons; m; m = m->next) {
		if (m->num < 0 || m->num >= (int)LENGTH(loadedsel) || !loadedsel[m->num])
			continue;
		if ((c = wintoclient(loadedsel[m->num])) && c->mon == m && ISVISIBLE(c)) {
			detachstack(c);
			attachstack(c);
			m->sel = c;
		}
	}
	for (m = mons; m && m->num != loadedselmon; m = m->next);
	if (m)
		selmon = m;
	focus(selmon->sel);
	arrange(NULL);
}

Monitor *
recttomon(int x, int y, int w, int h)
{
	Monitor *m, *r = selmon;
	int a, area = 0;

	for (m = mons; m; m = m->next)
		if ((a = INTERSECT(x, y, w, h, m)) > area) {
			area = a;
			r = m;
		}
	return r;
}

void
removesystrayicon(Client *i)
{
	Client **ii;

	if (!showsystray || !i)
		return;
	for (ii = &systray->icons; *ii && *ii != i; ii = &(*ii)->next);
	if (*ii)
		*ii = i->next;
	free(i);
}

void
resize(Client *c, int x, int y, int w, int h, int interact)
{
	/* A pure move (same size) must not re-apply size hints or the floating
	 * maximum: doing so made the first mouse motion snap/resize the window.
	 * Keep only the "do not lose the window off-screen" clamp. */
	if (titleh(c) != c->th) {
		/* the title bar appears/disappears (layout, floating, motif
		 * hints): keep the client size, the frame absorbs the change */
		if (w == c->w && h == c->h)
			h += titleh(c) - c->th;
		if (applysizehints(c, &x, &y, &w, &h, interact) || titleh(c) != c->th)
			resizeclient(c, x, y, w, h);
		return;
	}
	if (w == c->w && h == c->h) {
		if (interact) {
			if (x > sw)
				x = sw - w;
			if (y > sh)
				y = sh - h;
			if (x + w < 0)
				x = 0;
			if (y + h < 0)
				y = 0;
		}
		if (x != c->x || y != c->y)
			resizeclient(c, x, y, w, h);
		return;
	}
	/* Limit growth to the floating maximum, but only while the window is
	 * still within it: a window that is already larger (a tiled cell that
	 * was just floated) must not be shrunk by the first resize, and must
	 * stay resizable beyond the maximum. */
	if (c->isfloating && !c->isfullscreen) {
		if (c->w <= floatmaxw)
			w = MIN(w, floatmaxw);
		if (c->h <= floatmaxh)
			h = MIN(h, floatmaxh);
	}
	if (applysizehints(c, &x, &y, &w, &h, interact))
		resizeclient(c, x, y, w, h);
}

void
resizebarwin(Monitor *m) {
	unsigned int w = m->ww;
	if (showsystray && systray && m == systraytomon(m) && !systrayonleft)
		w -= getsystraywidth();
	/* drawbar() calls this on every redraw: only move when it changed */
	if (m->barwx == m->wx && m->barwy == m->by && m->barww == (int)w)
		return;
	m->barwx = m->wx;
	m->barwy = m->by;
	m->barww = (int)w;
	XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, w, bh);
}

void
resizeclient(Client *c, int x, int y, int w, int h)
{
	XWindowChanges wc;
	int th = titleh(c);
	int fw = MAX(1, w - 2 * c->bw);
	int fh = MAX(1, h - 2 * c->bw);
	int cw = fw;
	int ch = MAX(1, fh - th);
	int redraw = (w != c->w || h != c->h || th != c->th);

	c->oldx = c->x; c->x = wc.x = x;
	c->oldy = c->y; c->y = wc.y = y;
	c->oldw = c->w; c->w = wc.width = w;
	c->oldh = c->h; c->h = h;
	wc.width = fw;
	wc.height = fh;
	wc.border_width = c->bw;
	XConfigureWindow(dpy, c->frame, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	c->th = th;
	if (th > 0 && c->title) {
		XMoveResizeWindow(dpy, c->title, 0, 0, fw, th);
		if (!c->titlemapped) {
			XMapWindow(dpy, c->title);
			c->titlemapped = 1;
		}
		if (redraw)
			drawtitle(c, c == selmon->sel);
	} else if (c->title && c->titlemapped) {
		XUnmapWindow(dpy, c->title);
		c->titlemapped = 0;
	}
	XMoveResizeWindow(dpy, c->win, 0, th, cw, ch);
	setframeextents(c->win, c->bw, th, c->extents);
	configure(c);
	XSync(dpy, False);
	if (c == selmon->sel)
		outlineupdate();
}

void
resizerequest(XEvent *e)
{
	XResizeRequestEvent *ev = &e->xresizerequest;
	Client *i;

	if ((i = wintosystrayicon(ev->window))) {
		updatesystrayicongeom(i, ev->width, ev->height);
		resizebarwin(selmon);
		updatesystray();
	}
}

void
resizemouse(const Arg *arg)
{
	dragresize(selmon->sel, 0);
}

/* Resize c with the pointer from `anchor` (RLeft|RTop...), or, when it is
 * 0, from the corner/edge nearest to the pointer. */
void
dragresize(Client *c, int anchor)
{
	int x, y, ocx, ocy, ocw, och, nx, ny, nw, nh, px, py;
	int offx = 0, offy = 0, given = anchor != 0;
	int dl, dr, dt, db, near_left, near_right, near_top, near_bottom, edges;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!c)
		return;
	if (c->isfullscreen) /* no support resizing fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
	ocw = c->w;
	och = c->h;
	if (!getrootptr(&x, &y))
		return;

	/* Which corner/edge did the pointer grab? A click on the 5px frame
	 * border is precise; a Mod+right drag inside the window uses the
	 * nearest corner. */
	dl = x - ocx;
	dr = ocx + ocw - 1 - x;
	dt = y - ocy;
	db = ocy + och - 1 - y;
	near_left = dl <= 10;
	near_right = dr <= 10;
	near_top = dt <= 10;
	near_bottom = db <= 10;
	edges = near_left + near_right + near_top + near_bottom;
	if (anchor) {
		/* given by _NET_WM_MOVERESIZE */
	} else if (edges >= 2) {
		anchor = (near_left ? RLeft : RRight) | (near_top ? RTop : RBottom);
	} else if (edges == 1) {
		anchor = near_left ? RLeft : near_right ? RRight : near_top ? RTop : RBottom;
	} else {
		anchor = (x < ocx + ocw / 2 ? RLeft : RRight)
			| (y < ocy + och / 2 ? RTop : RBottom);
	}

	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess)
		return;
	if (!buttonheld()) {
		XUngrabPointer(dpy, CurrentTime);
		return;
	}

	/* The moving edges follow the pointer at their current offset. A
	 * dwm binding puts the pointer on the grabbed corner/edge first; a
	 * client's _NET_WM_MOVERESIZE keeps it where the client's drag began. */
	nx = x;
	ny = y;
	if (anchor & RLeft)
		nx = ocx;
	else if (anchor & RRight)
		nx = ocx + ocw - 1;
	if (anchor & RTop)
		ny = ocy;
	else if (anchor & RBottom)
		ny = ocy + och - 1;
	if (given) {
		offx = nx - x;
		offy = ny - y;
	} else
		XWarpPointer(dpy, None, root, 0, 0, 0, 0, nx, ny);

	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / refreshrate))
				continue;
			lasttime = ev.xmotion.time;

			nx = ocx;
			ny = ocy;
			nw = ocw;
			nh = och;
			px = ev.xmotion.x_root + offx;
			py = ev.xmotion.y_root + offy;
			if (anchor & RLeft) {
				nx = px;
				nw = ocx + ocw - nx;
			} else if (anchor & RRight) {
				nw = px - ocx + 1;
			}
			if (anchor & RTop) {
				ny = py;
				nh = ocy + och - ny;
			} else if (anchor & RBottom) {
				nh = py - ocy + 1;
			}
			if (nw < 1)
				nw = 1;
			if (nh < 1)
				nh = 1;
			/* Keep the fixed edge fixed when the moving edge crosses it. */
			if (nw < 2 * c->bw + 1) {
				if (anchor & RLeft)
					nx = ocx + ocw - (2 * c->bw + 1);
				nw = 2 * c->bw + 1;
			}
			if (nh < 2 * c->bw + c->th + 1) {
				if (anchor & RTop)
					ny = ocy + och - (2 * c->bw + c->th + 1);
				nh = 2 * c->bw + c->th + 1;
			}
			if (!c->isfloating && curlayout(c->mon)->arrange
			&& (abs(nw - c->w) > snap || abs(nh - c->h) > snap)) {
				/* Drag-to-float keeps the current cell; the explicit
				 * Mod+Space toggle is what restores natural size. */
				c->isfloating = 1;
				arrange(c->mon);
			}
			if (!curlayout(c->mon)->arrange || c->isfloating)
				resize(c, nx, ny, nw, nh, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
}

void
restack(Monitor *m)
{
	Client *c;
	XEvent ev;
	XWindowChanges wc;

	drawbar(m);
	if (m->sel) {
		if (m->sel->isfullscreen) {
			/* Fullscreen must cover the bar. */
			XRaiseWindow(dpy, m->sel->frame);
		} else {
			/* Tiled group: directly below the bar, in stack order. */
			if (curlayout(m)->arrange) {
				wc.stack_mode = Below;
				wc.sibling = m->barwin;
				for (c = m->stack; c; c = c->snext)
					if (!c->isfloating && ISVISIBLE(c)) {
						XConfigureWindow(dpy, c->frame, CWSibling|CWStackMode, &wc);
						wc.sibling = c->frame;
					}
			}
			/* Floating group (or the whole floating layout): above the
			 * tiled group, below the bar; the selected window on top. */
			wc.stack_mode = Below;
			wc.sibling = m->barwin;
			for (c = m->stack; c; c = c->snext)
				if (c != m->sel && ISVISIBLE(c)
				&& (!curlayout(m)->arrange || c->isfloating)) {
					XConfigureWindow(dpy, c->frame, CWSibling|CWStackMode, &wc);
					wc.sibling = c->frame;
				}
			if (!curlayout(m)->arrange || m->sel->isfloating) {
				wc.sibling = m->barwin;
				wc.stack_mode = Below;
				XConfigureWindow(dpy, m->sel->frame, CWSibling|CWStackMode, &wc);
			}
		}
	}
	XSync(dpy, False);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	outlineupdate();
}

/* SIGHUP restarts dwm in place, SIGTERM/SIGINT quit through cleanup(), so
 * clients are reparented back to the root and keep their state. The handler
 * only records the signal and wakes run() through a self-pipe. */
static void
sighandler(int sig)
{
	int saved = errno;

	pendingsig = sig;
	if (sigpipe[1] != -1 && write(sigpipe[1], "", 1) == -1) {
		/* pipe full: a wake-up is already pending */
	}
	errno = saved;
}

static void
handlesignals(void)
{
	int sig = pendingsig;

	pendingsig = 0;
	if (sig == SIGHUP)
		restart(NULL);
	else if (sig == SIGTERM || sig == SIGINT)
		quit(NULL);
}

void
run(void)
{
	XEvent ev;
	fd_set fds;
	char buf[64];
	int xfd = ConnectionNumber(dpy), maxfd;

	/* main event loop */
	XSync(dpy, False);
	maxfd = MAX(xfd, sigpipe[0]);
	while (running) {
		while (running && XPending(dpy)) {
			XNextEvent(dpy, &ev);
			if (handler[ev.type])
				handler[ev.type](&ev); /* call handler */
		}
		if (pendingsig)
			handlesignals();
		if (!running)
			break;
		FD_ZERO(&fds);
		FD_SET(xfd, &fds);
		if (sigpipe[0] != -1)
			FD_SET(sigpipe[0], &fds);
		if (select(maxfd + 1, &fds, NULL, NULL, NULL) == -1) {
			if (errno == EINTR)
				continue;
			die("dwm: select:");
		}
		if (sigpipe[0] != -1 && FD_ISSET(sigpipe[0], &fds))
			while (read(sigpipe[0], buf, sizeof buf) > 0);
	}
}

void
scan(void)
{
	unsigned int i, num;
	Window d1, d2, *wins = NULL;
	XWindowAttributes wa;

	if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		Window *cand = ecalloc(num ? num : 1, sizeof(Window));
		long *order = ecalloc(num ? num : 1, sizeof(long)), st[CLIENTSTATE], to;
		unsigned int n = 0, j;
		Window tw;

		for (i = 0; i < num; i++) {
			if (!XGetWindowAttributes(dpy, wins[i], &wa)
			|| wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
				continue;
			/* A previous reparenting WM may leave a NormalState client
			 * unmapped when its frame was destroyed (dwm restart). Manage
			 * it so it gets a frame/title and is mapped again. */
			if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState
			|| getstate(wins[i]) == NormalState) {
				cand[n] = wins[i];
				order[n++] = readclientstate(wins[i], st) ? st[4] : LONG_MAX;
			}
		}
		/* attach() prepends, so manage the last client of the saved order
		 * first; clients without a saved order go to the end. */
		for (i = 1; i < n; i++)
			for (j = i; j > 0 && order[j - 1] < order[j]; j--) {
				to = order[j]; order[j] = order[j - 1]; order[j - 1] = to;
				tw = cand[j]; cand[j] = cand[j - 1]; cand[j - 1] = tw;
			}
		for (i = 0; i < n; i++)
			if (XGetWindowAttributes(dpy, cand[i], &wa))
				manage(cand[i], &wa, 1);
		free(cand);
		free(order);
		for (i = 0; i < num; i++) { /* now the transients */
			if (!XGetWindowAttributes(dpy, wins[i], &wa))
				continue;
			if (XGetTransientForHint(dpy, wins[i], &d1)
			&& (wa.map_state == IsViewable || getstate(wins[i]) == IconicState
			|| getstate(wins[i]) == NormalState))
				manage(wins[i], &wa, 1);
		}
		if (wins)
			XFree(wins);
	}
}

void
sendmon(Client *c, Monitor *m)
{
	if (c->mon == m)
		return;
	unfocus(c, 1);
	detach(c);
	detachstack(c);
	if (c->mon->tabsel == c)
		c->mon->tabsel = NULL;
	c->mon = m;
	c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
	attach(c);
	attachstack(c);
	if (c->isfullscreen)
		resizeclient(c, m->mx, m->my, m->mw, m->mh);
	focus(NULL);
	arrange(NULL);
}

void
setclientstate(Client *c, long state)
{
	long data[] = { state, None };

	XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
		PropModeReplace, (unsigned char *)data, 2);
}

int
sendevent(Window w, Atom proto, int mask, long d0, long d1, long d2, long d3, long d4)
{
	int n;
	Atom *protocols, mt;
	int exists = 0;
	XEvent ev;

	if (proto == wmatom[WMTakeFocus] || proto == wmatom[WMDelete]) {
		mt = wmatom[WMProtocols];
		if (XGetWMProtocols(dpy, w, &protocols, &n)) {
			while (!exists && n--)
				exists = protocols[n] == proto;
			XFree(protocols);
		}
	}
	else {
		exists = True;
		mt = proto;
	}

	if (exists) {
		ev.type = ClientMessage;
		ev.xclient.window = w;
		ev.xclient.message_type = mt;
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = d0;
		ev.xclient.data.l[1] = d1;
		ev.xclient.data.l[2] = d2;
		ev.xclient.data.l[3] = d3;
		ev.xclient.data.l[4] = d4;
		XSendEvent(dpy, w, False, mask, &ev);
	}
	return exists;
}

void
setfocus(Client *c)
{
	if (!c->neverfocus)
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
	XChangeProperty(dpy, root, netatom[NetActiveWindow], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *)&c->win, 1);
	sendevent(c->win, wmatom[WMTakeFocus], NoEventMask, wmatom[WMTakeFocus], CurrentTime, 0, 0, 0);
}

void
setfullscreen(Client *c, int fullscreen)
{
	if (fullscreen && !c->isfullscreen) {
		c->isfullscreen = 1;
		updatenetstate(c);
		c->oldstate = c->isfloating;
		c->oldbw = c->bw;
		c->bw = 0;
		c->isfloating = 1;
		XSetWindowBorderWidth(dpy, c->frame, 0);
		resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
		XRaiseWindow(dpy, c->frame);
	} else if (!fullscreen && c->isfullscreen) {
		c->isfullscreen = 0;
		updatenetstate(c);
		c->isfloating = c->oldstate;
		c->bw = c->oldbw;
		XSetWindowBorderWidth(dpy, c->frame, c->bw);
		c->x = c->oldx;
		c->y = c->oldy;
		c->w = c->oldw;
		c->h = c->oldh;
		resizeclient(c, c->x, c->y, c->w, c->h);
		drawtitle(c, c == selmon->sel);
		arrange(c->mon);
	}
	if (c == selmon->sel)
		outlineupdate();
}

void
setlayout(const Arg *arg)
{
	unsigned int t = PT(curtag(selmon));
	unsigned int slot;

	if (!arg || !arg->v || arg->v != selmon->lt[t * 2 + selmon->sellt[t]])
		selmon->sellt[t] ^= 1;
	slot = t * 2 + selmon->sellt[t];
	if (arg && arg->v)
		selmon->lt[slot] = (Layout *)arg->v;
	snprintf(selmon->ltsymbol, sizeof(selmon->ltsymbol), "%s", selmon->lt[slot]->symbol);
	if (selmon->sel)
		arrange(selmon);
	else {
		drawbar(selmon);
		savestate();
	}
}

/* arg > 1.0 will set mfact absolutely */
void
setmfact(const Arg *arg)
{
	float f;

	if (!arg || !curlayout(selmon)->arrange)
		return;
	f = arg->f < 1.0 ? arg->f + MFACT(selmon) : arg->f - 1.0;
	if (f < 0.05 || f > 0.95)
		return;
	MFACT(selmon) = f;
	arrange(selmon);
}

void
setmode(const Arg *arg)
{
	int newmode = (wmmode == arg->i) ? ModeNormal : arg->i;

	if (wmmode == ModeNormal && newmode != ModeNormal)
		grabmodekeys(1);
	else if (wmmode != ModeNormal && newmode == ModeNormal)
		grabmodekeys(0);
	wmmode = newmode;
	drawbars();
}

/* While a mode is active, bare arrows/Return/Escape are passive grabs on the
 * root; otherwise the focused client receives them and the mode never sees
 * them.  Shift is grabbed for the 50px move step, and the lock bits keep the
 * grabs working with CapsLock/NumLock. */
static void
grabmodekeys(int grab)
{
	static const KeySym syms[] = {
		XK_Left, XK_Right, XK_Up, XK_Down,
		XK_j, XK_k, XK_l, XK_semicolon,
		XK_Escape, XK_Return
	};
	unsigned int mods[] = {
		0, ShiftMask, LockMask, numlockmask,
		LockMask | numlockmask,
		ShiftMask | LockMask,
		ShiftMask | numlockmask,
		ShiftMask | LockMask | numlockmask
	};
	unsigned int i, j;
	KeyCode code;

	for (i = 0; i < LENGTH(syms); i++) {
		if (!(code = XKeysymToKeycode(dpy, syms[i])))
			continue;
		for (j = 0; j < LENGTH(mods); j++) {
			if (grab)
				XGrabKey(dpy, code, mods[j], root, True,
					GrabModeAsync, GrabModeAsync);
			else
				XUngrabKey(dpy, code, mods[j], root);
		}
	}
	XSync(dpy, False);
}

/* i3's "resize grow/shrink width/height" for a tiled window. The axis that
 * crosses the master/stack split moves the split (mfact, from the side of
 * the focused window); the other axis changes the window's share of its
 * column or row (cfact) by the same 2 percentage points. */
static void
resizetiled(Client *c, int dw, int dh)
{
	Monitor *m = c->mon;
	void (*ar)(Monitor *) = curlayout(m)->arrange;
	unsigned int n = 0, idx = 0, nm, first, count;
	int splitdir, sharedir;
	float total = 0, share, rest;
	Client *t;
	Arg a;

	if (ar != tile && ar != tilev)
		return;
	for (t = nexttiled(m->clients); t; t = nexttiled(t->next), n++)
		if (t == c)
			idx = n;
	nm = MIN(n, (unsigned int)NMASTER(m));
	splitdir = ar == tile ? dw : dh;
	sharedir = ar == tile ? dh : dw;

	if (splitdir) {
		if (!nm || nm == n)
			return;
		a.f = (idx < nm ? 0.02f : -0.02f) * splitdir;
		setmfact(&a);
		return;
	}
	if (!sharedir)
		return;
	first = idx < nm ? 0 : nm;
	count = idx < nm ? nm : n - nm;
	if (count < 2)
		return;
	for (n = 0, t = nexttiled(m->clients); t; t = nexttiled(t->next), n++)
		if (n >= first && n < first + count)
			total += t->cfact;
	rest = total - c->cfact;
	share = c->cfact / total + 0.02f * sharedir;
	share = MAX(0.05f, MIN(0.95f, share));
	c->cfact = MAX(0.05f, MIN(20.0f, share * rest / (1 - share)));
	arrange(m);
}

int
modekey(XEvent *e, KeySym keysym)
{
	Client *c = selmon->sel;
	int shift = (e->xkey.state & ShiftMask) != 0;
	int step = 10;
	Arg a;

	if (keysym == XK_Escape || keysym == XK_Return
	|| (keysym == XK_r && CLEANMASK(e->xkey.state) == MODKEY)
	|| (keysym == XK_m && CLEANMASK(e->xkey.state) == MODKEY)) {
		wmmode = ModeNormal;
		grabmodekeys(0);
		drawbars();
		return 1;
	}
	if (!c)
		return 1;

	if (wmmode == ModeResize) {
		if (c->isfullscreen)
			return 1;
		if (c->isfloating || !curlayout(c->mon)->arrange) {
			int x = c->x, y = c->y, w = c->w, h = c->h;

			switch (keysym) {
			case XK_Left:  case XK_j: w = MAX(1, w - step); break;
			case XK_Right: case XK_semicolon: w = MAX(1, w + step); break;
			case XK_Up:    case XK_l: h = MAX(1, h - step); break;
			case XK_Down:  case XK_k: h = MAX(1, h + step); break;
			default: return 1;
			}
			resize(c, x, y, w, h, 1);
		} else if (shift && (keysym == XK_Up || keysym == XK_l)) {
			a.i = -1;
			incnmaster(&a);
		} else if (shift && (keysym == XK_Down || keysym == XK_k)) {
			a.i = 1;
			incnmaster(&a);
		} else {
			switch (keysym) {
			case XK_Left:  case XK_j: resizetiled(c, -1, 0); break;
			case XK_Right: case XK_semicolon: resizetiled(c, 1, 0); break;
			case XK_Up:    case XK_l: resizetiled(c, 0, -1); break;
			case XK_Down:  case XK_k: resizetiled(c, 0, 1); break;
			default: return 1;
			}
		}
		drawbars();
		return 1;
	}

	if (wmmode == ModeMove) {
		if (keysym != XK_Left && keysym != XK_Right && keysym != XK_Up && keysym != XK_Down)
			return 1;
		if (shift)
			step = 50;
		if (c->isfloating || !curlayout(c->mon)->arrange) {
			int x = c->x, y = c->y;

			switch (keysym) {
			case XK_Left:  x -= step; break;
			case XK_Right: x += step; break;
			case XK_Up:    y -= step; break;
			default:       y += step; break;
			}
			resize(c, x, y, c->w, c->h, 1);
		} else {
			a.i = keysym == XK_Left ? DirLeft : keysym == XK_Right ? DirRight
				: keysym == XK_Up ? DirUp : DirDown;
			movedir(&a);
		}
		return 1;
	}
	return 1;
}

void
setup(void)
{
	int i;
	XSetWindowAttributes wa;
	Atom utf8string;
	struct sigaction sa;

	/* do not transform children into zombies when they terminate */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);

	/* clean up any zombies (inherited from .xinitrc etc) immediately */
	while (waitpid(-1, NULL, WNOHANG) > 0);

	/* SIGHUP restarts, SIGTERM/SIGINT quit cleanly (see sighandler()) */
	if (pipe(sigpipe) == -1)
		die("dwm: pipe:");
	for (i = 0; i < 2; i++) {
		fcntl(sigpipe[i], F_SETFD, FD_CLOEXEC);
		fcntl(sigpipe[i], F_SETFL, fcntl(sigpipe[i], F_GETFL) | O_NONBLOCK);
	}
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = sighandler;
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	/* task labels (~/.dwm/tasks), before the monitors are created */
	loadtasks();

	/* init screen */
	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);
	drw = drw_create(dpy, screen, root, sw, sh);
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");
	lrpad = drw->fonts->h;
	bh = MAX(barheight, drw->fonts->h + 2);
	updategeom();
	/* init atoms */
	utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	wmatom[WMChangeState] = XInternAtom(dpy, "WM_CHANGE_STATE", False);
	netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
	netatom[NetSystemTray] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_S0", False);
	netatom[NetSystemTrayOP] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
	netatom[NetSystemTrayOrientation] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION", False);
	netatom[NetSystemTrayOrientationHorz] = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION_HORZ", False);
	netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
	netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	netatom[NetDwmTab] = XInternAtom(dpy, "_DWM_TAB", False);
	netatom[NetWmIcon] = XInternAtom(dpy, "_NET_WM_ICON", False);
	netatom[NetWMDesktop] = XInternAtom(dpy, "_NET_WM_DESKTOP", False);
	netatom[NetCurrentDesktop] = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
	netatom[NetNumberOfDesktops] = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
	netatom[NetDesktopNames] = XInternAtom(dpy, "_NET_DESKTOP_NAMES", False);
	netatom[NetCloseWindow] = XInternAtom(dpy, "_NET_CLOSE_WINDOW", False);
	netatom[NetWMMoveResize] = XInternAtom(dpy, "_NET_WM_MOVERESIZE", False);
	netatom[NetFrameExtents] = XInternAtom(dpy, "_NET_FRAME_EXTENTS", False);
	netatom[NetRequestFrameExtents] = XInternAtom(dpy, "_NET_REQUEST_FRAME_EXTENTS", False);
	netatom[NetWMStateDemandsAttention] = XInternAtom(dpy, "_NET_WM_STATE_DEMANDS_ATTENTION", False);
	netatom[NetWMStateHidden] = XInternAtom(dpy, "_NET_WM_STATE_HIDDEN", False);
	netatom[NetWMWindowTypeUtility] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
	netatom[NetWMWindowTypeSplash] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_SPLASH", False);
	netatom[NetWMWindowTypeToolbar] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_TOOLBAR", False);
	netatom[NetWMWindowTypeNotification] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
	dwmatom[MotifWMHints] = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
	dwmatom[DwmClientState] = XInternAtom(dpy, "_DWM_CLIENT_STATE", False);
	dwmatom[DwmState] = XInternAtom(dpy, "_DWM_STATE", False);
	xatom[Manager] = XInternAtom(dpy, "MANAGER", False);
	xatom[Xembed] = XInternAtom(dpy, "_XEMBED", False);
	xatom[XembedInfo] = XInternAtom(dpy, "_XEMBED_INFO", False);
	/* init cursors */
	cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
	cursor[CurResize] = drw_cur_create(drw, XC_sizing);
	cursor[CurMove] = drw_cur_create(drw, XC_fleur);
	/* init appearance */
	scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
	for (i = 0; i < LENGTH(colors); i++)
		scheme[i] = drw_scm_create(drw, colors[i], 3);
	/* monitors' tags/layouts from a previous dwm (restart) */
	loadstate();
	/* init focus outline */
	outlinecreate();
	/* init system tray */
	updatesystray();
	/* init bars */
	updatebars();
	updatestatus();
	/* supporting window for NetWMCheck */
	wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
		PropModeReplace, (unsigned char *) "dwm", 3);
	/* the session scripts signal this pid (restart/exit) */
	{
		long pid = getpid();

		XChangeProperty(dpy, wmcheckwin, XInternAtom(dpy, "_NET_WM_PID", False),
			XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&pid, 1);
	}
	XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	/* EWMH support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		PropModeReplace, (unsigned char *) netatom, NetLast);
	XDeleteProperty(dpy, root, netatom[NetClientList]);
	/* EWMH desktops: one per tag of every task */
	updatedesktops();
	/* input method for the task label prompt (dead keys, UTF-8) */
	if (XSetLocaleModifiers("") && (xim = XOpenIM(dpy, NULL, NULL, NULL)))
		xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
			XNClientWindow, root, XNFocusWindow, root, NULL);
	/* select events */
	wa.cursor = cursor[CurNormal]->cursor;
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
		|ButtonPressMask|PointerMotionMask|EnterWindowMask
		|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
	XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
	XSelectInput(dpy, root, wa.event_mask);
	grabkeys();
	focus(NULL);
}

void
seturgent(Client *c, int urg)
{
	XWMHints *wmh;

	c->isurgent = urg;
	/* i3's client.urgent border, but keep the focused border on the selection. */
	if (c->frame) {
		int focused = c == selmon->sel;

		setframeborder(c, urg ? SchemeUrg : focused ? SchemeSel : SchemeNorm);
		drawtitle(c, focused);
	}
	updatenetstate(c);
	/* a client without WM_HINTS (EWMH demands-attention) gets them, so
	 * pagers that only read the ICCCM urgency see it too */
	if (!(wmh = XGetWMHints(dpy, c->win)) && !(wmh = XAllocWMHints()))
		return;
	wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
	XSetWMHints(dpy, c->win, wmh);
	XFree(wmh);
}

void
showhide(Client *c)
{
	int tabs;

	if (!c)
		return;
	tabs = curlayout(c->mon)->arrange == tabbed;
	if (ISVISIBLE(c) && (!tabs || c->isfloating || c == c->mon->tabsel)) {
		/* show clients top down */
		XMoveResizeWindow(dpy, c->frame, c->x, c->y,
			c->w - 2 * c->bw, c->h - 2 * c->bw);
		XMapWindow(dpy, c->frame);
		if (!c->mapped) {
			/* The client window stays mapped; when its frame is
			 * remapped the server may not send it an Expose, so the
			 * framebuffer could keep the previous tab/tag image.
			 * Clear and force a full repaint. */
			c->mapped = 1;
			XClearArea(dpy, c->win, 0, 0, 0, 0, True);
		}
		drawtitle(c, c == selmon->sel);
		if ((!curlayout(c->mon)->arrange || c->isfloating) && !c->isfullscreen)
			resize(c, c->x, c->y, c->w, c->h, 0);
		showhide(c->snext);
	} else {
		/* hide clients bottom up */
		showhide(c->snext);
		if (c->mapped) {
			XUnmapWindow(dpy, c->frame);
			c->mapped = 0;
		}
	}
}

void
spawn(const Arg *arg)
{
	struct sigaction sa;
	pid_t pid;

	if ((pid = fork()) == -1) {
		fprintf(stderr, "dwm: fork failed: %s\n", strerror(errno));
		return;
	}
	if (pid == 0) {
		if (dpy)
			close(ConnectionNumber(dpy));
		setsid();

		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sa.sa_handler = SIG_DFL;
		sigaction(SIGCHLD, &sa, NULL);

		execvp(((char **)arg->v)[0], (char **)arg->v);
		die("dwm: execvp '%s' failed:", ((char **)arg->v)[0]);
	}
}

void
tag(const Arg *arg)
{
	if (selmon->sel && arg->ui & TAGMASK) {
		selmon->sel->tags = arg->ui & TAGMASK;
		focus(NULL);
		arrange(selmon);
	}
}

void
tagmon(const Arg *arg)
{
	if (!selmon->sel || !mons->next)
		return;
	sendmon(selmon->sel, dirtomon(arg->i));
}

/* Place a client inside an outer rectangle (border included). This is the
 * unit the i3 gaps are measured in. */
void
place(Client *c, int x, int y, int w, int h)
{
	int bw = c->bw;

	if (w < 2 * bw + 1)
		w = 2 * bw + 1;
	if (h < 2 * bw + 1)
		h = 2 * bw + 1;
	resize(c, x, y, w, h, 0);
}

/* Place `k` tiled clients starting at c inside x,y,w,h: side by side when
 * `horiz`, stacked otherwise. Each one gets a share of the length that is
 * proportional to its cfact; the last one takes the rounding remainder so
 * the gaps stay exact. Returns the first client after them. */
static Client *
splitarea(Client *c, unsigned int k, int x, int y, int w, int h, int horiz)
{
	unsigned int i;
	float total = 0;
	Client *t;
	int pos, end, sz, avail;

	if (!k)
		return c;
	for (i = 0, t = c; t && i < k; t = nexttiled(t->next), i++)
		total += t->cfact;
	if (total <= 0)
		total = 1;
	avail = (horiz ? w : h) - (int)(k - 1) * gappx;
	pos = horiz ? x : y;
	end = horiz ? x + w : y + h;
	for (i = 0; c && i < k; c = nexttiled(c->next), i++) {
		sz = (i == k - 1) ? end - pos : (int)(avail * c->cfact / total);
		if (horiz)
			place(c, pos, y, sz, h);
		else
			place(c, x, pos, w, sz);
		pos += sz + gappx;
	}
	return c;
}

/* splith: master column left, stack right. */
void
tile(Monitor *m)
{
	unsigned int n, nm, ns;
	int half = gappx / 2, mw = 0, sx, stw;
	int wx = m->wx + gappo, wy = m->wy + gappo;
	int ww = m->ww - 2 * gappo, wh = m->wh - 2 * gappo;
	float f = MFACT(m);
	Client *c;

	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if (n == 0 || ww < 1 || wh < 1)
		return;
	nm = MIN(n, (unsigned int)NMASTER(m));
	ns = n - nm;

	if (nm && ns) {
		mw = (int)(ww * f) - half;
		sx = wx + (int)(ww * f) + half;
		stw = ww - (int)(ww * f) - half;
	} else {
		sx = wx;
		stw = ww;
		if (nm)
			mw = ww;
	}
	c = splitarea(nexttiled(m->clients), nm, wx, wy, mw, wh, 0);
	splitarea(c, ns, sx, wy, stw, wh, 0);
}

/* splitv: master row on top, stack below. */
void
tilev(Monitor *m)
{
	unsigned int n, nm, ns;
	int half = gappx / 2, mh = 0, sy, sth;
	int wx = m->wx + gappo, wy = m->wy + gappo;
	int ww = m->ww - 2 * gappo, wh = m->wh - 2 * gappo;
	float f = MFACT(m);
	Client *c;

	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if (n == 0 || ww < 1 || wh < 1)
		return;
	nm = MIN(n, (unsigned int)NMASTER(m));
	ns = n - nm;

	if (nm && ns) {
		mh = (int)(wh * f) - half;
		sy = wy + (int)(wh * f) + half;
		sth = wh - (int)(wh * f) - half;
	} else {
		sy = wy;
		sth = wh;
		if (nm)
			mh = wh;
	}
	c = splitarea(nexttiled(m->clients), nm, wx, wy, ww, mh, 1);
	splitarea(c, ns, wx, sy, ww, sth, 1);
}

void
togglebar(const Arg *arg)
{
	selmon->showbar = !selmon->showbar;
	updatebarpos(selmon);
	resizebarwin(selmon);
	if (showsystray) {
		XWindowChanges wc;
		if (!selmon->showbar)
			wc.y = -bh;
		else if (selmon->showbar) {
			wc.y = 0;
			if (!selmon->topbar)
				wc.y = selmon->mh - bh;
		}
		XConfigureWindow(dpy, systray->win, CWY, &wc);
	}
	arrange(selmon);
}

void
togglefloating(const Arg *arg)
{
	if (!selmon->sel)
		return;
	if (selmon->sel->isfullscreen) /* no support for fullscreen windows */
		return;
	selmon->sel->isfloating = !selmon->sel->isfloating || selmon->sel->isfixed;
	if (selmon->sel->isfloating) {
		if (curlayout(selmon)->arrange)
			floatnatural(selmon->sel);
		else
			resize(selmon->sel, selmon->sel->x, selmon->sel->y,
				selmon->sel->w, selmon->sel->h, 0);
	}
	arrange(selmon);
}

void
togglefullscr(const Arg *arg)
{
	if (selmon->sel)
		setfullscreen(selmon->sel, !selmon->sel->isfullscreen);
}

void
togglesplit(const Arg *arg)
{
	Arg a;

	a.v = curlayout(selmon)->arrange == tile ? &layouts[1] : &layouts[0];
	setlayout(&a);
}

void
unfocus(Client *c, int setfocus)
{
	if (!c)
		return;
	grabbuttons(c, 0);
	setframeborder(c, c->isurgent ? SchemeUrg : SchemeNorm);
	drawtitle(c, 0);
	if (setfocus) {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
}

void
unmanage(Client *c, int destroyed)
{
	Monitor *m = c->mon;
	Window frame = c->frame;

	detach(c);
	detachstack(c);
	if (m->tabsel == c)
		m->tabsel = NULL;
	if (m->sel == c)
		m->sel = NULL;
	if (!destroyed) {
		int rx, ry;
		Window child;
		XWindowAttributes wa;

		XGrabServer(dpy); /* avoid race conditions */
		XSetErrorHandler(xerrordummy);
		if (XGetWindowAttributes(dpy, c->win, &wa)
		&& XTranslateCoordinates(dpy, c->win, root, 0, 0, &rx, &ry, &child)) {
			XReparentWindow(dpy, c->win, root, rx - c->oldbw, ry - c->oldbw);
			XSetWindowBorderWidth(dpy, c->win, c->oldbw);
		}
		XRemoveFromSaveSet(dpy, c->win);
		XSelectInput(dpy, c->win, NoEventMask);
		/* EWMH: remove _NET_WM_DESKTOP when the window is withdrawn,
		 * keep it when the WM shuts down (restart restores from it). */
		if (!cleaningup) {
			XDeleteProperty(dpy, c->win, netatom[NetWMDesktop]);
			XDeleteProperty(dpy, c->win, dwmatom[DwmClientState]);
		}
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		setclientstate(c, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	if (frame)
		XDestroyWindow(dpy, frame);
	if (c->cmap)
		XFreeColormap(dpy, c->cmap);
	freeicon(c);
	free(c);
	focus(NULL);
	updateclientlist();
	arrange(m);
}

void
unmapnotify(XEvent *e)
{
	Client *c;
	XUnmapEvent *ev = &e->xunmap;

	if ((c = wintoclient(ev->window))) {
		if (ev->window != c->win)
			return; /* frame/title unmapped by showhide or fullscreen */
		/* Once reparented, the client's own unmaps are reported on its
		 * StructureNotifyMask (event == win). An unmap reported to the
		 * root comes from XReparentWindow adopting a mapped window
		 * (scan() on restart) and must not unmanage it. A time-based
		 * grace here used to keep windows that withdraw quickly as
		 * empty, focused ghost frames. */
		if (!ev->send_event && ev->event != c->win)
			return;
		if (ev->send_event)
			setclientstate(c, WithdrawnState);
		else
			unmanage(c, 0);
	}
	else if ((c = wintosystrayicon(ev->window))) {
		/* KLUDGE! sometimes icons occasionally unmap their windows, but do
		 * _not_ destroy them. We map those windows back */
		XMapRaised(dpy, c->win);
		updatesystray();
	}
}

void
updatebars(void)
{
	unsigned int w;
	Monitor *m;
	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixmap = ParentRelative,
		.event_mask = ButtonPressMask|ExposureMask
	};
	XClassHint ch = {"dwm", "dwm"};
	for (m = mons; m; m = m->next) {
		if (m->barwin)
			continue;
		w = m->ww;
		if (showsystray && systray && m == systraytomon(m) && !systrayonleft)
			w -= getsystraywidth();
		m->barwin = XCreateWindow(dpy, root, m->wx, m->by, w, bh, 0, DefaultDepth(dpy, screen),
				CopyFromParent, DefaultVisual(dpy, screen),
				CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
		XDefineCursor(dpy, m->barwin, cursor[CurNormal]->cursor);
		if (showsystray && m == systraytomon(m))
			XMapRaised(dpy, systray->win);
		XMapRaised(dpy, m->barwin);
		XSetClassHint(dpy, m->barwin, &ch);
	}
}

void
updatebarpos(Monitor *m)
{
	m->wy = m->my;
	m->wh = m->mh;
	if (m->showbar) {
		m->wh -= bh;
		m->by = m->topbar ? m->wy : m->wy + m->wh;
		m->wy = m->topbar ? m->wy + bh : m->wy;
	} else
		m->by = -bh;
}

void
updateclientlist(void)
{
	Client *c;
	Monitor *m;

	XDeleteProperty(dpy, root, netatom[NetClientList]);
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			XChangeProperty(dpy, root, netatom[NetClientList],
				XA_WINDOW, 32, PropModeAppend,
				(unsigned char *) &(c->win), 1);
}

int
updategeom(void)
{
	int dirty = 0;

#ifdef XINERAMA
	if (XineramaIsActive(dpy)) {
		int i, j, n, nn;
		Client *c;
		Monitor *m;
		XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
		XineramaScreenInfo *unique = NULL;

		for (n = 0, m = mons; m; m = m->next, n++);
		/* only consider unique geometries as separate screens */
		unique = ecalloc(nn, sizeof(XineramaScreenInfo));
		for (i = 0, j = 0; i < nn; i++)
			if (isuniquegeom(unique, j, &info[i]))
				memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
		XFree(info);
		nn = j;

		/* new monitors if nn > n */
		for (i = n; i < nn; i++) {
			for (m = mons; m && m->next; m = m->next);
			if (m)
				m->next = createmon();
			else
				mons = createmon();
		}
		for (i = 0, m = mons; i < nn && m; m = m->next, i++)
			if (i >= n
			|| unique[i].x_org != m->mx || unique[i].y_org != m->my
			|| unique[i].width != m->mw || unique[i].height != m->mh)
			{
				dirty = 1;
				m->num = i;
				m->mx = m->wx = unique[i].x_org;
				m->my = m->wy = unique[i].y_org;
				m->mw = m->ww = unique[i].width;
				m->mh = m->wh = unique[i].height;
				updatebarpos(m);
			}
		/* removed monitors if n > nn */
		for (i = nn; i < n; i++) {
			for (m = mons; m && m->next; m = m->next);
			while ((c = m->clients)) {
				dirty = 1;
				m->clients = c->next;
				detachstack(c);
				c->mon = mons;
				attach(c);
				attachstack(c);
			}
			if (m == selmon)
				selmon = mons;
			cleanupmon(m);
		}
		free(unique);
	} else
#endif /* XINERAMA */
	{ /* default monitor setup */
		if (!mons)
			mons = createmon();
		if (mons->mw != sw || mons->mh != sh) {
			dirty = 1;
			mons->mw = mons->ww = sw;
			mons->mh = mons->wh = sh;
			updatebarpos(mons);
		}
	}
	if (dirty) {
		selmon = mons;
		selmon = wintomon(root);
	}
	return dirty;
}

void
updatenumlockmask(void)
{
	unsigned int i, j;
	XModifierKeymap *modmap;

	numlockmask = 0;
	if (!(modmap = XGetModifierMapping(dpy)))
		return;
	for (i = 0; i < 8; i++)
		for (j = 0; j < modmap->max_keypermod; j++)
			if (modmap->modifiermap[i * modmap->max_keypermod + j]
				== XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
	XFreeModifiermap(modmap);
}

void
updatesizehints(Client *c)
{
	long msize;
	XSizeHints size;

	if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
		/* size is uninitialized, ensure that size.flags aren't used */
		size.flags = PSize;
	if (size.flags & PBaseSize) {
		c->basew = MAX(0, size.base_width);
		c->baseh = MAX(0, size.base_height);
	} else if (size.flags & PMinSize) {
		c->basew = MAX(0, size.min_width);
		c->baseh = MAX(0, size.min_height);
	} else
		c->basew = c->baseh = 0;
	if (size.flags & PResizeInc) {
		c->incw = MAX(0, size.width_inc);
		c->inch = MAX(0, size.height_inc);
	} else
		c->incw = c->inch = 0;
	if (size.flags & PMaxSize) {
		c->maxw = MAX(0, size.max_width);
		c->maxh = MAX(0, size.max_height);
	} else
		c->maxw = c->maxh = 0;
	if (size.flags & PMinSize) {
		c->minw = MAX(0, size.min_width);
		c->minh = MAX(0, size.min_height);
	} else if (size.flags & PBaseSize) {
		c->minw = MAX(0, size.base_width);
		c->minh = MAX(0, size.base_height);
	} else
		c->minw = c->minh = 0;
	if ((size.flags & PAspect) && size.min_aspect.x > 0 && size.max_aspect.y > 0) {
		c->mina = (float)size.min_aspect.y / size.min_aspect.x;
		c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
	} else
		c->maxa = c->mina = 0.0;
	c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
	c->hintsvalid = 1;
}

void
updatestatus(void)
{
	if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext)))
		strcpy(stext, "dwm-"VERSION);
	drawbar(selmon);
	updatesystray();
}


void
updatesystrayicongeom(Client *i, int w, int h)
{
	if (!i)
		return;
	/* polybar's tray-maxsize is 16px; dwm's applysizehints() would force
	 * the icon up to bh (20px), so scale it here without that floor. */
	i->h = systrayicon;
	if (w == h)
		i->w = systrayicon;
	else if (h > 0)
		i->w = (int)((float)systrayicon * ((float)w / (float)h));
	else
		i->w = systrayicon;
	if (i->w < 1)
		i->w = 1;
}

void
updatesystrayiconstate(Client *i, XPropertyEvent *ev)
{
	long flags;
	int code = 0;

	if (!showsystray || !i || ev->atom != xatom[XembedInfo] ||
			!(flags = getatomprop(i, xatom[XembedInfo])))
		return;

	if (flags & XEMBED_MAPPED && !i->tags) {
		i->tags = 1;
		code = XEMBED_WINDOW_ACTIVATE;
		XMapRaised(dpy, i->win);
		setclientstate(i, NormalState);
	}
	else if (!(flags & XEMBED_MAPPED) && i->tags) {
		i->tags = 0;
		code = XEMBED_WINDOW_DEACTIVATE;
		XUnmapWindow(dpy, i->win);
		setclientstate(i, WithdrawnState);
	}
	else
		return;
	sendevent(i->win, xatom[Xembed], StructureNotifyMask, CurrentTime, code, 0,
			systray->win, XEMBED_EMBEDDED_VERSION);
}

void
updatesystray(void)
{
	XSetWindowAttributes wa;
	XWindowChanges wc;
	Client *i;
	Monitor *m = systraytomon(NULL);
	unsigned int x = m->mx + m->mw;
	unsigned int stextw = TEXTW(stext) - lrpad + systrayspacing;
	unsigned int w = 1;

	if (!showsystray)
		return;
	if (systrayonleft)
		x -= stextw + lrpad / 2;
	if (!systray) {
		/* init systray */
		if (!(systray = (Systray *)calloc(1, sizeof(Systray))))
			die("fatal: could not malloc() %u bytes\n", sizeof(Systray));
		systray->win = XCreateSimpleWindow(dpy, root, x, m->by, w, bh, 0, 0, scheme[SchemeSel][ColBg].pixel);
		wa.event_mask        = ButtonPressMask | ExposureMask;
		wa.override_redirect = True;
		wa.background_pixel  = scheme[SchemeNorm][ColBg].pixel;
		XSelectInput(dpy, systray->win, SubstructureNotifyMask);
		XChangeProperty(dpy, systray->win, netatom[NetSystemTrayOrientation], XA_CARDINAL, 32,
				PropModeReplace, (unsigned char *)&netatom[NetSystemTrayOrientationHorz], 1);
		XChangeWindowAttributes(dpy, systray->win, CWEventMask|CWOverrideRedirect|CWBackPixel, &wa);
		XMapRaised(dpy, systray->win);
		XSetSelectionOwner(dpy, netatom[NetSystemTray], systray->win, CurrentTime);
		if (XGetSelectionOwner(dpy, netatom[NetSystemTray]) == systray->win) {
			sendevent(root, xatom[Manager], StructureNotifyMask, CurrentTime, netatom[NetSystemTray], systray->win, 0, 0);
			XSync(dpy, False);
		}
		else {
			fprintf(stderr, "dwm: unable to obtain system tray.\n");
			free(systray);
			systray = NULL;
			return;
		}
	}
	for (w = 0, i = systray->icons; i; i = i->next) {
		/* make sure the background color stays the same */
		wa.background_pixel  = scheme[SchemeNorm][ColBg].pixel;
		XChangeWindowAttributes(dpy, i->win, CWBackPixel, &wa);
		XMapRaised(dpy, i->win);
		w += systrayspacing;
		i->x = w;
		XMoveResizeWindow(dpy, i->win, i->x,
			i->h < bh ? (bh - i->h) / 2 : 0, i->w, i->h);
		w += i->w;
		if (i->mon != m)
			i->mon = m;
	}
	w = w ? w + systrayspacing : 1;
	x -= w;
	XMoveResizeWindow(dpy, systray->win, x, m->by, w, bh);
	wc.x = x; wc.y = m->by; wc.width = w; wc.height = bh;
	wc.stack_mode = Above; wc.sibling = m->barwin;
	XConfigureWindow(dpy, systray->win, CWX|CWY|CWWidth|CWHeight|CWSibling|CWStackMode, &wc);
	XMapWindow(dpy, systray->win);
	XMapSubwindows(dpy, systray->win);
	/* redraw background */
	XSetForeground(dpy, drw->gc, scheme[SchemeNorm][ColBg].pixel);
	XFillRectangle(dpy, systray->win, drw->gc, 0, 0, w, bh);
	XSync(dpy, False);
}

void
updatetitle(Client *c)
{
	if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
		gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
	if (c->name[0] == '\0') /* hack to mark broken clients */
		strcpy(c->name, broken);
	c->taskcap = -1;
	if (c->title)
		drawtitle(c, c == selmon->sel);
}

void
updatewindowtype(Client *c)
{
	Atom state = getatomprop(c, netatom[NetWMState]);
	Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

	/* manage() calls this before the frame exists; it applies the
	 * fullscreen state itself once the client is reparented. */
	if (c->frame && state == netatom[NetWMFullscreen])
		setfullscreen(c, 1);
	if (wtype == netatom[NetWMWindowTypeDialog]
	|| wtype == netatom[NetWMWindowTypeUtility]
	|| wtype == netatom[NetWMWindowTypeSplash]
	|| wtype == netatom[NetWMWindowTypeToolbar]
	|| wtype == netatom[NetWMWindowTypeNotification])
		c->isfloating = 1;
}

void
updatewmhints(Client *c)
{
	XWMHints *wmh;

	if ((wmh = XGetWMHints(dpy, c->win))) {
		if (c == selmon->sel && wmh->flags & XUrgencyHint) {
			wmh->flags &= ~XUrgencyHint;
			XSetWMHints(dpy, c->win, wmh);
		} else
			c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
		if (wmh->flags & InputHint)
			c->neverfocus = !wmh->input;
		else
			c->neverfocus = 0;
		XFree(wmh);
	}
}

/* ~/.dwm/tasks: one task label per line, read at startup. */
static int
taskspath(char *path, size_t size, const char *suffix)
{
	const char *home = getenv("HOME");

	if (!home || !*home)
		return 0;
	return snprintf(path, size, "%s/.dwm/tasks%s", home, suffix) < (int)size;
}

/* Copy a label, cut at a UTF-8 character boundary. */
static void
copytaskname(char *dst, const char *src)
{
	size_t n = strlen(src);

	if (n > TASKNAMELEN - 1) {
		n = TASKNAMELEN - 1;
		while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80)
			n--;
	}
	memcpy(dst, src, n);
	dst[n] = '\0';
}

void
loadtasks(void)
{
	char path[PATH_MAX], line[256];
	FILE *f;
	size_t l;
	unsigned int i;

	tasknames = ecalloc(MAXTASKS, sizeof *tasknames);
	ntasks = 0;
	if (taskspath(path, sizeof path, "") && (f = fopen(path, "r"))) {
		while (ntasks < MAXTASKS && fgets(line, sizeof line, f)) {
			l = strcspn(line, "\r\n");
			line[l] = '\0';
			if (*line)
				copytaskname(tasknames[ntasks++], line);
		}
		fclose(f);
	}
	for (i = 0; !ntasks && i < LENGTH(deftasks) && i < MAXTASKS; i++)
		copytaskname(tasknames[i], deftasks[i]);
	if (!ntasks)
		ntasks = i;
	if (!ntasks)
		copytaskname(tasknames[ntasks++], "1");
	seltask = prevtask = 0;
}

/* Written atomically so a crash never leaves a half file. */
void
savetasks(void)
{
	char path[PATH_MAX], tmp[PATH_MAX], suffix[32], *slash;
	FILE *f;
	int i, ok;

	snprintf(suffix, sizeof suffix, ".%ld", (long)getpid());
	if (!taskspath(path, sizeof path, "") || !taskspath(tmp, sizeof tmp, suffix))
		return;
	if ((slash = strrchr(path, '/'))) {
		*slash = '\0';
		mkdir(path, 0755);
		*slash = '/';
	}
	if (!(f = fopen(tmp, "w"))) {
		fprintf(stderr, "dwm: cannot write %s: %s\n", tmp, strerror(errno));
		return;
	}
	for (i = 0; i < ntasks; i++)
		fprintf(f, "%s\n", tasknames[i]);
	ok = !ferror(f);
	if (fclose(f) || !ok || rename(tmp, path) == -1) {
		fprintf(stderr, "dwm: cannot write %s: %s\n", path, strerror(errno));
		unlink(tmp);
	}
}

/* EWMH desktops: every tag of every task, desktop = task * NTAGS + tag,
 * named "<task>:<tag>". */
void
updatedesktops(void)
{
	long ndesk = ntasks * NTAGS;
	size_t size = ndesk * (TASKNAMELEN + 16), len = 0;
	char *names = ecalloc(1, size);
	int t;
	unsigned int i;

	XChangeProperty(dpy, root, netatom[NetNumberOfDesktops], XA_CARDINAL, 32,
		PropModeReplace, (unsigned char *)&ndesk, 1);
	for (t = 0; t < ntasks; t++)
		for (i = 0; i < NTAGS; i++)
			len += snprintf(names + len, size - len, "%s:%s", tasknames[t], tags[i]) + 1;
	XChangeProperty(dpy, root, netatom[NetDesktopNames],
		XInternAtom(dpy, "UTF8_STRING", False), 8,
		PropModeReplace, (unsigned char *)names, len);
	free(names);
}

/* Make task t the active one on every monitor. Each monitor keeps, per
 * task, its viewed tags and its focused window. */
void
switchtask(int t)
{
	Monitor *m;
	Client *c;
	TaskView *tv;

	if (t < 0 || t >= ntasks || t == seltask)
		return;
	if (selmon->sel)
		unfocus(selmon->sel, 0);
	for (m = mons; m; m = m->next) {
		tv = &m->taskview[seltask];
		tv->tagset[0] = m->tagset[0];
		tv->tagset[1] = m->tagset[1];
		tv->seltags = m->seltags;
		tv->sel = m->sel ? m->sel->win : 0;
	}
	prevtask = seltask;
	seltask = t;
	for (m = mons; m; m = m->next) {
		tv = &m->taskview[t];
		m->tagset[0] = tv->tagset[0];
		m->tagset[1] = tv->tagset[1];
		m->seltags = tv->seltags;
		c = tv->sel ? wintoclient(tv->sel) : NULL;
		if (!c || c->mon != m || !ISVISIBLE(c))
			for (c = m->stack; c && !ISVISIBLE(c); c = c->snext);
		if (c) {
			detachstack(c);
			attachstack(c);
		}
		m->sel = c;
		snprintf(m->ltsymbol, sizeof m->ltsymbol, "%s", curlayout(m)->symbol);
	}
	arrange(NULL);
	focus(selmon->sel);
}

void
viewtask(const Arg *arg)
{
	if (arg->i == seltask) {
		/* the active task again: back and forth, like the tags */
		if (taskbackandforth && prevtask != seltask)
			switchtask(prevtask);
		return;
	}
	switchtask(arg->i);
}

void
cycletask(const Arg *arg)
{
	switchtask(((seltask + arg->i) % ntasks + ntasks) % ntasks);
}

void
lasttask(const Arg *arg)
{
	(void)arg;
	switchtask(prevtask);
}

/* Move the focused window to another task, keeping its tags. */
void
movetotask(const Arg *arg)
{
	Client *c = selmon->sel;

	if (!c || arg->i < 0 || arg->i >= ntasks || arg->i == c->task)
		return;
	c->task = arg->i;
	focus(NULL);
	arrange(c->mon);
}

/* Open a task slot at pos: later tasks, their windows and per-task state
 * move up by one. */
void
taskinsert(int pos)
{
	Monitor *m;
	Client *c;
	unsigned int from = pos * NTAGS, cnt = (ntasks - pos) * NTAGS, i;

	memmove(tasknames + pos + 1, tasknames + pos, (ntasks - pos) * sizeof *tasknames);
	for (m = mons; m; m = m->next) {
		memmove(m->taskview + pos + 1, m->taskview + pos,
			(ntasks - pos) * sizeof *m->taskview);
		memset(&m->taskview[pos], 0, sizeof m->taskview[pos]);
		m->taskview[pos].tagset[0] = m->taskview[pos].tagset[1] = 1;
		memmove(m->lt + (from + NTAGS) * 2, m->lt + from * 2, cnt * 2 * sizeof *m->lt);
		memmove(m->sellt + from + NTAGS, m->sellt + from, cnt * sizeof *m->sellt);
		memmove(m->mfacts + from + NTAGS, m->mfacts + from, cnt * sizeof *m->mfacts);
		memmove(m->nmasters + from + NTAGS, m->nmasters + from, cnt * sizeof *m->nmasters);
		for (i = 0; i < NTAGS; i++)
			initpertag(m, from + i);
		for (c = m->clients; c; c = c->next)
			if (c->task >= pos)
				c->task++;
	}
	ntasks++;
	if (seltask >= pos)
		seltask++;
	if (prevtask >= pos)
		prevtask++;
}

/* Close the slot of task pos, which must have no windows and not be the
 * active task. */
void
taskremove(int pos)
{
	Monitor *m;
	Client *c;
	unsigned int from = pos * NTAGS, cnt = (ntasks - pos - 1) * NTAGS, i;

	memmove(tasknames + pos, tasknames + pos + 1, (ntasks - pos - 1) * sizeof *tasknames);
	for (m = mons; m; m = m->next) {
		memmove(m->taskview + pos, m->taskview + pos + 1,
			(ntasks - pos - 1) * sizeof *m->taskview);
		memmove(m->lt + from * 2, m->lt + (from + NTAGS) * 2, cnt * 2 * sizeof *m->lt);
		memmove(m->sellt + from, m->sellt + from + NTAGS, cnt * sizeof *m->sellt);
		memmove(m->mfacts + from, m->mfacts + from + NTAGS, cnt * sizeof *m->mfacts);
		memmove(m->nmasters + from, m->nmasters + from + NTAGS, cnt * sizeof *m->nmasters);
		for (i = 0; i < NTAGS; i++)
			initpertag(m, (ntasks - 1) * NTAGS + i);
		memset(&m->taskview[ntasks - 1], 0, sizeof m->taskview[ntasks - 1]);
		m->taskview[ntasks - 1].tagset[0] = m->taskview[ntasks - 1].tagset[1] = 1;
		for (c = m->clients; c; c = c->next)
			if (c->task > pos)
				c->task--;
	}
	ntasks--;
	if (seltask > pos)
		seltask--;
	if (prevtask > pos)
		prevtask--;
	else if (prevtask == pos)
		prevtask = seltask;
}

/* A new task right after the active one, named with the lowest free
 * number; the bar then asks for its label (Escape keeps the number). */
void
newtask(const Arg *arg)
{
	char name[16];
	int n, t, used, pos = seltask + 1;

	(void)arg;
	if (ntasks >= MAXTASKS)
		return;
	for (n = 1; ; n++) {
		snprintf(name, sizeof name, "%d", n);
		for (used = 0, t = 0; t < ntasks && !used; t++)
			used = !strcmp(tasknames[t], name);
		if (!used)
			break;
	}
	taskinsert(pos);
	copytaskname(tasknames[pos], name);
	savetasks();
	updatedesktops();
	switchtask(pos);
	renametask(&(Arg){ .i = pos });
}

/* Remove the active task; its windows go, with their tags, to the
 * neighbouring task, which becomes the active one. */
void
deltask(const Arg *arg)
{
	Monitor *m;
	Client *c;
	int t = seltask, dest = seltask > 0 ? seltask - 1 : 1;

	(void)arg;
	if (ntasks <= 1)
		return;
	switchtask(dest);
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (c->task == t)
				c->task = dest;
	taskremove(t);
	savetasks();
	updatedesktops();
	arrange(NULL);
	focus(NULL);
}

/* Edit a task's label in place, in its bar cell; the keyboard is grabbed
 * until Return (keep, empty = unchanged) or Escape (cancel). */
void
renametask(const Arg *arg)
{
	int t = arg->i < 0 ? seltask : arg->i, i;

	if (t >= ntasks)
		return;
	if (renaming >= 0)
		promptend(0);
	if (wmmode != ModeNormal)
		setmode(&(Arg){ .i = wmmode });
	/* the key that started the prompt may still be held: retry briefly */
	for (i = 0; i < 200; i++) {
		if (XGrabKeyboard(dpy, root, False, GrabModeAsync, GrabModeAsync,
			CurrentTime) == GrabSuccess)
			break;
		usleep(1000);
	}
	if (i == 200)
		return;
	renaming = t;
	promptbuf[0] = '\0';
	if (xic)
		XSetICFocus(xic);
	drawbars();
}

void
promptend(int commit)
{
	char *s = promptbuf, *e;

	if (renaming < 0)
		return;
	/* trim the label */
	while (*s == ' ')
		s++;
	for (e = s + strlen(s); e > s && e[-1] == ' '; e--);
	*e = '\0';
	if (commit && *s && renaming < ntasks && strcmp(tasknames[renaming], s)) {
		copytaskname(tasknames[renaming], s);
		savetasks();
		updatedesktops();
	}
	renaming = -1;
	promptbuf[0] = '\0';
	if (xic)
		XUnsetICFocus(xic);
	XUngrabKeyboard(dpy, CurrentTime);
	drawbars();
}

void
promptkey(XEvent *e)
{
	XKeyEvent *ev = &e->xkey;
	char buf[64];
	KeySym ksym = NoSymbol;
	Status status;
	size_t len, cur;
	int n;

	if (XFilterEvent(e, None))
		return;
	if (xic)
		n = Xutf8LookupString(xic, ev, buf, sizeof buf - 1, &ksym, &status);
	else
		n = XLookupString(ev, buf, sizeof buf - 1, &ksym, NULL);
	if (n < 0)
		n = 0;
	buf[n] = '\0';
	cur = strlen(promptbuf);
	if (ev->state & ControlMask) {
		if (ksym == XK_u || ksym == XK_U)
			promptbuf[0] = '\0';
		else if (ksym == XK_g || ksym == XK_bracketleft)
			promptend(0);
		else if (ksym == XK_m || ksym == XK_j)
			promptend(1);
		else
			return;
		drawbars();
		return;
	}
	switch (ksym) {
	case XK_Return:
	case XK_KP_Enter:
		promptend(1);
		return;
	case XK_Escape:
		promptend(0);
		return;
	case XK_BackSpace:
		/* drop one UTF-8 character */
		while (cur > 0 && ((unsigned char)promptbuf[--cur] & 0xC0) == 0x80);
		promptbuf[cur] = '\0';
		break;
	default:
		if (!n || (unsigned char)buf[0] < 0x20 || buf[0] == 0x7f)
			return;
		len = strlen(buf);
		if (cur + len > TASKNAMELEN - 1)
			return;
		memcpy(promptbuf + cur, buf, len + 1);
		break;
	}
	drawbars();
}

void
view(const Arg *arg)
{
	if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags]) {
		/* i3 workspace_auto_back_and_forth: selecting the current tag
		 * again goes back to the previous one, like Mod+Tab. */
		if (!autobackandforth || !(arg->ui & TAGMASK)
		|| selmon->tagset[selmon->seltags ^ 1] == selmon->tagset[selmon->seltags])
			return;
		selmon->seltags ^= 1;
		focus(NULL);
		arrange(selmon);
		return;
	}
	selmon->seltags ^= 1; /* toggle sel tagset */
	if (arg->ui & TAGMASK)
		selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
	focus(NULL);
	arrange(selmon);
}

Client *
wintoclient(Window w)
{
	Client *c;
	Monitor *m;

	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (c->win == w || c->frame == w || c->title == w)
				return c;
	return NULL;
}

Client *
wintosystrayicon(Window w) {
	Client *i = NULL;

	if (!showsystray || !w)
		return i;
	for (i = systray->icons; i && i->win != w; i = i->next) ;
	return i;
}

Monitor *
wintomon(Window w)
{
	int x, y;
	Client *c;
	Monitor *m;

	if (w == root && getrootptr(&x, &y))
		return recttomon(x, y, 1, 1);
	for (m = mons; m; m = m->next)
		if (w == m->barwin)
			return m;
	if ((c = wintoclient(w)))
		return c->mon;
	return selmon;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int
xerror(Display *d, XErrorEvent *ee)
{
	if (ee->error_code == BadWindow
	|| (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
	|| (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
	|| (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
	|| (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
	|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
	|| (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
		return 0;
	fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
		ee->request_code, ee->error_code);
	return xerrorxlib(d, ee); /* may call exit */
}

int
xerrordummy(Display *d, XErrorEvent *ee)
{
	return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display *d, XErrorEvent *ee)
{
	die("dwm: another window manager is already running");
	return -1;
}

Monitor *
systraytomon(Monitor *m) {
	Monitor *t;
	int i, n;
	if(!systraypinning) {
		if(!m)
			return selmon;
		return m == selmon ? m : NULL;
	}
	for(n = 1, t = mons; t && t->next; n++, t = t->next) ;
	for(i = 1, t = mons; t && t->next && i < systraypinning; i++, t = t->next) ;
	if(systraypinningfailfirst && n < systraypinning)
		return mons;
	return t;
}

void
zoom(const Arg *arg)
{
	Client *c = selmon->sel;

	if (!curlayout(selmon)->arrange || !c || c->isfloating)
		return;
	if (c == nexttiled(selmon->clients) && !(c = nexttiled(c->next)))
		return;
	pop(c);
}

int
main(int argc, char *argv[])
{
	if (argc == 2 && !strcmp("-v", argv[1]))
		die("dwm-"VERSION);
	else if (argc != 1)
		die("usage: dwm [-v]");
	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = XOpenDisplay(NULL)))
		die("dwm: cannot open display");
	checkotherwm();
	setup();
#ifdef __OpenBSD__
	if (pledge("stdio rpath proc exec", NULL) == -1)
		die("pledge");
#endif /* __OpenBSD__ */
	scan();
	stateready = 1;
	restorefocus();
	run();
	cleanup();
	XCloseDisplay(dpy);
	if (restartwm) {
		execvp(argv[0], argv);
		die("dwm: restart: execvp '%s' failed:", argv[0]);
	}
	return EXIT_SUCCESS;
}
