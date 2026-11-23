/* Test helper: dock a solid 16x16 XEmbed icon into the system tray. */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	Atom opcode, selection;
	Display *dpy;
	XClientMessageEvent ev;
	XColor color, exact;
	XSetWindowAttributes wa;
	Window icon, owner, root;
	int screen;

	if (argc < 2)
		return 1;
	if (!(dpy = XOpenDisplay(NULL)))
		return 1;
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	selection = XInternAtom(dpy, "_NET_SYSTEM_TRAY_S0", False);
	owner = XGetSelectionOwner(dpy, selection);
	if (!owner)
		return 3;

	if (!XAllocNamedColor(dpy, DefaultColormap(dpy, screen), argv[1], &color, &exact))
		return 2;
	wa.background_pixel = color.pixel;
	icon = XCreateWindow(dpy, root, 0, 0, 16, 16, 0, CopyFromParent,
		InputOutput, CopyFromParent, CWBackPixel, &wa);
	/* Do not map before docking: dwm would manage it as a regular client. */

	memset(&ev, 0, sizeof ev);
	ev.type = ClientMessage;
	ev.window = owner;
	ev.message_type = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
	ev.format = 32;
	ev.data.l[0] = CurrentTime;
	ev.data.l[1] = 0; /* SYSTEM_TRAY_REQUEST_DOCK */
	ev.data.l[2] = icon;
	XSendEvent(dpy, owner, False, NoEventMask, (XEvent *)&ev);
	XSync(dpy, False);

	for (;;)
		pause();
}
