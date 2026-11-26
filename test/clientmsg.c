/* Send an EWMH client message to the root window.
 * usage: clientmsg WINDOW ATOM [l0 [l1 [l2 [l3 [l4]]]]]
 * Numeric arguments may also be atom names (for _NET_WM_STATE). */
#include <ctype.h>
#include <stdlib.h>
#include <X11/Xlib.h>

static long
arg(Display *dpy, const char *s)
{
	if (isdigit((unsigned char)s[0]) || s[0] == '-')
		return strtol(s, NULL, 0);
	return (long)XInternAtom(dpy, s, False);
}

int
main(int argc, char *argv[])
{
	Display *dpy;
	XEvent ev = { 0 };
	int i;

	if (argc < 3 || !(dpy = XOpenDisplay(NULL)))
		return 1;
	ev.xclient.type = ClientMessage;
	ev.xclient.window = strtoul(argv[1], NULL, 0);
	ev.xclient.message_type = XInternAtom(dpy, argv[2], False);
	ev.xclient.format = 32;
	for (i = 3; i < argc && i < 8; i++)
		ev.xclient.data.l[i - 3] = arg(dpy, argv[i]);
	XSendEvent(dpy, DefaultRootWindow(dpy), False,
		SubstructureRedirectMask | SubstructureNotifyMask, &ev);
	XCloseDisplay(dpy);
	return 0;
}
