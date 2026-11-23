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
#include <locale.h>
#include <regex.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"

/* macros */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define ISVISIBLE(C)            ((C->tags & C->mon->tagset[C->mon->seltags]))
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w)
#define HEIGHT(X)               ((X)->h)
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)

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
       SchemeIcon, SchemeIconSel, SchemeIconUrg, SchemeDot }; /* color schemes */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetSystemTray, NetSystemTrayOP, NetSystemTrayOrientation, NetSystemTrayOrientationHorz,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, NetLast }; /* EWMH atoms */
enum { Manager, Xembed, XembedInfo, XLast }; /* Xembed atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClkTagBar, ClkTaskBar, ClkStatusText, ClkClientWin, ClkBorder, ClkTitleBar, ClkRootWin, ClkLast }; /* clicks */
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
	int basew, baseh, incw, inch, maxw, maxh, minw, minh, hintsvalid;
	int bw, oldbw;
	unsigned int tags;
	int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen;
	Client *next;
	Client *snext;
	Monitor *mon;
	Window win;
	Window frame;
	Window title;
	long unmap_ignore_until; /* monotonic ms, reparent race grace */
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

struct Monitor {
	char ltsymbol[16];
	float mfact;
	int nmaster;
	int num;
	int by;               /* bar geometry */
	int mx, my, mw, mh;   /* screen size */
	int wx, wy, ww, wh;   /* window area  */
	unsigned int seltags;
	unsigned int sellt;
	unsigned int tagset[2];
	int showbar;
	int topbar;
	Client *clients;
	Client *sel;
	Client *stack;
	Monitor *next;
	Window barwin;
	const Layout *lt[2];
	/* bar hit-testing caches, filled by drawbar() */
	int tagx[33];
	int statusx;
	int barw;
	int taskn;
	int taskx[33];
	Client *taskclients[32];
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
static void checkotherwm(void);
static void cleanup(void);
static void cleanupmon(Monitor *mon);
static void clientmessage(XEvent *e);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon(void);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static Monitor *dirtomon(int dir);
static void drawbar(Monitor *m);
static void drawbars(void);
static int drawmode(int x);
static int drawseparator(int x);
static int drawtag(Monitor *m, unsigned int i, int x, unsigned int occ, unsigned int urg);
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
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static int modekey(XEvent *e, KeySym keysym);
static void monocle(Monitor *m);
static void motionnotify(XEvent *e);
static void movedir(const Arg *arg);
static void movemouse(const Arg *arg);
static Client *nexttiled(Client *c);
static void place(Client *c, int x, int y, int w, int h);
static void pop(Client *c);
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
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
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setmode(const Arg *arg);
static void setup(void);
static void seturgent(Client *c, int urg);
static void showhide(Client *c);
static void spawn(const Arg *arg);
static Monitor *systraytomon(Monitor *m);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *m);
static void tilev(Monitor *m);
static void togglebar(const Arg *arg);
static void titleclick(const Arg *arg);
static void togglefullscr(const Arg *arg);
static void togglefloating(const Arg *arg);
static void togglesplit(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unfocus(Client *c, int setfocus);
static void unmanage(Client *c, int destroyed);
static void truncatechars(const char *src, char *dst, size_t dstsz, int maxchars);
static void truncatepx(const char *src, char *dst, size_t dstsz, int maxpx);
static void unmapnotify(XEvent *e);
static void updatebarpos(Monitor *m);
static void updatebars(void);
static void updateclass(Client *c);
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
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static void zoom(const Arg *arg);

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
static Atom wmatom[WMLast], netatom[NetLast], xatom[XLast];
static int running = 1;
static int wmmode = ModeNormal;
typedef struct { regex_t re; int ok; } CompiledIcon;
static CompiledIcon *iconre;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static Monitor *mons, *selmon;
static Window root, wmcheckwin;

/* configuration, allows nested code to access above variables */
#include "config.h"

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

/* function implementations */
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
	int th = c->isfullscreen ? 0 : titleheight;
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

	if (resizehints || c->isfloating || !m->lt[m->sellt]->arrange) {
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
	if (m)
		showhide(m->stack);
	else for (m = mons; m; m = m->next)
		showhide(m->stack);
	if (m) {
		arrangemon(m);
		restack(m);
	} else for (m = mons; m; m = m->next)
		arrangemon(m);
}

void
arrangemon(Monitor *m)
{
	snprintf(m->ltsymbol, sizeof(m->ltsymbol), "%s", m->lt[m->sellt]->symbol);
	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
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
	/* focus monitor if necessary */
	if ((m = wintomon(ev->window)) && m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
	}
	if (ev->window == selmon->barwin) {
		for (i = 0; i < LENGTH(tags); i++)
			if (ev->x >= selmon->tagx[i] && ev->x < selmon->tagx[i + 1]) {
				click = ClkTagBar;
				arg.ui = 1 << i;
				break;
			}
		if (i == LENGTH(tags)) {
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
		if (ev->window == c->title)
			click = ClkTitleBar;
		else if (ev->window == c->frame)
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
			buttons[i].func(click == ClkTagBar || click == ClkTaskBar
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

	view(&a);
	selmon->lt[selmon->sellt] = &foo;
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

	for (i = 0; i < CurLast; i++)
		drw_cur_free(drw, cursor[i]);
	for (i = 0; i < LENGTH(colors); i++)
		drw_scm_free(drw, scheme[i], 3);
	free(scheme);
	XDestroyWindow(dpy, wmcheckwin);
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

	if (!c)
		return;
	if (cme->message_type == netatom[NetWMState]) {
		if (cme->data.l[1] == netatom[NetWMFullscreen]
		|| cme->data.l[2] == netatom[NetWMFullscreen])
			setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
				|| (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
	} else if (cme->message_type == netatom[NetActiveWindow]) {
		if (c != selmon->sel && !c->isurgent)
			seturgent(c, 1);
	}
}

void
configure(Client *c)
{
	XConfigureEvent ce;
	int th = c->isfullscreen ? 0 : titleheight;

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
		} else if (c->isfloating || !c->mon->lt[c->mon->sellt]->arrange) {
			int th = c->isfullscreen ? 0 : titleheight;
			int cw = c->w - 2 * c->bw;
			int ch = c->h - 2 * c->bw - th;

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

	m = ecalloc(1, sizeof(Monitor));
	m->tagset[0] = m->tagset[1] = 1;
	m->mfact = mfact;
	m->nmaster = nmaster;
	m->showbar = showbar;
	m->topbar = topbar;
	m->lt[0] = &layouts[0];
	m->lt[1] = &layouts[1 % LENGTH(layouts)];
	snprintf(m->ltsymbol, sizeof(m->ltsymbol), "%s", layouts[0].symbol);
	return m;
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

/* Pixel-width truncation, like taskbar.py's truncate_px(). */
void
truncatepx(const char *src, char *dst, size_t dstsz, int maxpx)
{
	static const char ellipsis[] = "\xe2\x80\xa6";
	char candidate[320];
	size_t i = 0, best = 0;
	int ellw = textwidth(ellipsis);

	if (!dstsz)
		return;
	if (maxpx <= 0 || textwidth(src) <= maxpx) {
		snprintf(dst, dstsz, "%s", src);
		return;
	}
	while (src[i]) {
		size_t l = utf8charlen((const unsigned char *)src + i);
		if (i + l >= sizeof candidate)
			break;
		memcpy(candidate, src, i + l);
		candidate[i + l] = '\0';
		if (textwidth(candidate) + ellw > maxpx)
			break;
		best = i + l;
		i += l;
	}
	if (!best || best + strlen(ellipsis) > dstsz - 1) {
		dst[0] = '\0';
		return;
	}
	memcpy(dst, src, best);
	strcpy(dst + best, ellipsis);
}

/* Case-insensitive POSIX ERE substring search over the icon patterns. */
static const char *
iconfor(Client *c)
{
	char hay[280];
	unsigned int i;

	if (!iconre) {
		iconre = ecalloc(LENGTH(iconrules), sizeof(CompiledIcon));
		for (i = 0; i < LENGTH(iconrules); i++)
			iconre[i].ok = regcomp(&iconre[i].re, iconrules[i].pattern,
				REG_EXTENDED|REG_ICASE|REG_NOSUB) == 0;
	}
	snprintf(hay, sizeof hay, "%s %s", c->class, c->instance);
	for (i = 0; i < LENGTH(iconrules); i++)
		if (iconre[i].ok && regexec(&iconre[i].re, hay, 0, NULL, 0) == 0)
			return iconrules[i].glyph;
	return fallbackicon;
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
	int th = c->isfullscreen ? 0 : titleheight;
	int tw, pad = 8, avail, textw;
	int urgent = c->isurgent;
	unsigned int seltitle = urgent ? SchemeUrg : focused ? SchemeSel : SchemeNorm;

	if (!c->title || th <= 0)
		return;
	tw = c->w - 2 * c->bw;
	if (tw <= 0)
		return;

	truncatechars(c->name, raw, sizeof raw, 64);
	avail = tw - 2 * pad;
	if (avail < 1)
		title[0] = '\0';
	else if (textwidth(raw) > avail)
		truncatepx(raw, title, sizeof title, avail);
	else
		snprintf(title, sizeof title, "%s", raw);

	drw_setscheme(drw, scheme[seltitle]);
	drw_rect(drw, 0, 0, tw, th, 1, 1);
	textw = textwidth(title);
	if (textw)
		drw_text(drw, pad, 0, textw, th, 0, title, 0);
	drw_map(drw, c->title, 0, 0, tw, th);
}

/* Width of one taskbar item; cap is the title's pixel budget (0 = no cap). */
int
taskwidth(Client *c, int cap)
{
	char raw[320], title[320];
	const char *icon = iconfor(c);
	int w;

	truncatechars(c->name, raw, sizeof raw, 28);
	if (cap > 0)
		truncatepx(raw, title, sizeof title, cap);
	else
		snprintf(title, sizeof title, "%s", raw);

	w = textwidth(" ") + textwidth(title) + textwidth(" ");
	if (icon && *icon)
		w += textwidth(icon) + textwidth(" ");
	if (c == c->mon->sel)
		w += 2; /* 1px border on each side */
	return w;
}

void
drawtask(Monitor *m, Client *c, int *x, int cap)
{
	char raw[320], title[320];
	const char *icon = iconfor(c);
	int focused = c == m->sel;
	int urgent = c->isurgent;
	int w = taskwidth(c, cap);
	int x0 = *x, tx;
	int seltask = urgent ? SchemeTaskUrg : focused ? SchemeTaskSel : SchemeTask;
	int selicon = urgent ? SchemeIconUrg : focused ? SchemeIconSel : SchemeIcon;

	truncatechars(c->name, raw, sizeof raw, 28);
	if (cap > 0)
		truncatepx(raw, title, sizeof title, cap);
	else
		snprintf(title, sizeof title, "%s", raw);

	if (focused || urgent) {
		drw_setscheme(drw, scheme[seltask]);
		drw_rect(drw, x0, 0, w, bh, 1, 1);
	}

	tx = x0 + (focused ? 1 : 0);
	drw_setscheme(drw, scheme[seltask]);
	drw_text(drw, tx, 0, textwidth(" "), bh, 0, " ", 0);
	tx += textwidth(" ");
	if (icon && *icon) {
		drw_setscheme(drw, scheme[selicon]);
		drw_text(drw, tx, 0, textwidth(icon), bh, 0, icon, 0);
		tx += textwidth(icon);
		drw_setscheme(drw, scheme[seltask]);
		drw_text(drw, tx, 0, textwidth(" "), bh, 0, " ", 0);
		tx += textwidth(" ");
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
	Client *c, *list[32];
	int n = 0, shown, i, avail, total, sepw, cap = 0, hidden = 0;
	int minpx = 56;

	m->taskn = 0;
	for (c = m->clients; c && n < (int)LENGTH(list); c = c->next)
		if (ISVISIBLE(c))
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
		m->taskclients[i] = list[i];
		m->taskx[i] = x;
		drawtask(m, list[i], &x, cap);
		m->taskn = i + 1;
		if (i + 1 < shown || hidden) {
			drw_setscheme(drw, scheme[SchemeSeparator]);
			drw_text(drw, x, 0, sepw, bh, 0, "  |  ", 0);
			x += sepw;
		}
	}
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
	int x, tw = 0, sw = 0, barw;
	unsigned int i, occ = 0, urg = 0;
	Client *c;

	if (!m->showbar)
		return;

	if (showsystray && systray && m == systraytomon(m) && !systrayonleft)
		sw = getsystraywidth();
	resizebarwin(m);
	barw = m->ww - sw;
	if (barw < 0)
		barw = 0;
	m->barw = barw;
	m->statusx = -1;

	drw_setscheme(drw, scheme[SchemeNorm]);
	drw_rect(drw, 0, 0, barw, bh, 1, 1);

	for (c = m->clients; c; c = c->next) {
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
		XSetWindowBorder(dpy, c->frame, scheme[c->isurgent ? SchemeUrg
			: SchemeSel][ColBorder].pixel);
		setfocus(c);
	} else {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	selmon->sel = c;
	drawbars();
	if (c)
		drawtitle(c, 1);
}

void
focusdir(const Arg *arg)
{
	Client *c, *best = NULL;
	int sx, sy, bestscore = 0;

	if (!selmon->sel)
		return;
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
	if (!t || !ISVISIBLE(c))
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
	selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
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
killclient(const Arg *arg)
{
	if (!selmon->sel)
		return;

	if (!sendevent(selmon->sel->win, wmatom[WMDelete], NoEventMask, wmatom[WMDelete], CurrentTime, 0 , 0, 0)) {
		XGrabServer(dpy);
		XSetErrorHandler(xerrordummy);
		XSetCloseDownMode(dpy, DestroyAll);
		XKillClient(dpy, selmon->sel->win);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
}

static long
nowms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void
manage(Window w, XWindowAttributes *wa)
{
	Client *c, *t = NULL;
	Window trans = None;
	XSetWindowAttributes fwa, twa;
	XEvent uev;
	int th = titleheight;

	c = ecalloc(1, sizeof(Client));
	c->win = w;
	c->oldbw = wa->border_width;
	c->bw = borderpx;

	updatetitle(c);
	updateclass(c);
	if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
		c->mon = t->mon;
		c->tags = t->tags;
	} else {
		c->mon = selmon;
		applyrules(c);
	}
	updatewindowtype(c);

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

	fwa.override_redirect = True;
	fwa.background_pixel = scheme[SchemeNorm][ColBorder].pixel;
	fwa.border_pixel = scheme[SchemeNorm][ColBorder].pixel;
	fwa.event_mask = ButtonPressMask;
	c->frame = XCreateWindow(dpy, root, c->x, c->y,
		c->w - 2 * c->bw, c->h - 2 * c->bw, c->bw,
		DefaultDepth(dpy, screen), CopyFromParent, DefaultVisual(dpy, screen),
		CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWEventMask, &fwa);

	twa.background_pixel = scheme[SchemeNorm][ColBg].pixel;
	twa.event_mask = ButtonPressMask|ExposureMask;
	c->title = XCreateWindow(dpy, c->frame, 0, 0, c->w - 2 * c->bw, th, 0,
		DefaultDepth(dpy, screen), CopyFromParent, DefaultVisual(dpy, screen),
		CWBackPixel|CWEventMask, &twa);
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
	if (c->isfloating)
		XRaiseWindow(dpy, c->frame);

	attach(c);
	attachstack(c);
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
		(unsigned char *) &(c->win), 1);
	setclientstate(c, NormalState);
	if (c->mon == selmon)
		unfocus(selmon->sel, 0);
	c->mon->sel = c;

	XMapWindow(dpy, c->win);
	XMapWindow(dpy, c->title);
	/* XReparentWindow unmaps the client; consume that UnmapNotify so
	 * unmapnotify() does not unmanage a freshly reparented window. */
	XSync(dpy, False);
	while (XCheckTypedWindowEvent(dpy, c->win, UnmapNotify, &uev));
	c->unmap_ignore_until = nowms() + 750;
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
		manage(ev->window, &wa);
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
static void
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
	Client *c, *list[64];
	int n = 0, i, j;

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
	for (c = nexttiled(selmon->clients); c && n < (int)LENGTH(list); c = nexttiled(c->next))
		list[n++] = c;
	for (i = 0; i < n && list[i] != selmon->sel; i++);
	if (i == n)
		return;
	j = (arg->i == DirLeft || arg->i == DirUp) ? i - 1 : i + 1;
	if (j < 0 || j >= n)
		return;
	swapclients(selmon, list[i], list[j]);
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

void
movemouse(const Arg *arg)
{
	int x, y, ocx, ocy, nx, ny;
	Client *c;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel))
		return;
	if (c->isfullscreen) /* no support moving fullscreen windows by mouse */
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurMove]->cursor, CurrentTime) != GrabSuccess)
		return;
	if (!getrootptr(&x, &y))
		return;
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
			if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
			&& (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
				togglefloating(NULL);
			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
				resize(c, nx, ny, c->w, c->h, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
}

Client *
nexttiled(Client *c)
{
	for (; c && (c->isfloating || !ISVISIBLE(c)); c = c->next);
	return c;
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
			break;
		case XA_WM_HINTS:
			updatewmhints(c);
			drawbars();
			break;
		}
		if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
			updatetitle(c);
			drawbar(c->mon);
		}
		if (ev->atom == XA_WM_CLASS) {
			updateclass(c);
			drawtitle(c, c == selmon->sel);
			drawbar(c->mon);
		}
		if (ev->atom == netatom[NetWMWindowType])
			updatewindowtype(c);
	}
}

void
quit(const Arg *arg)
{
	running = 0;
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
	if (ii)
		*ii = i->next;
	free(i);
}

void
resize(Client *c, int x, int y, int w, int h, int interact)
{
	/* A pure move (same size) must not re-apply size hints or the floating
	 * maximum: doing so made the first mouse motion snap/resize the window.
	 * Keep only the "do not lose the window off-screen" clamp. */
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
	/* Clamp only when the size is actually being changed. */
	if (c->isfloating && !c->isfullscreen) {
		w = MIN(w, floatmaxw);
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
	XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, w, bh);
}

void
resizeclient(Client *c, int x, int y, int w, int h)
{
	XWindowChanges wc;
	int th = c->isfullscreen ? 0 : titleheight;
	int fw = MAX(1, w - 2 * c->bw);
	int fh = MAX(1, h - 2 * c->bw);
	int cw = fw;
	int ch = MAX(1, fh - th);
	int redraw = (w != c->w || h != c->h);

	c->oldx = c->x; c->x = wc.x = x;
	c->oldy = c->y; c->y = wc.y = y;
	c->oldw = c->w; c->w = wc.width = w;
	c->oldh = c->h; c->h = h;
	wc.width = fw;
	wc.height = fh;
	wc.border_width = c->bw;
	XConfigureWindow(dpy, c->frame, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	if (th > 0 && c->title) {
		XMoveResizeWindow(dpy, c->title, 0, 0, fw, th);
		if (redraw)
			drawtitle(c, c == selmon->sel);
	}
	XMoveResizeWindow(dpy, c->win, 0, th, cw, ch);
	configure(c);
	XSync(dpy, False);
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
	int x, y, ocx, ocy, ocw, och, nx, ny, nw, nh, anchor;
	int dl, dr, dt, db, near_left, near_right, near_top, near_bottom, edges;
	Client *c;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (!(c = selmon->sel))
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
	if (edges >= 2) {
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

	/* Put the pointer on the grabbed corner/edge so the first motion delta
	 * starts from the opposite fixed edge. */
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
			if (anchor & RLeft) {
				nx = ev.xmotion.x_root;
				nw = ocx + ocw - nx;
			} else if (anchor & RRight) {
				nw = ev.xmotion.x_root - ocx + 1;
			}
			if (anchor & RTop) {
				ny = ev.xmotion.y_root;
				nh = ocy + och - ny;
			} else if (anchor & RBottom) {
				nh = ev.xmotion.y_root - ocy + 1;
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
			if (nh < 2 * c->bw + titleheight + 1) {
				if (anchor & RTop)
					ny = ocy + och - (2 * c->bw + titleheight + 1);
				nh = 2 * c->bw + titleheight + 1;
			}
			if (!c->isfloating && c->mon->lt[c->mon->sellt]->arrange
			&& (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
				togglefloating(NULL);
			if (!c->mon->lt[c->mon->sellt]->arrange || c->isfloating)
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
			if (m->lt[m->sellt]->arrange) {
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
				&& (!m->lt[m->sellt]->arrange || c->isfloating)) {
					XConfigureWindow(dpy, c->frame, CWSibling|CWStackMode, &wc);
					wc.sibling = c->frame;
				}
			if (!m->lt[m->sellt]->arrange || m->sel->isfloating) {
				wc.sibling = m->barwin;
				wc.stack_mode = Below;
				XConfigureWindow(dpy, m->sel->frame, CWSibling|CWStackMode, &wc);
			}
		}
	}
	XSync(dpy, False);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
run(void)
{
	XEvent ev;
	/* main event loop */
	XSync(dpy, False);
	while (running && !XNextEvent(dpy, &ev))
		if (handler[ev.type])
			handler[ev.type](&ev); /* call handler */
}

void
scan(void)
{
	unsigned int i, num;
	Window d1, d2, *wins = NULL;
	XWindowAttributes wa;
	Monitor *m;
	Client *c;
	XEvent ev;

	if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		for (i = 0; i < num; i++) {
			if (!XGetWindowAttributes(dpy, wins[i], &wa)
			|| wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
				continue;
			/* A previous reparenting WM may leave a NormalState client
			 * unmapped when its frame was destroyed (dwm restart). Manage
			 * it so it gets a frame/title and is mapped again. */
			if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState
			|| getstate(wins[i]) == NormalState)
				manage(wins[i], &wa);
		}
		for (i = 0; i < num; i++) { /* now the transients */
			if (!XGetWindowAttributes(dpy, wins[i], &wa))
				continue;
			if (XGetTransientForHint(dpy, wins[i], &d1)
			&& (wa.map_state == IsViewable || getstate(wins[i]) == IconicState
			|| getstate(wins[i]) == NormalState))
				manage(wins[i], &wa);
		}
		if (wins)
			XFree(wins);
	}
	/* A previous reparenting WM can leave stale UnmapNotify events for
	 * clients that were just adopted; do not unmanage them on the spot. */
	XSync(dpy, False);
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			while (XCheckTypedWindowEvent(dpy, c->win, UnmapNotify, &ev));
}

void
sendmon(Client *c, Monitor *m)
{
	if (c->mon == m)
		return;
	unfocus(c, 1);
	detach(c);
	detachstack(c);
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
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
		c->isfullscreen = 1;
		c->oldstate = c->isfloating;
		c->oldbw = c->bw;
		c->bw = 0;
		c->isfloating = 1;
		XSetWindowBorderWidth(dpy, c->frame, 0);
		if (c->title)
			XUnmapWindow(dpy, c->title);
		resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
		XRaiseWindow(dpy, c->frame);
	} else if (!fullscreen && c->isfullscreen) {
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)0, 0);
		c->isfullscreen = 0;
		c->isfloating = c->oldstate;
		c->bw = c->oldbw;
		XSetWindowBorderWidth(dpy, c->frame, c->bw);
		if (c->title)
			XMapWindow(dpy, c->title);
		c->x = c->oldx;
		c->y = c->oldy;
		c->w = c->oldw;
		c->h = c->oldh;
		resizeclient(c, c->x, c->y, c->w, c->h);
		drawtitle(c, c == selmon->sel);
		arrange(c->mon);
	}
}

void
setlayout(const Arg *arg)
{
	if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
		selmon->sellt ^= 1;
	if (arg && arg->v)
		selmon->lt[selmon->sellt] = (Layout *)arg->v;
	snprintf(selmon->ltsymbol, sizeof(selmon->ltsymbol), "%s", selmon->lt[selmon->sellt]->symbol);
	if (selmon->sel)
		arrange(selmon);
	else
		drawbar(selmon);
}

/* arg > 1.0 will set mfact absolutely */
void
setmfact(const Arg *arg)
{
	float f;

	if (!arg || !selmon->lt[selmon->sellt]->arrange)
		return;
	f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
	if (f < 0.05 || f > 0.95)
		return;
	selmon->mfact = f;
	arrange(selmon);
}

void
setmode(const Arg *arg)
{
	wmmode = (wmmode == arg->i) ? ModeNormal : arg->i;
	drawbars();
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
		drawbars();
		return 1;
	}
	if (!c)
		return 1;

	if (wmmode == ModeResize) {
		if (c->isfullscreen)
			return 1;
		if (c->isfloating) {
			int dx = 0, dy = 0;

			switch (keysym) {
			case XK_Left:  case XK_j: dx = -10; break;
			case XK_Right: case XK_semicolon: dx = 10; break;
			case XK_Up:    case XK_l: dy = -10; break;
			case XK_Down:  case XK_k: dy = 10; break;
			default: return 1;
			}
			resize(c, c->x, c->y, MAX(1, c->w + dx), MAX(1, c->h + dy), 1);
		} else {
			switch (keysym) {
			case XK_Left:  case XK_j: a.f = -0.02f; setmfact(&a); break;
			case XK_Right: case XK_semicolon: a.f = 0.02f; setmfact(&a); break;
			case XK_Up:    case XK_l: a.i = -1; incnmaster(&a); break;
			case XK_Down:  case XK_k: a.i = 1;  incnmaster(&a); break;
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
		if (c->isfloating) {
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
	XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	/* EWMH support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		PropModeReplace, (unsigned char *) netatom, NetLast);
	XDeleteProperty(dpy, root, netatom[NetClientList]);
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

		XSetWindowBorder(dpy, c->frame, scheme[urg ? SchemeUrg
			: focused ? SchemeSel : SchemeNorm][ColBorder].pixel);
		drawtitle(c, focused);
	}
	if (!(wmh = XGetWMHints(dpy, c->win)))
		return;
	wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
	XSetWMHints(dpy, c->win, wmh);
	XFree(wmh);
}

void
showhide(Client *c)
{
	if (!c)
		return;
	if (ISVISIBLE(c)) {
		/* show clients top down */
		XMoveResizeWindow(dpy, c->frame, c->x, c->y,
			c->w - 2 * c->bw, c->h - 2 * c->bw);
		XMapWindow(dpy, c->frame);
		drawtitle(c, c == selmon->sel);
		if ((!c->mon->lt[c->mon->sellt]->arrange || c->isfloating) && !c->isfullscreen)
			resize(c, c->x, c->y, c->w, c->h, 0);
		showhide(c->snext);
	} else {
		/* hide clients bottom up */
		showhide(c->snext);
		XUnmapWindow(dpy, c->frame);
	}
}

void
spawn(const Arg *arg)
{
	struct sigaction sa;
	pid_t pid;

	if (arg->v == dmenucmd)
		snprintf(dmenumon, sizeof(dmenumon), "%d", selmon->num);

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

/* splith: master column left, stack right. gaps inner 10px, outer 0. */
void
tile(Monitor *m)
{
	unsigned int i, n, nm, ns;
	int g = gappx, half = g / 2, mw = 0, sx, sw, y, h, avail;
	int wx = m->wx + gappo, wy = m->wy + gappo;
	int ww = m->ww - 2 * gappo, wh = m->wh - 2 * gappo;
	Client *c;

	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if (n == 0 || ww < 1 || wh < 1)
		return;
	nm = MIN(n, (unsigned int)m->nmaster);
	ns = n - nm;

	if (nm && ns) {
		mw = (int)(ww * m->mfact) - half;
		sx = wx + (int)(ww * m->mfact) + half;
		sw = ww - (int)(ww * m->mfact) - half;
	} else {
		sx = wx;
		sw = ww;
		if (nm)
			mw = ww;
	}

	i = 0;
	y = wy;
	avail = wh - (nm > 1 ? (int)(nm - 1) * g : 0);
	for (c = nexttiled(m->clients); c && i < nm; c = nexttiled(c->next), i++) {
		h = (i == nm - 1) ? (wy + wh - y) : avail / (int)nm;
		place(c, wx, y, mw, h);
		y += h + g;
	}

	i = 0;
	y = wy;
	avail = wh - (ns > 1 ? (int)(ns - 1) * g : 0);
	for (; c; c = nexttiled(c->next), i++) {
		h = (i == ns - 1) ? (wy + wh - y) : avail / (int)ns;
		place(c, sx, y, sw, h);
		y += h + g;
	}
}

/* splitv: master row on top, stack below. */
void
tilev(Monitor *m)
{
	unsigned int i, n, nm, ns;
	int g = gappx, half = g / 2, mh = 0, sy, sh, x, w, avail;
	int wx = m->wx + gappo, wy = m->wy + gappo;
	int ww = m->ww - 2 * gappo, wh = m->wh - 2 * gappo;
	Client *c;

	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if (n == 0 || ww < 1 || wh < 1)
		return;
	nm = MIN(n, (unsigned int)m->nmaster);
	ns = n - nm;

	if (nm && ns) {
		mh = (int)(wh * m->mfact) - half;
		sy = wy + (int)(wh * m->mfact) + half;
		sh = wh - (int)(wh * m->mfact) - half;
	} else {
		sy = wy;
		sh = wh;
		if (nm)
			mh = wh;
	}

	i = 0;
	x = wx;
	avail = ww - (nm > 1 ? (int)(nm - 1) * g : 0);
	for (c = nexttiled(m->clients); c && i < nm; c = nexttiled(c->next), i++) {
		w = (i == nm - 1) ? (wx + ww - x) : avail / (int)nm;
		place(c, x, wy, w, mh);
		x += w + g;
	}

	i = 0;
	x = wx;
	avail = ww - (ns > 1 ? (int)(ns - 1) * g : 0);
	for (; c; c = nexttiled(c->next), i++) {
		w = (i == ns - 1) ? (wx + ww - x) : avail / (int)ns;
		place(c, x, sy, w, sh);
		x += w + g;
	}
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
	if (selmon->sel->isfloating)
		resize(selmon->sel, selmon->sel->x, selmon->sel->y,
			selmon->sel->w, selmon->sel->h, 0);
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

	a.v = selmon->lt[selmon->sellt]->arrange == tile ? &layouts[1] : &layouts[0];
	setlayout(&a);
}

void
toggletag(const Arg *arg)
{
	unsigned int newtags;

	if (!selmon->sel)
		return;
	newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
	if (newtags) {
		selmon->sel->tags = newtags;
		focus(NULL);
		arrange(selmon);
	}
}

void
toggleview(const Arg *arg)
{
	unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

	if (newtagset) {
		selmon->tagset[selmon->seltags] = newtagset;
		focus(NULL);
		arrange(selmon);
	}
}

void
unfocus(Client *c, int setfocus)
{
	if (!c)
		return;
	grabbuttons(c, 0);
	XSetWindowBorder(dpy, c->frame, scheme[c->isurgent ? SchemeUrg
		: SchemeNorm][ColBorder].pixel);
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
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		setclientstate(c, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	if (frame)
		XDestroyWindow(dpy, frame);
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
		/* XReparentWindow's UnmapNotify can arrive after manage()'s
		 * consume loop (especially after a dwm restart); ignore it. */
		if (c->unmap_ignore_until && nowms() < c->unmap_ignore_until)
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
	unsigned int sw = TEXTW(stext) - lrpad + systrayspacing;
	unsigned int w = 1;

	if (!showsystray)
		return;
	if (systrayonleft)
		x -= sw + lrpad / 2;
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
	if (c->title)
		drawtitle(c, c == selmon->sel);
}

void
updatewindowtype(Client *c)
{
	Atom state = getatomprop(c, netatom[NetWMState]);
	Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

	if (state == netatom[NetWMFullscreen])
		setfullscreen(c, 1);
	if (wtype == netatom[NetWMWindowTypeDialog])
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

void
view(const Arg *arg)
{
	if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
		return;
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
xerror(Display *dpy, XErrorEvent *ee)
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
	return xerrorxlib(dpy, ee); /* may call exit */
}

int
xerrordummy(Display *dpy, XErrorEvent *ee)
{
	return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display *dpy, XErrorEvent *ee)
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

	if (!selmon->lt[selmon->sellt]->arrange || !c || c->isfloating)
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
	run();
	cleanup();
	XCloseDisplay(dpy);
	return EXIT_SUCCESS;
}
