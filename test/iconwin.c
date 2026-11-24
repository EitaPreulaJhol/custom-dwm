/* Synthetic client for the real _NET_WM_ICON test.
 *
 *   iconwin NAME CLASS #RRGGBB SIZE [SECONDS ACTION]
 *
 * SIZE is the source icon size (0 = publish no icon).  A transparent-corner
 * circle is published so alpha compositing is visible.  With SECONDS and
 * ACTION=change|delete the property is replaced/deleted after that delay.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

static unsigned long
rgbvalue(const char *s)
{
	if (*s == '#')
		s++;
	return strtoul(s, NULL, 16) & 0xffffff;
}

static void
publishicon(Display *dpy, Window w, int size, unsigned long rgb)
{
	unsigned long *data;
	double cx, cy, dx, dy, d2, r2, inner;
	int x, y;

	data = malloc((2 + (size_t)size * size) * sizeof(unsigned long));
	if (!data)
		return;
	data[0] = size;
	data[1] = size;
	cx = cy = size / 2.0;
	r2 = (size / 2.0 - 1.0) * (size / 2.0 - 1.0);
	inner = (size / 2.0 - 3.0) * (size / 2.0 - 3.0);
	for (y = 0; y < size; y++) {
		for (x = 0; x < size; x++) {
			dx = x + 0.5 - cx;
			dy = y + 0.5 - cy;
			d2 = dx * dx + dy * dy;
			if (d2 > r2)
				data[2 + y * size + x] = 0; /* transparent corner */
			else if (d2 > inner)
				data[2 + y * size + x] = 0xffffffff; /* white ring */
			else
				data[2 + y * size + x] = 0xff000000 | rgb;
		}
	}
	XChangeProperty(dpy, w, XInternAtom(dpy, "_NET_WM_ICON", False),
		XA_CARDINAL, 32, PropModeReplace, (unsigned char *)data,
		2 + size * size);
	free(data);
}

int
main(int argc, char *argv[])
{
	const char *name = argc > 1 ? argv[1] : "icon test";
	const char *class = argc > 2 ? argv[2] : "IconTest";
	unsigned long rgb = argc > 3 ? rgbvalue(argv[3]) : 0xff0000;
	int size = argc > 4 ? atoi(argv[4]) : 64;
	int later = argc > 5 ? atoi(argv[5]) : 0;
	const char *action = argc > 6 ? argv[6] : "change";
	Display *dpy;
	Window w;
	XClassHint ch = { (char *)"iconwin", (char *)class };
	XEvent ev;
	time_t deadline = 0;
	int done = 0;

	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "cannot open display\n");
		return 1;
	}
	w = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 60, 80, 420, 260,
		0, BlackPixel(dpy, DefaultScreen(dpy)),
		BlackPixel(dpy, DefaultScreen(dpy)));
	XStoreName(dpy, w, name);
	XSetClassHint(dpy, w, &ch);
	if (size > 0)
		publishicon(dpy, w, size, rgb);
	XSelectInput(dpy, w, ExposureMask | StructureNotifyMask);
	XMapWindow(dpy, w);
	XFlush(dpy);
	if (later > 0)
		deadline = time(NULL) + later;

	while (1) {
		fd_set fds;
		struct timeval tv = { 1, 0 };
		int fd = ConnectionNumber(dpy);

		while (XPending(dpy)) {
			XNextEvent(dpy, &ev);
			if (ev.type == Expose) {
				XClearWindow(dpy, w);
				XFlush(dpy);
			} else if (ev.type == ClientMessage) {
				return 0;
			}
		}
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		if (select(fd + 1, &fds, NULL, NULL, &tv) < 0)
			return 1;
		if (!done && later > 0 && time(NULL) >= deadline) {
			if (!strcmp(action, "delete"))
				XDeleteProperty(dpy, w,
					XInternAtom(dpy, "_NET_WM_ICON", False));
			else
				publishicon(dpy, w, size, 0x00c000);
			XFlush(dpy);
			done = 1;
		}
	}
}
