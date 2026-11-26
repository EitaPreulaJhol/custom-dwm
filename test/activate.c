/* Send only _NET_ACTIVE_WINDOW for a window, like an application asking
 * for attention (source indication 1). Unlike `xdotool windowactivate` it
 * never switches the desktop first.
 * usage: activate WINDOW_ID */
#include <stdlib.h>
#include <X11/Xlib.h>

int
main(int argc, char *argv[])
{
	Display *dpy;
	XEvent ev = { 0 };

	if (argc != 2 || !(dpy = XOpenDisplay(NULL)))
		return 1;
	ev.xclient.type = ClientMessage;
	ev.xclient.window = strtoul(argv[1], NULL, 0);
	ev.xclient.message_type = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = 1;
	ev.xclient.data.l[1] = CurrentTime;
	XSendEvent(dpy, DefaultRootWindow(dpy), False,
		SubstructureRedirectMask | SubstructureNotifyMask, &ev);
	XCloseDisplay(dpy);
	return 0;
}
