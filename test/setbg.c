/* Test helper: paint a full-screen background color in Xvfb.
 *
 * `xsetroot -solid` works under Xorg but this Xvfb build keeps returning the
 * initial black root from XGetImage. A full-screen override-redirect window
 * gives the same visual result without touching dwm. */
#include <X11/Xlib.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	Display *dpy;
	XColor color, exact;
	XSetWindowAttributes wa;
	int screen;
	Window root, win;

	if (argc < 2)
		return 1;
	if (!(dpy = XOpenDisplay(NULL)))
		return 1;
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	if (!XAllocNamedColor(dpy, DefaultColormap(dpy, screen), argv[1], &color, &exact))
		return 2;

	wa.override_redirect = True;
	wa.background_pixel = color.pixel;
	win = XCreateWindow(dpy, root, 0, 0, DisplayWidth(dpy, screen),
		DisplayHeight(dpy, screen), 0, CopyFromParent, InputOutput,
		CopyFromParent, CWOverrideRedirect | CWBackPixel, &wa);
	XMapWindow(dpy, win);
	XLowerWindow(dpy, win);
	XSync(dpy, False);

	for (;;)
		pause();
}
