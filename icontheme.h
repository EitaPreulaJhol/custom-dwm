/* See LICENSE file for copyright and license details. */

/* Icon for a window class/instance from the icon theme, as 0xAARRGGBB
 * pixels (w x h, caller frees), or NULL. Without ICONTHEME it always
 * returns NULL. */
unsigned long *iconthemelookup(const char *class, const char *instance, int *w, int *h);
