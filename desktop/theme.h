/*
 * The interface's look: its colours (a palette chosen at run time) and
 * whether widgets are drawn as 9x bevels or flat.
 *
 *   Classic  the Windows 9x look the toolkit was built with (the default).
 *   Breeze   flat, in the colours of the KDE Plasma colour scheme in use
 *            (read from kdeglobals, so custom schemes carry over).
 *   Adwaita  flat, in GNOME's colours, light or dark as the system says.
 *
 * "System" picks Breeze on KDE Plasma and Adwaita elsewhere on Linux. On
 * Windows only Classic is offered. These looks are our own drawings in
 * those desktops' colours; no theme engine, artwork or icons from them are
 * used.
 */
#ifndef DXG_THEME_H
#define DXG_THEME_H

typedef enum { LOOK_CLASSIC, LOOK_BREEZE, LOOK_ADWAITA } ui_look_id;

/* What the user picked (config.ini [ui] look). */
typedef enum {
    LOOK_SET_CLASSIC,
    LOOK_SET_SYSTEM,
    LOOK_SET_BREEZE,
    LOOK_SET_ADWAITA,
    LOOK_SET_COUNT
} look_setting;

typedef struct {
    /* The 9x scheme's roles; flat looks fill them with their nearest
     * equivalents so code that only uses these still looks right. */
    unsigned face;      /* window and control background */
    unsigned light, hilight, shadow, dkshadow; /* bevel edges */
    unsigned text, graytext;
    unsigned window;    /* list, tree and edit background */
    unsigned select, seltext;
    unsigned select_inactive, seltext_inactive; /* selection without the focus */
    unsigned title_a1, title_a2, title_i1, title_i2; /* title bar gradients */
    unsigned title_text_a, title_text_i;
    unsigned workspace, desktop;
    unsigned tooltip, tooltip_text;
    /* Flat looks. */
    unsigned button, button_text;
    unsigned border;    /* frames around controls and windows */
    unsigned hover;     /* background of a hovered menu or toolbar item */
    unsigned accent;    /* focus, checked boxes, sliders, progress */
    unsigned accent_text;
    unsigned close_hover; /* the close button under the mouse */
    int dark;
} ui_palette;

extern ui_palette UI_PAL;
extern ui_look_id UI_LOOK;

#define UI_FLAT (UI_LOOK != LOOK_CLASSIC)

/* Applies a setting now (loading the desktop's colours if needed). */
void theme_set(look_setting s);
look_setting theme_setting(void);
/* Call once a frame: picks up a changed KDE colour scheme or system
 * light/dark mode. Returns 1 when the palette changed. */
int theme_poll(void);
/* The setting's name in config.ini, and back (unknown names: Classic). */
const char *theme_setting_name(look_setting s);
look_setting theme_setting_from_name(const char *name);
/* "Breeze (Sweet-Ambar-Blue, dark)" and the like, for the log and About. */
const char *theme_describe(void);

/* Mixes two colours: t = 0 gives a, 256 gives b. */
unsigned theme_mix(unsigned a, unsigned b, int t);

#endif
