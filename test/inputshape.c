/* Test helper: print the input and bounding shape of a window.
 * Usage: inputshape WINDOWID
 * Output: one "input N", one "bounding N", then the bounding rectangles.
 */
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char **argv)
{
	Display *dpy;
	Window window;
	XRectangle *rects;
	int count, ordering, i;
	char *end;

	if (argc != 2)
		return 1;
	if (!(dpy = XOpenDisplay(NULL)))
		return 1;
	window = (Window)strtoul(argv[1], &end, 0);
	if (*end != '\0')
		return 2;

	rects = XShapeGetRectangles(dpy, window, ShapeInput, &count, &ordering);
	printf("input %d\n", count);
	if (rects)
		XFree(rects);
	rects = XShapeGetRectangles(dpy, window, ShapeBounding, &count, &ordering);
	printf("bounding %d\n", count);
	for (i = 0; rects && i < count; i++)
		printf("rect %d %d %u %u\n", rects[i].x, rects[i].y,
			rects[i].width, rects[i].height);
	if (rects)
		XFree(rects);
	XCloseDisplay(dpy);
	return 0;
}
