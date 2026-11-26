/* Focus test client: a plain window that logs what it receives.
 *
 *   focuswin NAME [options]
 *     -u MS   unmap (withdraw) itself MS milliseconds after mapping
 *     -f      ask for _NET_WM_STATE_FULLSCREEN before mapping
 *     -t WIN  WM_TRANSIENT_FOR = WIN (hex or decimal)
 *     -n      WM_HINTS input = False (a "no input" client)
 *
 * Each line on stdout is "NAME focusin|focusout|key <keysym>", flushed at
 * once, so a test can tell which client really got the keyboard. */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

int
main(int argc, char **argv)
{
	Display *dpy;
	Window win, trans = None;
	XEvent ev;
	XClassHint ch;
	XWMHints wmh;
	const char *name;
	int i, unmapms = -1, fullscreen = 0, noinput = 0, mapped = 0;
	char buf[32];
	KeySym ks;

	if (argc < 2 || !(dpy = XOpenDisplay(NULL)))
		return 1;
	name = argv[1];
	for (i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "-u") && i + 1 < argc)
			unmapms = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-f"))
			fullscreen = 1;
		else if (!strcmp(argv[i], "-n"))
			noinput = 1;
		else if (!strcmp(argv[i], "-t") && i + 1 < argc)
			trans = strtoul(argv[++i], NULL, 0);
	}
	setvbuf(stdout, NULL, _IOLBF, 0);

	win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 50, 50, 300, 200,
		0, BlackPixel(dpy, 0), 0xffffff);
	ch.res_name = (char *)name;
	ch.res_class = "FocusWin";
	XSetClassHint(dpy, win, &ch);
	XStoreName(dpy, win, name);
	wmh.flags = InputHint;
	wmh.input = !noinput;
	XSetWMHints(dpy, win, &wmh);
	if (trans)
		XSetTransientForHint(dpy, win, trans);
	if (fullscreen) {
		Atom st = XInternAtom(dpy, "_NET_WM_STATE", False);
		Atom fs = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
		XChangeProperty(dpy, win, st, XA_ATOM, 32, PropModeReplace,
			(unsigned char *)&fs, 1);
	}
	XSelectInput(dpy, win, KeyPressMask | FocusChangeMask | StructureNotifyMask);
	XMapWindow(dpy, win);
	XFlush(dpy);
	printf("%s window 0x%lx\n", name, win);

	for (;;) {
		if (unmapms >= 0 && mapped && !XPending(dpy)) {
			struct pollfd p = { ConnectionNumber(dpy), POLLIN, 0 };
			if (poll(&p, 1, unmapms) == 0) {
				XWithdrawWindow(dpy, win, DefaultScreen(dpy));
				XFlush(dpy);
				printf("%s withdrawn\n", name);
				unmapms = -1;
			}
			continue;
		}
		XNextEvent(dpy, &ev);
		switch (ev.type) {
		case MapNotify:
			mapped = 1;
			break;
		case FocusIn:
			if (ev.xfocus.detail != NotifyPointer)
				printf("%s focusin\n", name);
			break;
		case FocusOut:
			if (ev.xfocus.detail != NotifyPointer)
				printf("%s focusout\n", name);
			break;
		case KeyPress:
			XLookupString(&ev.xkey, buf, sizeof buf, &ks, NULL);
			printf("%s key %s\n", name, XKeysymToString(ks));
			break;
		}
	}
}
