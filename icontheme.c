/* See LICENSE file for copyright and license details.
 *
 * Icon theme lookup for clients without _NET_WM_ICON: a small subset of the
 * freedesktop icon theme spec. The icon name comes from the window class and
 * instance, directly or through the Icon= key of a matching .desktop file.
 * The current GTK icon theme (gtk-3.0 settings.ini) and the themes it
 * inherits are searched, then hicolor, then /usr/share/pixmaps. Only PNG
 * files are read (libpng); scalable SVG-only icons are not found.
 */
#ifdef ICONTHEME
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <png.h>

#include "icontheme.h"
#include "util.h"

#define MAXTHEMES  8
#define MAXBASES   12

static char bases[MAXBASES][1024];
static int nbases = -1;
static char themes[MAXTHEMES][128];
static int nthemes;

static const int sizes[] = { 16, 22, 24, 32, 48, 64, 96, 128, 256, 512 };
static const char *const categories[] = { "apps", "legacy" };

static void
addbase(const char *dir, const char *suffix)
{
	if (nbases >= MAXBASES || !dir || !*dir)
		return;
	snprintf(bases[nbases++], sizeof bases[0], "%s%s", dir, suffix);
}

/* XDG data directories that hold "icons" and "applications". */
static void
initbases(void)
{
	const char *home = getenv("HOME"), *data = getenv("XDG_DATA_HOME");
	const char *dirs = getenv("XDG_DATA_DIRS");
	char buf[1024], *tok, *save;

	nbases = 0;
	if (data && *data)
		addbase(data, "");
	else if (home) {
		snprintf(buf, sizeof buf, "%s/.local/share", home);
		addbase(buf, "");
	}
	snprintf(buf, sizeof buf, "%s", dirs && *dirs ? dirs : "/usr/local/share:/usr/share");
	for (tok = strtok_r(buf, ":", &save); tok; tok = strtok_r(NULL, ":", &save))
		addbase(tok, "");
}

static int
hastheme(const char *name)
{
	int i;

	for (i = 0; i < nthemes; i++)
		if (!strcmp(themes[i], name))
			return 1;
	return 0;
}

/* Value of `key=` in an ini-style file, or 0. */
static int
inivalue(const char *path, const char *key, char *out, size_t outsz)
{
	FILE *f;
	char line[1024], *v, *e;
	size_t kl = strlen(key);

	if (!(f = fopen(path, "r")))
		return 0;
	while (fgets(line, sizeof line, f)) {
		for (v = line; isspace((unsigned char)*v); v++);
		if (strncmp(v, key, kl))
			continue;
		v += kl;
		while (isspace((unsigned char)*v))
			v++;
		if (*v++ != '=')
			continue;
		while (isspace((unsigned char)*v))
			v++;
		for (e = v + strlen(v); e > v && isspace((unsigned char)e[-1]); e--);
		*e = '\0';
		if (*v == '"' && e > v + 1 && e[-1] == '"') {
			v++;
			e[-1] = '\0';
		}
		snprintf(out, outsz, "%s", v);
		fclose(f);
		return 1;
	}
	fclose(f);
	return 0;
}

static void
addtheme(const char *name, int depth)
{
	char path[1280], inherits[512], *tok, *save;
	int i;

	if (!name || !*name || depth > 4 || nthemes >= MAXTHEMES || hastheme(name))
		return;
	snprintf(themes[nthemes++], sizeof themes[0], "%s", name);
	for (i = 0; i < nbases; i++) {
		snprintf(path, sizeof path, "%s/icons/%s/index.theme", bases[i], name);
		if (inivalue(path, "Inherits", inherits, sizeof inherits)) {
			for (tok = strtok_r(inherits, ",", &save); tok;
			     tok = strtok_r(NULL, ",", &save))
				addtheme(tok, depth + 1);
			break;
		}
	}
}

static void
initthemes(void)
{
	const char *home = getenv("HOME"), *cfg = getenv("XDG_CONFIG_HOME");
	char path[1024], name[128];

	initbases();
	if (cfg && *cfg)
		snprintf(path, sizeof path, "%s/gtk-3.0/settings.ini", cfg);
	else
		snprintf(path, sizeof path, "%s/.config/gtk-3.0/settings.ini", home ? home : "");
	if (inivalue(path, "gtk-icon-theme-name", name, sizeof name))
		addtheme(name, 0);
	addtheme("hicolor", 0);
}

/* Decode a PNG into 0xAARRGGBB longs (the _NET_WM_ICON layout). */
static unsigned long *
loadpng(const char *path, int *w, int *h)
{
	png_image img;
	unsigned char *buf;
	unsigned long *argb;
	size_t i, n;

	memset(&img, 0, sizeof img);
	img.version = PNG_IMAGE_VERSION;
	if (!png_image_begin_read_from_file(&img, path))
		return NULL;
	if (!img.width || !img.height || img.width > 1024 || img.height > 1024) {
		png_image_free(&img);
		return NULL;
	}
	img.format = PNG_FORMAT_ARGB;
	buf = malloc(PNG_IMAGE_SIZE(img));
	if (!buf) {
		png_image_free(&img);
		return NULL;
	}
	if (!png_image_finish_read(&img, NULL, buf, 0, NULL)) {
		free(buf);
		return NULL;
	}
	n = (size_t)img.width * img.height;
	argb = ecalloc(n, sizeof *argb);
	for (i = 0; i < n; i++)
		argb[i] = ((unsigned long)buf[i * 4] << 24)
			| ((unsigned long)buf[i * 4 + 1] << 16)
			| ((unsigned long)buf[i * 4 + 2] << 8)
			| buf[i * 4 + 3];
	free(buf);
	*w = img.width;
	*h = img.height;
	return argb;
}

static unsigned long *
findicon(const char *name, int *w, int *h)
{
	char path[1536];
	unsigned long *p;
	unsigned int b, t, s, k;

	if (!name || !*name || strchr(name, '/'))
		return name && name[0] == '/' && strstr(name, ".png")
			? loadpng(name, w, h) : NULL;
	for (t = 0; t < (unsigned int)nthemes; t++)
		for (s = 0; s < LENGTH(sizes); s++)
			for (b = 0; b < (unsigned int)nbases; b++)
				for (k = 0; k < LENGTH(categories); k++) {
					snprintf(path, sizeof path, "%s/icons/%s/%dx%d/%s/%s.png",
						bases[b], themes[t], sizes[s], sizes[s],
						categories[k], name);
					if (!access(path, R_OK) && (p = loadpng(path, w, h)))
						return p;
				}
	for (b = 0; b < (unsigned int)nbases; b++) {
		snprintf(path, sizeof path, "%s/pixmaps/%s.png", bases[b], name);
		if (!access(path, R_OK) && (p = loadpng(path, w, h)))
			return p;
	}
	return NULL;
}

/* Icon= of <name>.desktop in the XDG applications directories. */
static int
desktopicon(const char *name, char *out, size_t outsz)
{
	char path[1536];
	int b;

	for (b = 0; b < nbases; b++) {
		snprintf(path, sizeof path, "%s/applications/%s.desktop", bases[b], name);
		if (inivalue(path, "Icon", out, outsz))
			return 1;
	}
	return 0;
}

static void
lower(const char *src, char *dst, size_t dstsz)
{
	size_t i;

	for (i = 0; src[i] && i + 1 < dstsz; i++)
		dst[i] = tolower((unsigned char)src[i]);
	dst[i] = '\0';
}

unsigned long *
iconthemelookup(const char *class, const char *instance, int *w, int *h)
{
	char names[4][128], icon[256];
	unsigned long *p;
	int i, n = 0;

	if (nbases < 0)
		initthemes();
	if (class && *class) {
		lower(class, names[n], sizeof names[0]);
		n++;
	}
	if (instance && *instance) {
		lower(instance, names[n], sizeof names[0]);
		n++;
	}
	if (class && *class)
		snprintf(names[n++], sizeof names[0], "%s", class);
	if (instance && *instance)
		snprintf(names[n++], sizeof names[0], "%s", instance);
	for (i = 0; i < n; i++)
		if (desktopicon(names[i], icon, sizeof icon)
		&& (p = findicon(icon, w, h)))
			return p;
	for (i = 0; i < n; i++)
		if ((p = findicon(names[i], w, h)))
			return p;
	return NULL;
}
#else /* ICONTHEME */
#include <stddef.h>
#include "icontheme.h"

unsigned long *
iconthemelookup(const char *class, const char *instance, int *w, int *h)
{
	return NULL;
}
#endif /* ICONTHEME */
