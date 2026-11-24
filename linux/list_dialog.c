#include "list_dialog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list_download.h"
#include "log.h"

ui_window *listdlg_window;

static void (*on_reload)(void);
static void (*on_status)(const char *msg);

static struct {
    int mp3, aac, aacp, ogg, opus, flac, hls, refresh, nocoords;
    char countries[200], bitrate[8], tag[64], most[10], km[8];
    char cache_info[200];
    /* The summary, one list row per line. */
    char *report;
    char *lines[200];
    int nlines;
    ui_listview lv;
    ui_column col;
    int shown_done; /* the summary for this run is set up */
} D;

static void set_defaults(void)
{
    /* Like the bundled list: every format, HLS too, no real cap. */
    D.mp3 = D.aac = D.aacp = D.ogg = D.opus = D.flac = D.hls = 1;
    D.refresh = 0;
    D.nocoords = 1;
    D.countries[0] = '\0';
    D.tag[0] = '\0';
    snprintf(D.bitrate, sizeof(D.bitrate), "0");
    snprintf(D.most, sizeof(D.most), "100000");
    snprintf(D.km, sizeof(D.km), "30");
}

static void report_cell(int row, int col, char *out, int n, void *ud)
{
    (void)col;
    (void)ud;
    snprintf(out, (size_t)n, "%s", row < D.nlines ? D.lines[row] : "");
}

/* The summary with its long lines ("Per codec: AAC 17, AAC+ 5, ...")
 * broken after a comma, so they fit the dialog without scrolling sideways. */
#define WRAP_COLS 62

static void take_report(const char *text)
{
    char *p, *o;
    size_t n = strlen(text);
    free(D.report);
    D.report = (char *)malloc(n * 2 + 16);
    if (!D.report)
        return;
    o = D.report;
    while (*text) {
        const char *eol = strchr(text, '\n');
        size_t len = eol ? (size_t)(eol - text) : strlen(text), col = 0, i, indent = 0;
        while (indent < len && text[indent] == ' ')
            indent++;
        for (i = 0; i < len; i++) {
            *o++ = text[i];
            col++;
            if (text[i] == ',' && i + 1 < len && text[i + 1] == ' ' && col > WRAP_COLS - 16) {
                const char *next = memchr(text + i + 1, ',', len - i - 1);
                size_t until = next ? (size_t)(next - text) - i : len - i;
                if (col + until > WRAP_COLS) {
                    size_t k;
                    *o++ = '\n';
                    for (k = 0; k < indent + 4; k++)
                        *o++ = ' ';
                    col = indent + 4;
                    i++; /* the space after the comma */
                }
            }
        }
        *o++ = '\n';
        text += len + (eol ? 1 : 0);
    }
    *o = '\0';
    D.nlines = 0;
    for (p = D.report; p && *p && D.nlines < (int)(sizeof(D.lines) / sizeof(D.lines[0]));) {
        char *nl = strchr(p, '\n');
        D.lines[D.nlines++] = p;
        if (!nl)
            break;
        *nl = '\0';
        p = nl + 1;
    }
    memset(&D.lv, 0, sizeof(D.lv));
    D.col.title = "Summary";
    D.col.width = 2000;
    D.lv.id = ui_id("ld_report");
    D.lv.cols = &D.col;
    D.lv.n_cols = 1;
    D.lv.n_rows = D.nlines;
    D.lv.sel = -1;
    D.lv.sort_col = -1;
    D.lv.dragging_col = -1;
    D.lv.cell = report_cell;
}

static const char *const OK_BUTTON[] = { "OK" };

/* Reads the options from the boxes. Returns 0, or -1 after a message. */
static int read_options(rb_options *o)
{
    char *end;
    long v;
    double km;
    rb_options_default(o);
    o->codecs = (D.mp3 ? RB_MP3 : 0) | (D.aac ? RB_AAC : 0) | (D.aacp ? RB_AACP : 0) |
                (D.ogg ? RB_OGG : 0) | (D.opus ? RB_OPUS : 0) | (D.flac ? RB_FLAC : 0);
    o->hls = D.hls;
    o->without_coords = D.nocoords;
    if (!o->codecs && !o->hls) {
        ui_msgbox_open("Download Station List", "Please tick at least one format.", MB_WARNING,
                       OK_BUTTON, 1);
        return -1;
    }
    if (rb_parse_countries(o, D.countries) != 0) {
        ui_msgbox_open("Download Station List",
                       "Countries are two-letter codes with commas between them, like "
                       "GB,IE,FR. Leave the box empty for every country.",
                       MB_WARNING, OK_BUTTON, 1);
        return -1;
    }
    v = D.bitrate[0] ? strtol(D.bitrate, &end, 10) : 0;
    if (D.bitrate[0] && (*end || v < 0 || v > 10000)) {
        ui_msgbox_open("Download Station List",
                       "The lowest bitrate is a number of kbps, or 0 for any.", MB_WARNING,
                       OK_BUTTON, 1);
        return -1;
    }
    o->min_bitrate = (int)v;
    v = D.most[0] ? strtol(D.most, &end, 10) : 1000000;
    if (D.most[0] && (*end || v < 1)) {
        ui_msgbox_open("Download Station List",
                       "Most stations is a number, or leave it empty for no limit.", MB_WARNING,
                       OK_BUTTON, 1);
        return -1;
    }
    o->max = (int)v;
    km = D.km[0] ? strtod(D.km, &end) : 30;
    if (D.km[0] && (*end || km <= 0 || km > 500)) {
        ui_msgbox_open("Download Station List",
                       "The town distance is a number of kilometres, from 1 to 500.",
                       MB_WARNING, OK_BUTTON, 1);
        return -1;
    }
    o->city_km = km;
    snprintf(o->tag, sizeof(o->tag), "%s", D.tag);
    return 0;
}

static void separator(rect c)
{
    draw_rect(c.x + 10, c.y + c.h - 44, c.w - 20, 1, UI_SHADOW);
    draw_rect(c.x + 10, c.y + c.h - 43, c.w - 20, 1, UI_HILIGHT);
}

static void draw_options(ui_window *w, rect c)
{
    int x = c.x + 12, y = c.y + 10, fw = c.w - 24, lw = 118, gy;
    rb_options o;
    y += ui_text_wrap(x, y, fw, UI_TEXT,
                      "Builds a new station list from Radio Browser (radio-browser.info), "
                      "with towns from GeoNames and country borders from Natural Earth. "
                      "Your own list stays as it is until you choose to replace it.") + 8;

    gy = y;
    ui_group("Formats", x, gy, fw, 66);
    ui_checkbox("MP3", x + 12, gy + 20, &D.mp3);
    ui_checkbox("AAC", x + 82, gy + 20, &D.aac);
    ui_checkbox("AAC+", x + 152, gy + 20, &D.aacp);
    ui_checkbox("Ogg Vorbis", x + 230, gy + 20, &D.ogg);
    ui_checkbox("Opus", x + 12, gy + 42, &D.opus);
    ui_checkbox("FLAC", x + 82, gy + 42, &D.flac);
    ui_checkbox("HLS streams", x + 152, gy + 42, &D.hls);
    y = gy + 76;

    ui_label(x, y + 4, "Countries:");
    ui_edit(ui_id("ld_cc"), D.countries, sizeof(D.countries), x + lw, y, fw - lw);
    y += 24;
    ui_label_col(x + lw, y, UI_GRAYTEXT, "Two-letter codes, like GB,IE. Empty for all.");
    y += 20;
    ui_label(x, y + 4, "Tag:");
    ui_edit(ui_id("ld_tag"), D.tag, sizeof(D.tag), x + lw, y, 140);
    ui_label_col(x + lw + 150, y + 4, UI_GRAYTEXT, "e.g. jazz; empty for any");
    y += 26;
    ui_label(x, y + 4, "Lowest bitrate:");
    ui_edit(ui_id("ld_br"), D.bitrate, sizeof(D.bitrate), x + lw, y, 60);
    ui_label_col(x + lw + 70, y + 4, UI_GRAYTEXT, "kbps; 0 for any");
    y += 26;
    ui_label(x, y + 4, "Most stations:");
    ui_edit(ui_id("ld_max"), D.most, sizeof(D.most), x + lw, y, 60);
    ui_label_col(x + lw + 70, y + 4, UI_GRAYTEXT, "the most popular first");
    y += 26;
    ui_label(x, y + 4, "Town distance:");
    ui_edit(ui_id("ld_km"), D.km, sizeof(D.km), x + lw, y, 60);
    ui_label_col(x + lw + 70, y + 4, UI_GRAYTEXT, "km; farther stations keep their own");
    y += 28;
    ui_checkbox("Stations without coordinates", x, y, &D.nocoords);
    y += 18;
    ui_label_col(x + 18, y, UI_GRAYTEXT,
                 "Placed by their region or name, or in a Country-wide place.");
    y += 26;

    ui_label_col(x, y, UI_TEXT, D.cache_info);
    y += 18;
    ui_checkbox("Download the town names, regions and borders again", x, y, &D.refresh);

    separator(c);
    if (ui_button_ex(ui_id("ld_go"), "Download", c.x + c.w - 168, c.y + c.h - 33, 75,
                     UI_BUTTON_H, 1, 0)) {
        if (read_options(&o) == 0) {
            D.shown_done = 0;
            list_download_start(&o, D.refresh);
        }
        return;
    }
    if (ui_button("Cancel", c.x + c.w - 87, c.y + c.h - 33, 75, UI_BUTTON_H) ||
        (UI.scope_has_focus && UI.key == SDLK_ESCAPE))
        ui_window_close(w);
}

static void draw_running(rect c, const ld_status *s)
{
    int x = c.x + 12, y = c.y + 16, fw = c.w - 24;
    y += ui_text_wrap(x, y, fw, UI_TEXT,
                      "Downloading. Radio Browser is asked for one page of stations a second, "
                      "to go easy on its volunteer-run servers, so a big list takes a few "
                      "minutes.") + 16;
    ui_label(x, y, s->stage);
    y += 22;
    ui_progress(x, y, fw, 20, s->pct);
    separator(c);
    if (ui_button_ex(ui_id("ld_cancel"), "Cancel", c.x + c.w - 87, c.y + c.h - 33, 75,
                     UI_BUTTON_H, 1, 0) ||
        (UI.scope_has_focus && UI.key == SDLK_ESCAPE))
        list_download_cancel();
}

static void draw_done(ui_window *w, rect c, const ld_status *s)
{
    int x = c.x + 12, y = c.y + 10, fw = c.w - 24, lh;
    if (!D.shown_done) {
        take_report(s->report);
        D.shown_done = 1;
    }
    {
        char line[300];
        snprintf(line, sizeof(line), "%s It is saved as stations-new.txt in the data folder.",
                 s->stage);
        y += ui_text_wrap(x, y, fw, UI_TEXT, line) + 6;
    }
    lh = c.y + c.h - 52 - 40 - y;
    ui_list(&D.lv, x, y, fw, lh);
    y += lh + 6;
    ui_text_wrap(x, y, fw, UI_TEXT,
                 "Replacing keeps your current list as stations-backup-<date>.txt, so you "
                 "can go back to it.");
    separator(c);
    if (ui_button_ex(ui_id("ld_replace"), "Replace My Station List", c.x + c.w - 268,
                     c.y + c.h - 33, 180, UI_BUTTON_H, 1, 0)) {
        char backup[300], err[300], msg[500];
        if (list_download_install(backup, sizeof(backup), err, sizeof(err)) != 0) {
            snprintf(msg, sizeof(msg), "The list was not replaced: %s.", err);
            ui_msgbox_open("Download Station List", msg, MB_ERROR, OK_BUTTON, 1);
            return;
        }
        ui_window_close(w);
        if (on_reload)
            on_reload();
        if (on_status) {
            if (backup[0])
                snprintf(msg, sizeof(msg), "New station list in place; the old one is %s",
                         backup);
            else
                snprintf(msg, sizeof(msg), "New station list in place");
            on_status(msg);
        }
        return;
    }
    if (ui_button("Close", c.x + c.w - 87, c.y + c.h - 33, 75, UI_BUTTON_H) ||
        (UI.scope_has_focus && UI.key == SDLK_ESCAPE))
        ui_window_close(w);
}

static void draw_failed(ui_window *w, rect c, const ld_status *s)
{
    int x = c.x + 12, y = c.y + 16, fw = c.w - 24;
    char line[500];
    if (s->state == LD_CANCELLED)
        snprintf(line, sizeof(line), "The download was cancelled. Your station list is as it "
                                     "was.");
    else
        snprintf(line, sizeof(line),
                 "The download didn't work: %s. Your station list is as it was. The log "
                 "(in the data folder) has the details.",
                 s->error);
    ui_text_wrap(x, y, fw, UI_TEXT, line);
    separator(c);
    if (ui_button_ex(ui_id("ld_back"), "Back", c.x + c.w - 168, c.y + c.h - 33, 75,
                     UI_BUTTON_H, 1, 0)) {
        list_download_reset();
        return;
    }
    if (ui_button("Close", c.x + c.w - 87, c.y + c.h - 33, 75, UI_BUTTON_H) ||
        (UI.scope_has_focus && UI.key == SDLK_ESCAPE))
        ui_window_close(w);
}

static void dialog_draw(ui_window *w, rect c, void *ud)
{
    ld_status s;
    (void)ud;
    list_download_status(&s);
    /* Closing the window while it runs cancels; it closes when stopped. */
    if (w->close_requested) {
        w->close_requested = 0;
        if (s.state == LD_RUNNING)
            list_download_cancel();
        else
            ui_window_close(w);
    }
    switch (s.state) {
    case LD_RUNNING: draw_running(c, &s); break;
    case LD_DONE: draw_done(w, c, &s); break;
    case LD_FAILED:
    case LD_CANCELLED: draw_failed(w, c, &s); break;
    default: draw_options(w, c); break;
    }
}

void listdlg_init(void (*reload)(void), void (*status)(const char *msg))
{
    on_reload = reload;
    on_status = status;
    set_defaults();
    listdlg_window = ui_window_new("Download Station List", ICON_GLOBE, 0, 0, 470, 452,
                                   UIW_DIALOG | UIW_MODAL, dialog_draw, NULL);
    listdlg_window->keep_on_close = 1;
}

void listdlg_open(void)
{
    ld_status s;
    list_download_status(&s);
    /* A finished or failed run from before starts again at the options. */
    if (s.state == LD_FAILED || s.state == LD_CANCELLED)
        list_download_reset();
    list_download_cache_info(D.cache_info, sizeof(D.cache_info));
    ui_window_center(listdlg_window);
    UI.focus = 0;
    ui_window_show(listdlg_window);
}
