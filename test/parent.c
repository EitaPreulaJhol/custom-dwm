/* Test helper: print the parent window id of a window. */
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char **argv)
{
	Display *dpy;
	Window root, parent, *children = NULL;
	unsigned int n;
	char *end;

	if (argc < 2)
		return 1;
	if (!(dpy = XOpenDisplay(NULL)))
		return 1;
	Window window = (Window)strtoul(argv[1], &end, 0);
	if (*end != '\0')
		return 2;
	if (!XQueryTree(dpy, window, &root, &parent, &children, &n))
		return 3;
	if (children)
		XFree(children);
	printf("0x%lx\n", parent);
	XCloseDisplay(dpy);
	return 0;
}
