/* dwm deployed with the look and controls of the reference i3 setup.
 * See LICENSE file for copyright and license details. */

/* appearance */
static const unsigned int borderpx  = 5;        /* border pixel of windows (i3: border normal 5) */
static const unsigned int snap      = 0;        /* snap pixel (0 = free movement, like i3) */
static const unsigned int systraypinning = 0;   /* 0: sloppy systray follows selected monitor, >0: pin systray to monitor X */
static const unsigned int systrayonleft = 0;    /* 0: systray in the right corner, >0: systray on left of status text */
static const unsigned int systrayspacing = 2;   /* systray spacing (i3/polybar reference: 2px) */
static const int systraypinningfailfirst = 1;   /* 1: if pinning fails, display systray on the first monitor */
static const int showsystray        = 1;        /* 0 means no systray */
static const int systrayicon         = 16;       /* tray icon size (polybar tray-maxsize) */
static const int showbar            = 1;        /* 0 means no bar */
static const int topbar             = 1;        /* 0 means bottom bar */
static const int barheight          = 20;       /* bar height in pixels (polybar theme.ini) */
static const int titleheight        = 20;       /* per-window title bar (i3 border normal) */
static const int gappx              = 10;       /* gaps between windows (i3: gaps inner 10, outer 0) */
static const int gappo              = 0;        /* gaps around the monitor edge (i3: gaps outer 0) */
static const int floatmaxw          = 1310;     /* i3: floating_maximum_size 1310x710 */
static const int floatmaxh          = 710;
static const char *fonts[]          = { "Sans:pixelsize=12", "Symbols Nerd Font Mono:pixelsize=12" };
static const char dmenufont[]       = "Sans:pixelsize=12";
static const char col_bg[]          = "#DEDEDE"; /* bar background */
static const char col_fg[]          = "#333333"; /* bar text */
static const char col_selbg[]       = "#C5C5C5"; /* focused tag/task background */
static const char col_selfg[]       = "#000000";
static const char col_emptyfg[]     = "#A8A8A8"; /* empty tag */
static const char col_occfg[]       = "#3A3A3A"; /* occupied tag */
static const char col_taskfg[]      = "#555555"; /* unfocused task */
static const char col_iconfg[]      = "#767676"; /* task icon */
static const char col_sep[]         = "#B0B0B0"; /* separator "|" */
static const char col_accent[]      = "#FF8C00"; /* orange, 1px borders and focus outline */
static const char col_urgbg[]       = "#AAAAAA";
static const char col_unfocbw[]     = "#C5C5C5"; /* unfocused window border */
static const char col_focbw[]       = "#DEDEDE"; /* focused window border */
static const char *colors[][3]      = {
	/*                          fg          bg          border    */
	[SchemeNorm]      = { col_fg,      col_bg,     col_unfocbw },
	[SchemeSel]       = { col_selfg,   col_selbg,  col_focbw   },
	[SchemeUrg]       = { col_selfg,   col_urgbg,  col_urgbg   },
	[SchemeTagEmpty]  = { col_emptyfg, col_bg,     col_unfocbw },
	[SchemeTagOcc]    = { col_occfg,   col_bg,     col_unfocbw },
	[SchemeSeparator] = { col_sep,     col_bg,     col_bg      },
	[SchemeAccent]    = { col_accent,  col_bg,     col_accent  },
	[SchemeTask]      = { col_taskfg,  col_bg,     col_unfocbw },
	[SchemeTaskSel]   = { col_selfg,   col_selbg,  col_focbw   },
	[SchemeTaskUrg]   = { col_selfg,   col_urgbg,  col_urgbg   },
	[SchemeIcon]      = { col_iconfg,  col_bg,     col_unfocbw },
	[SchemeIconSel]   = { col_iconfg,  col_selbg,  col_focbw   },
	[SchemeIconUrg]   = { col_iconfg,  col_urgbg,  col_urgbg   },
	[SchemeDot]       = { col_selfg,   col_bg,     col_bg      },
};

/* tagging: i3 workspaces 1..10 */
static const char *tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" };

static const Rule rules[] = {
	/* xprop(1):
	 *	WM_CLASS(STRING) = instance, class
	 *	WM_NAME(STRING) = title
	 */
	/* class                   instance    title     tags mask   isfloating   monitor */
	{ "dwm-never-matches",     NULL,       NULL,     0,          0,           -1 },
	/* float only the exit confirmation dialog (xmessage -name dwm-exit) */
	{ NULL,                    "dwm-exit", NULL,     0,          1,           -1 },
};

/* layout(s) */
static const float mfact     = 0.55; /* factor of master area size [0.05..0.95] */
static const int nmaster     = 1;    /* number of clients in master area */
static const int resizehints = 0;    /* 0 = exact tiling cells (consistent gaps) */
static const int lockfullscreen = 1; /* 1 will force focus on the fullscreen window */
static const int refreshrate = 120;  /* refresh rate (per second) for client move/resize */

static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[]=",      tile },    /* splith: master left, stack right (i3 default) */
	{ "[v]",      tilev },   /* splitv: master top, stack bottom */
	{ "[S]",      monocle }, /* stacking (without i3 title bars) */
	{ "[T]",      monocle }, /* tabbed (tabs are the taskbar entries) */
	{ "><>",      NULL },    /* floating */
};

/* taskbar icons (ported from ~/.config/polybar/scripts/taskbar.json) */
typedef struct {
	const char *pattern;
	const char *glyph;
} IconRule;

static const IconRule iconrules[] = {
	{ "(alacritty|kitty|termite|wezterm|st-|urxvt|xterm|tilix|gnome-terminal|xfd4-terminal)", "\uf120" },
	{ "(brave|chrome|chromium|vivaldi|opera|edge)", "\uf268" },
	{ "(firefox|librewolf|waterfox|tor-browser|tor browser)", "\uf269" },
	{ "(code|codium|vscode|code-oss|code - oss)", "\ue70c" },
	{ "(idea|pycharm|webstorm|clion|goland|rider|phpstorm|datagrip|rubymine|android-studio|jetbrains)", "\uf121" },
	{ "(sublime|gedit|kate|mousepad|leafpad|notepad|neovim|nvim|vim|emacs)", "\uf044" },
	{ "(ida|idaso|ghidra|radare|rizin|binaryninja|binary ninja|x64dbg|hopper|objdump)", "\uf188" },
	{ "(python|idle|jupyter|ipython)", "\ue73c" },
	{ "(pcmanfm|thunar|nautilus|nemo|dolphin|caja|file-roller|file-?manager|ark|engrampa|xarchiver)", "\uf07b" },
	{ "(mpv|vlc|mplayer|celluloid|totem|smplayer|parole)", "\uf008" },
	{ "(spotify|rhythmbox|audacious|deadbeef|mpd|cmus|elisa|strawberry)", "\uf001" },
	{ "(discord|slack|telegram|whatsapp|signal|element|weechat|irssi|hexchat|thunderbird|evolution|geary|betterbird)", "\uf086" },
	{ "(obsidian|joplin|logseq|notion|zettlr|typora|qownnotes)", "\uf0217" },
	{ "(libreoffice|soffice|writer|calc|impress|onlyoffice|wps|gnumeric|abiword)", "\uf1c2" },
	{ "(gimp|krita|inkscape|eog|feh|nsxiv|sxiv|gthumb|shotwell|pix|ristretto)", "\uf03e" },
	{ "(zathura|evince|okular|mupdf|qpdfview|atril|xpdf)", "\uf1c1" },
	{ "(docker|podman|lazydocker)", "\uf308" },
	{ "(gitkraken|gitg|giggle|meld|git-cola)", "\uf1d3" },
	{ "(steam|lutris|heroic|wine|retroarch|bottles)", "\uf1b6" },
	{ "(virt-manager|qemu|virtualbox|vmware|gnome-boxes|looking-glass)", "\uf109" },
	{ "(pavucontrol|alsamixer|qpwgraph|helvum)", "\uf028" },
	{ "(blueman|bluetooth)", "\uf293" },
	{ "(nm-connection-editor|wpa_gui|network)", "\uf1eb" },
	{ "(gnome-control-center|xfce4-settings|lxappearance|arandr|systemsettings|settings)", "\uf013" },
	{ "(gparted|gnome-disks|gdu|baobab|galculator|qalculate|kcalc)", "\uf0a0" },
	{ "(obs-studio|obs|kazam|flameshot|spectacle|scrot|simplescreenrecorder)", "\uf030" },
	{ "(postman|insomnia|soapui|bruno)", "\uf0ac" },
	{ "(libreoffice-draw|draw|figma|penpot)", "\uf1fc" },
	{ "(spreadsheet|excel|scalc)", "\uf1c3" },
	{ "(zoom|teams|skype|remmina|anydesk|rustdesk|vnc|rdesktop)", "\uf03d" },
	{ "(xmessage|xclock|xeyes|xlogo|xterm)", "\uf2d0" },
};
static const char *fallbackicon = "\uf2d0";

/* key definitions */
#define MODKEY Mod4Mask
#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                       KEY,      view,           {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask,           KEY,      toggleview,     {.ui = 1 << TAG} }, \
	{ MODKEY|ShiftMask,             KEY,      tag,            {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask|ShiftMask, KEY,      toggletag,      {.ui = 1 << TAG} },

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* commands */
static char dmenumon[8]; /* component of dmenucmd, manipulated in spawn() */
static const char *dmenucmd[] = { "dmenu_run", "-m", dmenumon, "-fn", dmenufont, "-nb", col_bg, "-nf", col_fg, "-sb", col_selbg, "-sf", col_selfg, NULL };
static const char *termcmd[]  = { "st", NULL };
static const char *filecmd[]  = { "pcmanfm", NULL };
static const char *shotcmd[]  = { "flameshot", "gui", NULL };
static const char *launchercmd[] = { "i3-launcher", NULL };
static const char *restartcmd[] = { "dwm-restart", NULL };
static const char *exitcmd[]  = { "dwm-exit", NULL };
static const char *calendarcmd[] = { "/bin/sh", "-c",
	"$HOME/.config/polybar/scripts/toggle-calendar.sh", NULL };

static const Key keys[] = {
	/* modifier                     key        function        argument */
	/* i3: $mod+Return exec st */
	{ MODKEY,                       XK_Return, spawn,          {.v = termcmd } },
	/* i3: $mod+f exec pcmanfm */
	{ MODKEY,                       XK_f,      spawn,          {.v = filecmd } },
	/* i3: $mod+Shift+s exec flameshot gui */
	{ MODKEY|ShiftMask,             XK_s,      spawn,          {.v = shotcmd } },
	/* i3: $mod+q kill */
	{ MODKEY,                       XK_q,      killclient,     {0} },
	/* i3: $mod+d exec i3-launcher */
	{ MODKEY,                       XK_d,      spawn,          {.v = launchercmd } },
	/* i3: $mod+arrows focus left/down/up/right */
	{ MODKEY,                       XK_Left,   focusdir,       {.i = DirLeft } },
	{ MODKEY,                       XK_Down,   focusdir,       {.i = DirDown } },
	{ MODKEY,                       XK_Up,     focusdir,       {.i = DirUp } },
	{ MODKEY,                       XK_Right,  focusdir,       {.i = DirRight } },
	/* i3: $mod+Shift+arrows move left/down/up/right */
	{ MODKEY|ShiftMask,             XK_Left,   movedir,        {.i = DirLeft } },
	{ MODKEY|ShiftMask,             XK_Down,   movedir,        {.i = DirDown } },
	{ MODKEY|ShiftMask,             XK_Up,     movedir,        {.i = DirUp } },
	{ MODKEY|ShiftMask,             XK_Right,  movedir,        {.i = DirRight } },
	/* i3: $mod+h / $mod+v split horizontal / vertical */
	{ MODKEY,                       XK_h,      setlayout,      {.v = &layouts[0]} },
	{ MODKEY,                       XK_v,      setlayout,      {.v = &layouts[1]} },
	/* i3: $mod+Shift+f fullscreen toggle */
	{ MODKEY|ShiftMask,             XK_f,      togglefullscr,  {0} },
	/* i3: $mod+s / $mod+w / $mod+e layout stacking / tabbed / toggle split */
	{ MODKEY,                       XK_s,      setlayout,      {.v = &layouts[2]} },
	{ MODKEY,                       XK_w,      setlayout,      {.v = &layouts[3]} },
	{ MODKEY,                       XK_e,      togglesplit,    {0} },
	/* i3: $mod+space floating toggle */
	{ MODKEY,                       XK_space,  togglefloating, {0} },
	/* i3: $mod+Shift+space focus mode_toggle */
	{ MODKEY|ShiftMask,             XK_space,  focusmode,      {0} },
	/* i3: $mod+r / $mod+m binding modes */
	{ MODKEY,                       XK_r,      setmode,        {.i = ModeResize } },
	{ MODKEY,                       XK_m,      setmode,        {.i = ModeMove } },
	/* i3: $mod+Shift+c reload, $mod+Shift+r restart (dwm re-execs) */
	{ MODKEY|ShiftMask,             XK_c,      spawn,          {.v = restartcmd } },
	{ MODKEY|ShiftMask,             XK_r,      spawn,          {.v = restartcmd } },
	/* i3: $mod+Shift+e exit (confirmation dialog) */
	{ MODKEY|ShiftMask,             XK_e,      spawn,          {.v = exitcmd } },
	/* i3: $mod+1..0 and $mod+Shift+1..0 (10 tags) */
	TAGKEYS(                        XK_1,                      0)
	TAGKEYS(                        XK_2,                      1)
	TAGKEYS(                        XK_3,                      2)
	TAGKEYS(                        XK_4,                      3)
	TAGKEYS(                        XK_5,                      4)
	TAGKEYS(                        XK_6,                      5)
	TAGKEYS(                        XK_7,                      6)
	TAGKEYS(                        XK_8,                      7)
	TAGKEYS(                        XK_9,                      8)
	TAGKEYS(                        XK_0,                      9)
	/* i3: $mod+Tab workspace back_and_forth */
	{ MODKEY,                       XK_Tab,    view,           {0} },
	/* dwm extensions kept out of the way */
	{ MODKEY,                       XK_j,      focusstack,     {.i = +1 } },
	{ MODKEY,                       XK_k,      focusstack,     {.i = -1 } },
	{ MODKEY,                       XK_Return, zoom,           {0} },
	{ MODKEY,                       XK_b,      togglebar,      {0} },
	{ MODKEY,                       XK_comma,  focusmon,       {.i = -1 } },
	{ MODKEY,                       XK_period, focusmon,       {.i = +1 } },
	{ MODKEY|ShiftMask,             XK_comma,  tagmon,         {.i = -1 } },
	{ MODKEY|ShiftMask,             XK_period, tagmon,         {.i = +1 } },
	{ MODKEY|ShiftMask,             XK_q,      quit,           {0} },
};

/* button definitions */
/* click can be ClkTagBar, ClkTaskBar, ClkStatusText, ClkClientWin, or ClkRootWin */
static const Button buttons[] = {
	/* click                event mask      button          function        argument */
	{ ClkTagBar,            0,              Button1,        view,           {0} },
	{ ClkTagBar,            0,              Button3,        toggleview,     {0} },
	{ ClkTagBar,            MODKEY,         Button1,        tag,            {0} },
	{ ClkTagBar,            MODKEY,         Button3,        toggletag,      {0} },
	{ ClkTaskBar,           0,              Button1,        focustask,      {0} },
	{ ClkTaskBar,           MODKEY,         Button2,        zoom,           {0} },
	{ ClkStatusText,        0,              Button1,        spawn,          {.v = calendarcmd } },
	{ ClkTitleBar,          0,              Button1,        titleclick,     {0} },
	{ ClkTitleBar,          MODKEY,         Button1,        movemouse,      {0} },
	{ ClkTitleBar,          0,              Button2,        togglefloating, {0} },
	{ ClkTitleBar,          MODKEY,         Button3,        resizemouse,    {0} },
	{ ClkClientWin,         MODKEY,         Button1,        movemouse,      {0} },
	{ ClkClientWin,         MODKEY,         Button2,        togglefloating, {0} },
	{ ClkClientWin,         MODKEY,         Button3,        resizemouse,    {0} },
};
