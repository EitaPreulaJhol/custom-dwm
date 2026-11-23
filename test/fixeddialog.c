/* Test client that mimics the GCR/gnome-keyring prompt: a normal window with
 * no transient/type hint, mapped small, which later advertises a fixed size
 * (939x111) and resizes itself. Under a tiling WM this used to be tiled into
 * a smaller cell; the buttons at the bottom/right were clipped. */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
draw(Display *dpy, Window win, GC gc, int width, int height)
{
	XSetForeground(dpy, gc, 0xffffff);
	XFillRectangle(dpy, win, gc, 0, 0, width, height);
	XSetForeground(dpy, gc, 0x333333);
	XDrawString(dpy, win, gc, 16, 24, "Choose password for new keyring", 31);
	XDrawString(dpy, win, gc, 16, 48,
		"An application wants to create a new keyring called dwm-test.", 61);
	XSetForeground(dpy, gc, 0xdddddd);
	XFillRectangle(dpy, win, gc, 0, height - 36, width, 36);
	XSetForeground(dpy, gc, 0x000000);
	XDrawString(dpy, win, gc, width - 200, height - 14, "Cancel", 6);
	XDrawString(dpy, win, gc, width - 100, height - 14, "Continue", 8);
}

int
main(int argc, char **argv)
{
	Display *dpy;
	GC gc;
	Window win;
	XClassHint ch = { "gcr-prompter", "Gcr-prompter" };
	XSizeHints hints;
	XEvent ev;

	if (!(dpy = XOpenDisplay(NULL)))
		return 1;
	if (argc > 1)
		ch.res_class = argv[1];
	win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 10, 10, 10, 10,
		0, BlackPixel(dpy, 0), WhitePixel(dpy, 0));
	XSetClassHint(dpy, win, &ch);
	XStoreName(dpy, win, "gcr-prompter");
	XSelectInput(dpy, win, ExposureMask|StructureNotifyMask);
	gc = XCreateGC(dpy, win, 0, NULL);
	XMapWindow(dpy, win);
	XFlush(dpy);

	/* GCR maps a small helper, then sets fixed size hints and resizes. */
	usleep(400000);
	memset(&hints, 0, sizeof hints);
	hints.flags = PMinSize|PMaxSize;
	hints.min_width = hints.max_width = 939;
	hints.min_height = hints.max_height = 111;
	XSetWMNormalHints(dpy, win, &hints);
	XResizeWindow(dpy, win, 939, 111);
	XFlush(dpy);

	for (;;) {
		XNextEvent(dpy, &ev);
		if (ev.type == Expose && ev.xexpose.count == 0)
			draw(dpy, win, gc, 939, 111);
	}
}
