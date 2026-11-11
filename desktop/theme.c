/* The interface's look and colours: see theme.h. */
#include "theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "draw.h"
#include "log.h"

ui_palette UI_PAL;
ui_look_id UI_LOOK = LOOK_CLASSIC;

static look_setting setting = LOOK_SET_CLASSIC;
static char described[160];
/* kdeglobals and Plasma 6's defaults layer under it, as KConfig reads
 * them: the first file that has a key wins. */
#define KDE_FILES 2
static SDL_Time kde_mtime[KDE_FILES];
static int kde_mtime_known[KDE_FILES];
static SDL_SystemTheme sys_theme = SDL_SYSTEM_THEME_UNKNOWN;
static uint64_t next_poll;

unsigned theme_mix(unsigned a, unsigned b, int t)
{
    unsigned r = 0;
    int sh;
    for (sh = 0; sh < 24; sh += 8) {
        int ca = (int)((a >> sh) & 0xFF), cb = (int)((b >> sh) & 0xFF);
        r |= (unsigned)((ca + (cb - ca) * t / 256) & 0xFF) << sh;
    }
    return r | 0xFF000000u;
}

static int luma(unsigned c)
{
    return (int)(((c & 0xFF) * 299 + ((c >> 8) & 0xFF) * 587 + ((c >> 16) & 0xFF) * 114) / 1000);
}

/* ---- Classic: the standard 9x scheme ---- */

static void classic(ui_palette *p)
{
    memset(p, 0, sizeof(*p));
    p->face = COL(0xC0, 0xC0, 0xC0);
    p->light = COL(0xDF, 0xDF, 0xDF);
    p->hilight = COL(0xFF, 0xFF, 0xFF);
    p->shadow = COL(0x80, 0x80, 0x80);
    p->dkshadow = COL(0x00, 0x00, 0x00);
    p->text = COL(0x00, 0x00, 0x00);
    p->graytext = COL(0x80, 0x80, 0x80);
    p->window = COL(0xFF, 0xFF, 0xFF);
    p->select = COL(0x00, 0x00, 0x80);
    p->seltext = COL(0xFF, 0xFF, 0xFF);
    p->select_inactive = p->face;
    p->seltext_inactive = p->text;
    p->title_a1 = COL(0x00, 0x00, 0x80);
    p->title_a2 = COL(0x10, 0x84, 0xD0);
    p->title_i1 = COL(0x80, 0x80, 0x80);
    p->title_i2 = COL(0xC0, 0xC0, 0xC0);
    p->title_text_a = p->hilight;
    p->title_text_i = p->light;
    p->workspace = COL(0x80, 0x80, 0x80);
    p->desktop = COL(0x00, 0x80, 0x80);
    p->tooltip = COL(0xFF, 0xFF, 0xE1);
    p->tooltip_text = p->text;
    p->button = p->face;
    p->button_text = p->text;
    p->border = p->dkshadow;
    p->hover = p->select;
    p->accent = p->select;
    p->accent_text = p->seltext;
    p->close_hover = p->face;
}

/* ---- flat looks: the 9x roles from a handful of desktop colours ---- */

typedef struct {
    unsigned window_bg, window_fg, inactive_fg;
    unsigned view_bg, view_fg;
    unsigned button_bg, button_fg;
    unsigned select_bg, select_fg;
    unsigned accent, hover_deco;
    unsigned header_bg, header_fg, header_inactive_bg, header_inactive_fg;
    unsigned tooltip_bg, tooltip_fg;
    unsigned negative;
    int border_t; /* how far the border sits from the background to the text, /256 */
} desk_colours;

static void flat_from(ui_palette *p, const desk_colours *c)
{
    memset(p, 0, sizeof(*p));
    p->dark = luma(c->window_bg) < 128;
    p->face = c->window_bg;
    p->text = c->window_fg;
    p->graytext = c->inactive_fg;
    p->window = c->view_bg;
    p->select = c->select_bg;
    p->seltext = c->select_fg;
    p->select_inactive = theme_mix(c->view_bg, c->select_bg, 90);
    p->seltext_inactive = c->view_fg;
    p->border = theme_mix(c->window_bg, c->window_fg, c->border_t);
    /* Bevel roles, for code that still draws 9x edges: all the frame
     * colour, so they come out as plain lines. */
    p->light = p->hilight = p->face;
    p->shadow = p->border;
    p->dkshadow = p->border;
    p->title_a1 = p->title_a2 = c->header_bg;
    p->title_i1 = p->title_i2 = c->header_inactive_bg;
    p->title_text_a = c->header_fg;
    p->title_text_i = c->header_inactive_fg;
    p->workspace = theme_mix(c->window_bg, c->window_fg, p->dark ? 20 : 28);
    p->desktop = p->workspace;
    p->tooltip = c->tooltip_bg;
    p->tooltip_text = c->tooltip_fg;
    p->button = c->button_bg;
    p->button_text = c->button_fg;
    p->accent = c->accent;
    p->accent_text = c->select_fg;
    p->hover = theme_mix(c->window_bg, c->hover_deco, p->dark ? 80 : 64);
    p->close_hover = c->negative;
}

/* Breeze and Breeze Dark as shipped with Plasma 6, for keys a scheme
 * leaves out. */
static void breeze_defaults(desk_colours *c, int dark)
{
    if (!dark) {
        c->window_bg = COL(239, 240, 241);
        c->window_fg = COL(35, 38, 41);
        c->inactive_fg = COL(112, 125, 138);
        c->view_bg = COL(255, 255, 255);
        c->view_fg = COL(35, 38, 41);
        c->button_bg = COL(252, 252, 252);
        c->button_fg = COL(35, 38, 41);
        c->header_bg = COL(222, 224, 226);
        c->header_fg = COL(35, 38, 41);
        c->header_inactive_bg = COL(239, 240, 241);
        c->header_inactive_fg = COL(112, 125, 138);
        c->tooltip_bg = COL(247, 247, 247);
        c->tooltip_fg = COL(35, 38, 41);
    } else {
        c->window_bg = COL(32, 35, 38);
        c->window_fg = COL(252, 252, 252);
        c->inactive_fg = COL(161, 169, 177);
        c->view_bg = COL(20, 22, 24);
        c->view_fg = COL(252, 252, 252);
        c->button_bg = COL(41, 44, 48);
        c->button_fg = COL(252, 252, 252);
        c->header_bg = COL(41, 44, 48);
        c->header_fg = COL(252, 252, 252);
        c->header_inactive_bg = COL(32, 35, 38);
        c->header_inactive_fg = COL(161, 169, 177);
        c->tooltip_bg = COL(41, 44, 48);
        c->tooltip_fg = COL(252, 252, 252);
    }
    c->select_bg = COL(61, 174, 233);
    c->select_fg = COL(255, 255, 255);
    c->accent = COL(61, 174, 233);
    c->hover_deco = COL(61, 174, 233);
    c->negative = COL(218, 68, 83);
    c->border_t = dark ? 56 : 64;
}

/* ---- kdeglobals ---- */

#ifndef _WIN32
static int kdeglobals_path(int i, char *out, size_t n)
{
    const char *xdg = SDL_getenv("XDG_CONFIG_HOME"), *home = SDL_getenv("HOME");
    const char *leaf = i == 0 ? "kdeglobals" : "kdedefaults/kdeglobals";
    if (xdg && xdg[0])
        snprintf(out, n, "%s/%s", xdg, leaf);
    else if (home && home[0])
        snprintf(out, n, "%s/.config/%s", home, leaf);
    else
        return 0;
    return 1;
}
#endif

/* The value of key in [group], or NULL. Plain INI as KConfig writes it:
 * "[Colors:Window]" then "BackgroundNormal=239,240,241". */
static const char *ini_get(const char *text, const char *group, const char *key, char *out,
                           size_t n)
{
    const char *p = text;
    int in_group = 0;
    size_t gl = strlen(group), kl = strlen(key);
    while (p && *p) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len && p[len - 1] == '\r')
            len--;
        if (len >= 2 && p[0] == '[') {
            in_group = len == gl + 2 && strncmp(p + 1, group, gl) == 0 && p[gl + 1] == ']';
        } else if (in_group && len > kl && strncmp(p, key, kl) == 0) {
            const char *v = p + kl;
            while (v < p + len && (*v == ' ' || *v == '\t'))
                v++;
            if (v < p + len && *v == '=') {
                size_t vl;
                v++;
                while (v < p + len && (*v == ' ' || *v == '\t'))
                    v++;
                vl = (size_t)(p + len - v);
                if (vl >= n)
                    vl = n - 1;
                memcpy(out, v, vl);
                out[vl] = '\0';
                return out;
            }
        }
        p = e ? e + 1 : NULL;
    }
    return NULL;
}

static char *kde_text[KDE_FILES];

static const char *kde_get(const char *group, const char *key, char *out, size_t n)
{
    int i;
    for (i = 0; i < KDE_FILES; i++)
        if (kde_text[i] && ini_get(kde_text[i], group, key, out, n))
            return out;
    return NULL;
}

static void kde_colour(const char *group, const char *key, unsigned *col)
{
    char v[64];
    int r, g, b;
    if (kde_get(group, key, v, sizeof(v)) && sscanf(v, "%d,%d,%d", &r, &g, &b) == 3 &&
        r >= 0 && r < 256 && g >= 0 && g < 256 && b >= 0 && b < 256)
        *col = COL(r, g, b);
}

/* Fills c from the KDE colour scheme in use; returns 1 when kdeglobals had
 * colours. `scheme` gets the scheme's name. */
static int breeze_from_kde(desk_colours *c, char *scheme, size_t n)
{
    int got = 0;
    scheme[0] = '\0';
#ifndef _WIN32
    {
        char path[1100];
        int i;
        unsigned probe = 0;
        for (i = 0; i < KDE_FILES; i++) {
            SDL_PathInfo info;
            size_t len;
            kde_text[i] = NULL;
            kde_mtime_known[i] = 0;
            if (!kdeglobals_path(i, path, sizeof(path)))
                continue;
            if (SDL_GetPathInfo(path, &info)) {
                kde_mtime[i] = info.modify_time;
                kde_mtime_known[i] = 1;
                kde_text[i] = (char *)SDL_LoadFile(path, &len);
            }
        }
        kde_colour("Colors:Window", "BackgroundNormal", &probe);
        if (probe) {
            /* Start from Breeze or Breeze Dark, whichever is nearer, then
             * take every colour the scheme sets. */
            breeze_defaults(c, luma(probe) < 128);
            got = 1;
            kde_colour("Colors:Window", "BackgroundNormal", &c->window_bg);
            kde_colour("Colors:Window", "ForegroundNormal", &c->window_fg);
            kde_colour("Colors:Window", "ForegroundInactive", &c->inactive_fg);
            kde_colour("Colors:View", "BackgroundNormal", &c->view_bg);
            kde_colour("Colors:View", "ForegroundNormal", &c->view_fg);
            kde_colour("Colors:Button", "BackgroundNormal", &c->button_bg);
            kde_colour("Colors:Button", "ForegroundNormal", &c->button_fg);
            kde_colour("Colors:Button", "DecorationFocus", &c->accent);
            kde_colour("Colors:Button", "DecorationHover", &c->hover_deco);
            kde_colour("Colors:Selection", "BackgroundNormal", &c->select_bg);
            kde_colour("Colors:Selection", "ForegroundNormal", &c->select_fg);
            kde_colour("Colors:Tooltip", "BackgroundNormal", &c->tooltip_bg);
            kde_colour("Colors:Tooltip", "ForegroundNormal", &c->tooltip_fg);
            kde_colour("Colors:View", "ForegroundNegative", &c->negative);
            /* Title bars: the window manager's colours, as KWin draws
             * them; Plasma 6's Header set, if the scheme has one, is what
             * Breeze uses for toolbars and title bars together. */
            c->header_bg = c->window_bg;
            c->header_fg = c->window_fg;
            c->header_inactive_bg = c->window_bg;
            c->header_inactive_fg = c->inactive_fg;
            kde_colour("WM", "activeBackground", &c->header_bg);
            kde_colour("WM", "activeForeground", &c->header_fg);
            kde_colour("WM", "inactiveBackground", &c->header_inactive_bg);
            kde_colour("WM", "inactiveForeground", &c->header_inactive_fg);
            kde_colour("Colors:Header", "BackgroundNormal", &c->header_bg);
            kde_colour("Colors:Header", "ForegroundNormal", &c->header_fg);
            kde_colour("Colors:Header][Inactive", "BackgroundNormal", &c->header_inactive_bg);
            kde_colour("Colors:Header][Inactive", "ForegroundNormal", &c->header_inactive_fg);
            if (!kde_get("General", "ColorScheme", scheme, n))
                snprintf(scheme, n, "custom");
        }
        for (i = 0; i < KDE_FILES; i++) {
            SDL_free(kde_text[i]);
            kde_text[i] = NULL;
        }
    }
#else
    (void)c;
    (void)n;
#endif
    return got;
}

/* ---- Adwaita (libadwaita's named colours, solid) ---- */

static void adwaita(desk_colours *c, int dark)
{
    if (!dark) {
        c->window_bg = COL(0xFA, 0xFA, 0xFB);
        c->window_fg = COL(0x32, 0x32, 0x36);
        c->inactive_fg = COL(0x7E, 0x7E, 0x82);
        c->view_bg = COL(0xFF, 0xFF, 0xFF);
        c->view_fg = COL(0x32, 0x32, 0x36);
        c->button_bg = COL(0xEB, 0xEB, 0xED);
        c->button_fg = COL(0x32, 0x32, 0x36);
        c->header_bg = COL(0xEB, 0xEB, 0xED);
        c->header_fg = COL(0x32, 0x32, 0x36);
        c->header_inactive_bg = COL(0xF6, 0xF6, 0xF7);
        c->header_inactive_fg = COL(0x92, 0x92, 0x96);
        c->tooltip_bg = COL(0x33, 0x33, 0x37);
        c->tooltip_fg = COL(0xFF, 0xFF, 0xFF);
        c->accent = COL(0x35, 0x84, 0xE4);
        c->border_t = 44;
    } else {
        c->window_bg = COL(0x22, 0x22, 0x26);
        c->window_fg = COL(0xFF, 0xFF, 0xFF);
        c->inactive_fg = COL(0x9A, 0x9A, 0x9E);
        c->view_bg = COL(0x1D, 0x1D, 0x20);
        c->view_fg = COL(0xFF, 0xFF, 0xFF);
        c->button_bg = COL(0x38, 0x38, 0x3C);
        c->button_fg = COL(0xFF, 0xFF, 0xFF);
        c->header_bg = COL(0x2E, 0x2E, 0x32);
        c->header_fg = COL(0xFF, 0xFF, 0xFF);
        c->header_inactive_bg = COL(0x26, 0x26, 0x2A);
        c->header_inactive_fg = COL(0x9A, 0x9A, 0x9E);
        c->tooltip_bg = COL(0x0E, 0x0E, 0x10);
        c->tooltip_fg = COL(0xFF, 0xFF, 0xFF);
        c->accent = COL(0x35, 0x84, 0xE4);
        c->border_t = 40;
    }
    c->select_bg = c->accent;
    c->select_fg = COL(0xFF, 0xFF, 0xFF);
    c->hover_deco = c->window_fg;
    c->negative = COL(0xE0, 0x1B, 0x24);
}

/* ---- choosing ---- */

static int on_kde(void)
{
    const char *d = SDL_getenv("XDG_CURRENT_DESKTOP");
    return d && SDL_strcasestr(d, "KDE") != NULL;
}

static void apply(void)
{
    ui_look_id look = LOOK_CLASSIC;
#ifndef _WIN32
    if (setting == LOOK_SET_SYSTEM)
        look = on_kde() ? LOOK_BREEZE : LOOK_ADWAITA;
    else if (setting == LOOK_SET_BREEZE)
        look = LOOK_BREEZE;
    else if (setting == LOOK_SET_ADWAITA)
        look = LOOK_ADWAITA;
#endif
    sys_theme = SDL_GetSystemTheme();
    UI_LOOK = look;
    if (look == LOOK_CLASSIC) {
        classic(&UI_PAL);
        snprintf(described, sizeof(described), "Classic");
    } else if (look == LOOK_BREEZE) {
        desk_colours c;
        char scheme[96];
        memset(&c, 0, sizeof(c));
        if (!breeze_from_kde(&c, scheme, sizeof(scheme))) {
            breeze_defaults(&c, sys_theme == SDL_SYSTEM_THEME_DARK);
            snprintf(scheme, sizeof(scheme), "%s",
                     sys_theme == SDL_SYSTEM_THEME_DARK ? "Breeze Dark" : "Breeze");
        }
        flat_from(&UI_PAL, &c);
        snprintf(described, sizeof(described), "Breeze (%s, %s)", scheme,
                 UI_PAL.dark ? "dark" : "light");
    } else {
        desk_colours c;
        memset(&c, 0, sizeof(c));
        adwaita(&c, sys_theme == SDL_SYSTEM_THEME_DARK);
        flat_from(&UI_PAL, &c);
        snprintf(described, sizeof(described), "Adwaita (%s)", UI_PAL.dark ? "dark" : "light");
    }
    LOG("ui: look %s", described);
}

void theme_set(look_setting s)
{
    if ((int)s < 0 || s >= LOOK_SET_COUNT)
        s = LOOK_SET_CLASSIC;
    setting = s;
    apply();
}

look_setting theme_setting(void)
{
    return setting;
}

int theme_poll(void)
{
    uint64_t now = SDL_GetTicks();
    int changed = 0;
    if (UI_LOOK == LOOK_CLASSIC || now < next_poll)
        return 0;
    next_poll = now + 1000;
    if (SDL_GetSystemTheme() != sys_theme)
        changed = 1;
#ifndef _WIN32
    if (UI_LOOK == LOOK_BREEZE) {
        int i;
        for (i = 0; i < KDE_FILES; i++) {
            char path[1100];
            SDL_PathInfo info;
            if (kdeglobals_path(i, path, sizeof(path))) {
                int have = SDL_GetPathInfo(path, &info);
                if (have != kde_mtime_known[i] || (have && info.modify_time != kde_mtime[i]))
                    changed = 1;
            }
        }
    }
#endif
    if (changed)
        apply();
    return changed;
}

static const char *const NAMES[LOOK_SET_COUNT] = { "classic", "system", "breeze", "adwaita" };

const char *theme_setting_name(look_setting s)
{
    return (int)s >= 0 && s < LOOK_SET_COUNT ? NAMES[s] : NAMES[0];
}

look_setting theme_setting_from_name(const char *name)
{
    int i;
    for (i = 0; name && i < LOOK_SET_COUNT; i++)
        if (SDL_strcasecmp(name, NAMES[i]) == 0)
            return (look_setting)i;
    return LOOK_SET_CLASSIC;
}

const char *theme_describe(void)
{
    return described;
}
