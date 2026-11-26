/* Test client with EWMH/Motif knobs.
 * usage: ewmhwin NAME CLASS [-motif] [-argb] [-type TYPE] [-wmdelete]
 *   -motif     _MOTIF_WM_HINTS without decorations (client-side ones)
 *   -argb      32-bit ARGB visual (half transparent red)
 *   -type T    _NET_WM_WINDOW_TYPE_T (UTILITY, SPLASH, ...)
 * The window title is NAME; it runs until killed or closed by the WM. */
#include <stdio.h>
#include <string.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

int
main(int argc, char *argv[])
{
	Display *dpy;
	Window w, root;
	XSetWindowAttributes swa;
	XVisualInfo vi;
	XClassHint ch;
	XEvent ev;
	Atom wmdelete;
	int i, motif = 0, argb = 0, depth;
	const char *type = NULL;
	Visual *visual;
	unsigned long mask = CWBackPixel | CWBorderPixel | CWEventMask;

	if (argc < 3 || !(dpy = XOpenDisplay(NULL)))
		return 1;
	for (i = 3; i < argc; i++) {
		if (!strcmp(argv[i], "-motif"))
			motif = 1;
		else if (!strcmp(argv[i], "-argb"))
			argb = 1;
		else if (!strcmp(argv[i], "-type") && i + 1 < argc)
			type = argv[++i];
	}
	root = DefaultRootWindow(dpy);
	depth = DefaultDepth(dpy, DefaultScreen(dpy));
	visual = DefaultVisual(dpy, DefaultScreen(dpy));
	swa.background_pixel = 0x3366cc;
	if (argb && XMatchVisualInfo(dpy, DefaultScreen(dpy), 32, TrueColor, &vi)) {
		depth = 32;
		visual = vi.visual;
		swa.colormap = XCreateColormap(dpy, root, visual, AllocNone);
		swa.background_pixel = 0x80800000; /* premultiplied 50% red */
		mask |= CWColormap;
	}
	swa.border_pixel = 0;
	swa.event_mask = ExposureMask | StructureNotifyMask;
	w = XCreateWindow(dpy, root, 100, 100, 300, 200, 0, depth, InputOutput,
		visual, mask, &swa);
	XStoreName(dpy, w, argv[1]);
	ch.res_name = argv[1];
	ch.res_class = argv[2];
	XSetClassHint(dpy, w, &ch);
	wmdelete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, w, &wmdelete, 1);
	if (motif) {
		long hints[5] = { 2, 0, 0, 0, 0 };
		Atom a = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
		XChangeProperty(dpy, w, a, a, 32, PropModeReplace, (unsigned char *)hints, 5);
	}
	if (type) {
		char name[128];
		Atom t;
		snprintf(name, sizeof name, "_NET_WM_WINDOW_TYPE_%s", type);
		t = XInternAtom(dpy, name, False);
		XChangeProperty(dpy, w, XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False),
			XA_ATOM, 32, PropModeReplace, (unsigned char *)&t, 1);
	}
	XMapWindow(dpy, w);
	for (;;) {
		XNextEvent(dpy, &ev);
		if (ev.type == ClientMessage && (Atom)ev.xclient.data.l[0] == wmdelete)
			break;
	}
	XCloseDisplay(dpy);
	return 0;
}
