/*
 * Download Station List: builds a new stations.txt from Radio Browser in a
 * worker thread, only when the user asks (File, Download Station List).
 *
 *   1. Radio Browser's servers, found the documented way (a DNS lookup of
 *      all.api.radio-browser.info, then each address's name).
 *   2. The stations, page by page, most popular first (a pause between
 *      pages; each server tried four times before the next).
 *   3. GeoNames' cities1000.zip and Natural Earth's country borders, kept
 *      in the data folder's cache/ and downloaded only when missing or when
 *      asked to.
 *   4. core/rb_build.c turns them into stations-new.txt in the data folder,
 *      the same list tools/rb_to_stations.py would write.
 *
 * Nothing replaces the user's stations.txt until list_download_install().
 */
#ifndef DXG_LIST_DOWNLOAD_H
#define DXG_LIST_DOWNLOAD_H

#include <stddef.h>

#include "rb_build.h"

typedef enum { LD_IDLE, LD_RUNNING, LD_DONE, LD_FAILED, LD_CANCELLED } ld_state;

typedef struct {
    ld_state state;
    char stage[200];   /* "Radio Browser: 3,500 stations so far" */
    int pct;           /* 0-100 */
    char error[300];
    char report[4096]; /* the summary, when done */
    long stations;
    char new_path[1100];
} ld_status;

/* Starts a download (if none is running). refresh: download GeoNames and
 * the borders again even if cached. */
void list_download_start(const rb_options *o, int refresh);
void list_download_cancel(void);
/* Back to LD_IDLE after a failed or cancelled run (not while running). */
void list_download_reset(void);
void list_download_status(ld_status *s);
/* Waits for the worker to finish (at exit). */
void list_download_shutdown(void);
/* The cache's state, for the dialog: "GeoNames and borders downloaded
 * 2026-11-22" or "not downloaded yet". */
void list_download_cache_info(char *out, size_t n);

/* Puts stations-new.txt in place of stations.txt, keeping the old one as
 * stations-backup-<date>.txt (its name goes into backup). Returns 0, or -1
 * with a message. The caller reloads the library. */
int list_download_install(char *backup, size_t bn, char *err, size_t errn);

/* Runs a download without a window (--download-list): prints progress and
 * the summary. Returns 0 on success. */
int list_download_headless(const rb_options *o, int refresh);

#endif
