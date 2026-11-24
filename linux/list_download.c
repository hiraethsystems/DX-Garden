#include "list_download.h"

#include <SDL3/SDL.h>
#include <curl/curl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#include "fs.h"
#include "json.h"
#include "log.h"
#include "player.h"
#include "text.h"
#include "zip_read.h"

#define RB_DNS_NAME "all.api.radio-browser.info"
#define RETRIES 4          /* per server, waiting 2, 4 and 8 s between tries */
#define PAGE_DELAY_MS 1000 /* between pages, to go easy on the servers */
#define MAX_SERVERS 16
#define GEONAMES_URL "https://download.geonames.org/export/dump/cities1000.zip"
#define GEONAMES_FILE "cities1000.zip"
/* Region names, for stations without coordinates. */
#define ADMIN1_URL "https://download.geonames.org/export/dump/admin1CodesASCII.txt"
#define ADMIN1_FILE "admin1CodesASCII.txt"
/* Natural Earth 5.1.2 (public domain), the file the bundled list used. */
#define BORDERS_URL                                                                          \
    "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/v5.1.2/geojson/"         \
    "ne_50m_admin_0_countries.geojson"
#define BORDERS_FILE "ne_50m_admin_0_countries.geojson"
#define USER_AGENT "DXGarden/" DXG_VERSION " (" DXG_PLATFORM_NAME "; station list download)"
#define MAX_DOWNLOAD (200u << 20)

static struct {
    SDL_Mutex *lock;
    SDL_Thread *thread;
    ld_status st;
    SDL_AtomicInt cancel;
    rb_options opts;
    int refresh;
    int headless;
} L;

/* ---- status ------------------------------------------------------------ */

static void ensure_lock(void)
{
    if (!L.lock)
        L.lock = SDL_CreateMutex();
}

static void set_stage(int pct, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

static void set_stage(int pct, const char *fmt, ...)
{
    va_list ap;
    SDL_LockMutex(L.lock);
    va_start(ap, fmt);
    vsnprintf(L.st.stage, sizeof(L.st.stage), fmt, ap);
    va_end(ap);
    if (pct >= 0)
        L.st.pct = pct;
    SDL_UnlockMutex(L.lock);
    if (L.headless) {
        printf("%3d%%  %s\n", L.st.pct, L.st.stage);
        fflush(stdout);
    }
}

static int cancelled(void)
{
    return SDL_GetAtomicInt(&L.cancel) != 0;
}

/* Sleeps in small steps so Cancel is quick. Returns 1 if cancelled. */
static int nap(int ms)
{
    while (ms > 0 && !cancelled()) {
        SDL_Delay(ms < 100 ? (Uint32)ms : 100);
        ms -= 100;
    }
    return cancelled();
}

/* "12,345" */
static void thousands(long v, char *out, size_t n)
{
    char t[32];
    int len, i, o = 0;
    snprintf(t, sizeof(t), "%ld", v);
    len = (int)strlen(t);
    for (i = 0; i < len && (size_t)o + 2 < n; i++) {
        if (i && (len - i) % 3 == 0)
            out[o++] = ',';
        out[o++] = t[i];
    }
    out[o] = '\0';
}

/* ---- HTTP ---------------------------------------------------------------- */

typedef struct {
    char *p;
    size_t n, cap;
    int too_big;
    /* For a progress bar over a big file: pct from..to of the whole job. */
    int pct_from, pct_to;
    const char *what;
} membuf;

static size_t mem_write(char *data, size_t size, size_t nmemb, void *ud)
{
    membuf *m = (membuf *)ud;
    size_t k = size * nmemb;
    if (m->n + k > MAX_DOWNLOAD) {
        m->too_big = 1;
        return 0;
    }
    if (m->n + k + 1 > m->cap) {
        size_t nc = m->cap ? m->cap * 2 : 65536;
        char *np;
        while (nc < m->n + k + 1)
            nc *= 2;
        np = (char *)realloc(m->p, nc);
        if (!np)
            return 0;
        m->p = np;
        m->cap = nc;
    }
    memcpy(m->p + m->n, data, k);
    m->n += k;
    m->p[m->n] = '\0';
    return k;
}

static int on_progress(void *ud, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal,
                       curl_off_t ulnow)
{
    membuf *m = (membuf *)ud;
    (void)ultotal;
    (void)ulnow;
    if (cancelled())
        return 1;
    if (m->what && dltotal > 0) {
        int span = m->pct_to - m->pct_from;
        int pct = m->pct_from + (int)(span * (double)dlnow / (double)dltotal);
        SDL_LockMutex(L.lock);
        snprintf(L.st.stage, sizeof(L.st.stage), "Downloading %s: %ld of %ld KB", m->what,
                 (long)(dlnow / 1024), (long)(dltotal / 1024));
        L.st.pct = pct;
        SDL_UnlockMutex(L.lock);
    }
    return 0;
}

/* GET url into m. Returns 0, or -1 with a message. */
static int http_get(const char *url, membuf *m, char *err, size_t errn)
{
    CURL *c = curl_easy_init();
    CURLcode rc;
    long code = 0;
    char ebuf[CURL_ERROR_SIZE];
    if (!c) {
        snprintf(err, errn, "curl failed to start");
        return -1;
    }
    ebuf[0] = '\0';
    m->n = 0;
    m->too_big = 0;
    curl_easy_setopt(c, CURLOPT_URL, url);
    player_tls_setup(c);
    curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 100L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, mem_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, m);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, on_progress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, m);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, ebuf);
    rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    if (rc == CURLE_ABORTED_BY_CALLBACK && cancelled()) {
        snprintf(err, errn, "cancelled");
        return -1;
    }
    if (m->too_big) {
        snprintf(err, errn, "the file is too big");
        return -1;
    }
    if (rc != CURLE_OK) {
        snprintf(err, errn, "%s", ebuf[0] ? ebuf : curl_easy_strerror(rc));
        return -1;
    }
    if (code >= 400) {
        snprintf(err, errn, "HTTP %ld", code);
        return -1;
    }
    return 0;
}

/* ---- Radio Browser -------------------------------------------------------- */

static int find_servers(char names[][256], char *err, size_t errn)
{
    struct addrinfo hints, *res = NULL, *ai;
    int n = 0, rc, i, j;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    rc = getaddrinfo(RB_DNS_NAME, "443", &hints, &res);
    if (rc != 0) {
        snprintf(err, errn, "could not look up %s: %s", RB_DNS_NAME, gai_strerror(rc));
        return 0;
    }
    for (ai = res; ai && n < MAX_SERVERS; ai = ai->ai_next) {
        char host[256];
        size_t hl;
        int dup = 0;
        if (getnameinfo(ai->ai_addr, ai->ai_addrlen, host, sizeof(host), NULL, 0, NI_NAMEREQD))
            continue;
        hl = strlen(host);
        if (hl < 20 || strcmp(host + hl - 19, ".radio-browser.info") != 0)
            continue;
        for (i = 0; i < n; i++)
            if (!strcmp(names[i], host))
                dup = 1;
        if (!dup)
            snprintf(names[n++], 256, "%s", host);
    }
    freeaddrinfo(res);
    if (!n) {
        snprintf(err, errn, "no Radio Browser servers found via %s", RB_DNS_NAME);
        return 0;
    }
    /* A random order spreads the load, as the servers ask. */
    for (i = n - 1; i > 0; i--) {
        char t[256];
        j = SDL_rand(i + 1);
        memcpy(t, names[i], 256);
        memcpy(names[i], names[j], 256);
        memcpy(names[j], t, 256);
    }
    return n;
}

typedef struct {
    json_value **docs;
    size_t ndocs;
    const json_value **st;
    size_t n, cap;
} snapshot;

static void snapshot_free(snapshot *s)
{
    size_t i;
    for (i = 0; i < s->ndocs; i++)
        json_free(s->docs[i]);
    free(s->docs);
    free(s->st);
    memset(s, 0, sizeof(*s));
}

static int snapshot_add(snapshot *s, json_value *doc)
{
    size_t i;
    json_value **nd = (json_value **)realloc(s->docs, (s->ndocs + 1) * sizeof(*nd));
    if (!nd)
        return -1;
    s->docs = nd;
    s->docs[s->ndocs++] = doc;
    for (i = 0; i < doc->n; i++) {
        if (s->n == s->cap) {
            size_t nc = s->cap ? s->cap * 2 : 1024;
            const json_value **ns = (const json_value **)realloc(s->st, nc * sizeof(*ns));
            if (!ns)
                return -1;
            s->st = ns;
            s->cap = nc;
        }
        s->st[s->n++] = &doc->items[i];
    }
    return 0;
}

/* One page from the first server that answers. */
static json_value *get_page(char servers[][256], int *nserv, const char *query, char *err,
                            size_t errn)
{
    membuf m;
    memset(&m, 0, sizeof(m));
    while (*nserv > 0) {
        int attempt;
        char url[1600];
        snprintf(url, sizeof(url), "https://%.255s/json/stations/search?%.1200s", servers[0], query);
        for (attempt = 0; attempt < RETRIES; attempt++) {
            char perr[200], jerr[120];
            json_value *doc;
            if (http_get(url, &m, perr, sizeof(perr)) == 0) {
                doc = json_parse(m.p ? m.p : "", m.n, jerr, sizeof(jerr));
                if (doc && doc->type == JSON_ARRAY) {
                    free(m.p);
                    return doc;
                }
                json_free(doc);
                snprintf(perr, sizeof(perr), "unexpected reply (%s)",
                         doc ? "not a list" : jerr);
            }
            if (cancelled()) {
                free(m.p);
                snprintf(err, errn, "cancelled");
                return NULL;
            }
            snprintf(err, errn, "%.100s: %.190s", servers[0], perr);
            LOG("download: %s", err);
            if (attempt + 1 < RETRIES && nap(2000 << attempt)) {
                free(m.p);
                snprintf(err, errn, "cancelled");
                return NULL;
            }
        }
        LOG("download: %s failed %d times, trying the next server", servers[0], RETRIES);
        memmove(servers[0], servers[1], (size_t)(*nserv - 1) * 256);
        (*nserv)--;
    }
    free(m.p);
    return NULL;
}

static int fetch_stations(snapshot *s, char *err, size_t errn)
{
    char servers[MAX_SERVERS][256], query[1200], num[32];
    int nserv, c, ncc = L.opts.ncountries ? L.opts.ncountries : 1, pages = 0;
    long wanted = rb_wanted(&L.opts);
    set_stage(2, "Finding Radio Browser's servers...");
    nserv = find_servers(servers, err, errn);
    if (!nserv)
        return -1;
    LOG("download: %d Radio Browser servers, starting with %s", nserv, servers[0]);
    for (c = 0; c < ncc; c++) {
        const char *cc = L.opts.ncountries ? L.opts.countries[c] : NULL;
        long offset = 0, got = 0;
        while (got < wanted) {
            json_value *doc;
            size_t len;
            rb_query(&L.opts, cc, offset, query, sizeof(query));
            doc = get_page(servers, &nserv, query, err, errn);
            if (!doc)
                return -1;
            len = doc->n;
            if (snapshot_add(s, doc) != 0) {
                json_free(doc);
                snprintf(err, errn, "out of memory");
                return -1;
            }
            got += (long)len;
            pages++;
            thousands((long)s->n, num, sizeof(num));
            if (cc)
                set_stage(5 + (pages * 40 / (pages + 10)), "Radio Browser: %s stations so far (%s)",
                          num, cc);
            else
                set_stage(5 + (pages * 40 / (pages + 10)), "Radio Browser: %s stations so far",
                          num);
            if (len < RB_PAGE_SIZE)
                break;
            offset += RB_PAGE_SIZE;
            if (nap(PAGE_DELAY_MS)) {
                snprintf(err, errn, "cancelled");
                return -1;
            }
        }
    }
    LOG("download: %lu stations in %d pages", (unsigned long)s->n, pages);
    return 0;
}

/* ---- the cache ----------------------------------------------------------- */

static void cache_path(const char *file, char *out, size_t n)
{
    snprintf(out, n, "%scache/%s", PATHS.data, file);
}

/* The file from the cache, downloading it first if missing or refresh. */
static char *cached_file(const char *url, const char *file, const char *what, int refresh,
                         int pct_from, int pct_to, size_t *len, char *err, size_t errn)
{
    char path[1200], cdir[1100];
    read_status rs;
    char *data;
    cache_path(file, path, sizeof(path));
    if (!refresh && fs_exists(path)) {
        data = read_file(path, MAX_DOWNLOAD, len, &rs);
        if (data)
            return data;
    }
    snprintf(cdir, sizeof(cdir), "%scache", PATHS.data);
    fs_mkdir(cdir);
    {
        membuf m;
        memset(&m, 0, sizeof(m));
        m.pct_from = pct_from;
        m.pct_to = pct_to;
        m.what = what;
        set_stage(pct_from, "Downloading %s...", what);
        if (http_get(url, &m, err, errn) != 0) {
            char e2[300];
            snprintf(e2, sizeof(e2), "%s: %s", what, err);
            snprintf(err, errn, "%s", e2);
            free(m.p);
            return NULL;
        }
        if (fs_write_file(path, m.p ? m.p : "", m.n) != 0)
            LOG("download: could not keep %s in the cache", path);
        LOG("download: %s, %lu bytes", url, (unsigned long)m.n);
        *len = m.n;
        if (!m.p)
            m.p = (char *)calloc(1, 1);
        return m.p;
    }
}

void list_download_cache_info(char *out, size_t n)
{
    char path[1200];
    SDL_PathInfo info;
    cache_path(GEONAMES_FILE, path, sizeof(path));
    if (SDL_GetPathInfo(path, &info)) {
        time_t t = (time_t)(info.modify_time / 1000000000LL);
        struct tm tm;
        char d[16];
        localtime_r(&t, &tm);
        strftime(d, sizeof(d), "%Y-%m-%d", &tm);
        snprintf(out, n, "Town names, regions and borders were downloaded %s.", d);
    } else {
        snprintf(out, n, "Town names, regions and borders aren't downloaded yet (about 14 MB).");
    }
}

/* ---- the job --------------------------------------------------------------- */

static int run_job(void)
{
    snapshot s;
    rb_geo *geo = NULL;
    rb_result r;
    char err[300], *buf, *text, *head = NULL, *report = NULL, num[32];
    size_t len, tl, i, total;
    int rc = -1;

    memset(&s, 0, sizeof(s));
    memset(&r, 0, sizeof(r));
    err[0] = '\0';
    if (fetch_stations(&s, err, sizeof(err)) != 0)
        goto out;

    buf = cached_file(GEONAMES_URL, GEONAMES_FILE, "GeoNames towns", L.refresh, 45, 75, &len,
                      err, sizeof(err));
    if (!buf)
        goto out;
    set_stage(76, "Reading the town names...");
    text = zip_extract((const unsigned char *)buf, len, ".txt", &tl, err, sizeof(err));
    free(buf);
    if (!text) {
        char e2[300];
        snprintf(e2, sizeof(e2), "GeoNames: %s", err);
        snprintf(err, sizeof(err), "%.240s (try again with \"Download them again\")", e2);
        goto out;
    }
    geo = rb_geo_new();
    if (!geo || rb_geo_load_cities(geo, text, tl) <= 0) {
        free(text);
        snprintf(err, sizeof(err), "GeoNames: no towns in the file");
        goto out;
    }
    free(text);
    if (cancelled())
        goto out;
    buf = cached_file(BORDERS_URL, BORDERS_FILE, "country borders", L.refresh, 80, 88, &len, err,
                      sizeof(err));
    if (!buf)
        goto out;
    set_stage(89, "Reading the borders...");
    {
        char jerr[120];
        long ns = rb_geo_load_borders(geo, buf, len, jerr, sizeof(jerr));
        free(buf);
        if (ns <= 0) {
            snprintf(err, sizeof(err), "borders: %s (try again with \"Download them again\")",
                     ns < 0 ? jerr : "no shapes");
            goto out;
        }
    }
    if (cancelled())
        goto out;
    if (L.opts.without_coords) {
        long nr;
        buf = cached_file(ADMIN1_URL, ADMIN1_FILE, "GeoNames regions", L.refresh, 89, 90, &len,
                          err, sizeof(err));
        if (!buf)
            goto out;
        set_stage(90, "Reading the region names...");
        nr = rb_geo_load_admin1(geo, buf, len);
        free(buf);
        if (nr <= 0) {
            snprintf(err, sizeof(err),
                     "GeoNames regions: none in the file (try again with \"Download them "
                     "again\")");
            goto out;
        }
        LOG("download: %ld regions", nr);
    }
    if (cancelled())
        goto out;

    thousands((long)s.n, num, sizeof(num));
    set_stage(92, "Building the list from %s stations...", num);
    if (rb_convert(s.st, s.n, &L.opts, geo, &r) != 0) {
        snprintf(err, sizeof(err), "out of memory");
        goto out;
    }
    head = rb_header(&L.opts, 1, r.n, time(NULL), time(NULL),
                     "DX Garden " DXG_VERSION " (build " DXG_BUILD ")", NULL);
    total = head ? strlen(head) : 0;
    for (i = 0; i < r.n; i++)
        total += strlen(r.lines[i].line) + 1;
    if (total > 32u * 1024 * 1024) {
        snprintf(err, sizeof(err), "the list would be over 32 MB: lower \"Most stations\"");
        goto out;
    }
    {
        char *all = (char *)malloc(total + 1), *o = all;
        if (!all) {
            snprintf(err, sizeof(err), "out of memory");
            goto out;
        }
        if (head) {
            memcpy(o, head, strlen(head));
            o += strlen(head);
        }
        for (i = 0; i < r.n; i++) {
            size_t k = strlen(r.lines[i].line);
            memcpy(o, r.lines[i].line, k);
            o += k;
            *o++ = '\n';
        }
        if (fs_write_file(L.st.new_path, all, total) != 0) {
            free(all);
            snprintf(err, sizeof(err), "could not write stations-new.txt in the data folder");
            goto out;
        }
        free(all);
    }
    report = rb_report(&r, &L.opts, 1, "stations-new.txt", total);
    SDL_LockMutex(L.lock);
    snprintf(L.st.report, sizeof(L.st.report), "%s", report ? report : "");
    L.st.stations = (long)r.n;
    SDL_UnlockMutex(L.lock);
    LOG("download: wrote %lu stations to %s", (unsigned long)r.n, L.st.new_path);
    rc = 0;

out:
    if (rc != 0 && (cancelled() || !strcmp(err, "cancelled")))
        rc = 1;
    SDL_LockMutex(L.lock);
    if (rc == 0) {
        L.st.state = LD_DONE;
        L.st.pct = 100;
        thousands(L.st.stations, num, sizeof(num));
        snprintf(L.st.stage, sizeof(L.st.stage), "Done: %s stations.", num);
    } else if (rc == 1) {
        L.st.state = LD_CANCELLED;
        snprintf(L.st.stage, sizeof(L.st.stage), "Cancelled.");
    } else {
        L.st.state = LD_FAILED;
        snprintf(L.st.error, sizeof(L.st.error), "%s", err[0] ? err : "failed");
        snprintf(L.st.stage, sizeof(L.st.stage), "Failed.");
        LOG("download: failed: %s", L.st.error);
    }
    SDL_UnlockMutex(L.lock);
    free(head);
    free(report);
    rb_result_free(&r);
    rb_geo_free(geo);
    snapshot_free(&s);
    return rc;
}

static int SDLCALL worker(void *ud)
{
    (void)ud;
    run_job();
    return 0;
}

static void prepare(const rb_options *o, int refresh)
{
    ensure_lock();
    SDL_LockMutex(L.lock);
    memset(&L.st, 0, sizeof(L.st));
    L.st.state = LD_RUNNING;
    snprintf(L.st.stage, sizeof(L.st.stage), "Starting...");
    snprintf(L.st.new_path, sizeof(L.st.new_path), "%sstations-new.txt", PATHS.data);
    SDL_UnlockMutex(L.lock);
    L.opts = *o;
    L.refresh = refresh;
    SDL_SetAtomicInt(&L.cancel, 0);
}

void list_download_start(const rb_options *o, int refresh)
{
    ensure_lock();
    if (L.thread) {
        ld_status s;
        list_download_status(&s);
        if (s.state == LD_RUNNING)
            return;
        SDL_WaitThread(L.thread, NULL);
        L.thread = NULL;
    }
    prepare(o, refresh);
    LOG("download: started (codecs %u, hls %d, max %d, %d countries, refresh %d)", o->codecs,
        o->hls, o->max, o->ncountries, refresh);
    L.thread = SDL_CreateThread(worker, "list download", NULL);
    if (!L.thread) {
        SDL_LockMutex(L.lock);
        L.st.state = LD_FAILED;
        snprintf(L.st.error, sizeof(L.st.error), "could not start a thread");
        SDL_UnlockMutex(L.lock);
    }
}

void list_download_cancel(void)
{
    SDL_SetAtomicInt(&L.cancel, 1);
}

void list_download_reset(void)
{
    ensure_lock();
    SDL_LockMutex(L.lock);
    if (L.st.state != LD_RUNNING)
        L.st.state = LD_IDLE;
    SDL_UnlockMutex(L.lock);
}

void list_download_status(ld_status *s)
{
    ensure_lock();
    SDL_LockMutex(L.lock);
    *s = L.st;
    SDL_UnlockMutex(L.lock);
}

void list_download_shutdown(void)
{
    if (L.thread) {
        list_download_cancel();
        SDL_WaitThread(L.thread, NULL);
        L.thread = NULL;
    }
}

int list_download_install(char *backup, size_t bn, char *err, size_t errn)
{
    char path[1200], date[16];
    time_t now = time(NULL);
    struct tm tm;
    int i;
    ld_status s;
    list_download_status(&s);
    if (s.state != LD_DONE || !fs_exists(s.new_path)) {
        snprintf(err, errn, "there is no new list to install");
        return -1;
    }
    backup[0] = '\0';
    if (fs_exists(PATHS.stations)) {
        localtime_r(&now, &tm);
        strftime(date, sizeof(date), "%Y-%m-%d", &tm);
        for (i = 1; i < 100; i++) {
            if (i == 1)
                snprintf(path, sizeof(path), "%sstations-backup-%s.txt", PATHS.data, date);
            else
                snprintf(path, sizeof(path), "%sstations-backup-%s-%d.txt", PATHS.data, date, i);
            if (!fs_exists(path))
                break;
        }
        if (!SDL_RenamePath(PATHS.stations, path)) {
            snprintf(err, errn, "could not keep your old list: %s", SDL_GetError());
            return -1;
        }
        snprintf(backup, bn, "%s", strrchr(path, '/') ? strrchr(path, '/') + 1 : path);
        LOG("download: old list kept as %s", path);
    }
    if (!SDL_RenamePath(s.new_path, PATHS.stations)) {
        snprintf(err, errn, "could not put the new list in place: %s", SDL_GetError());
        if (backup[0])
            SDL_RenamePath(path, PATHS.stations);
        return -1;
    }
    SDL_LockMutex(L.lock);
    L.st.state = LD_IDLE;
    SDL_UnlockMutex(L.lock);
    LOG("download: new list installed");
    return 0;
}

int list_download_headless(const rb_options *o, int refresh)
{
    int rc;
    L.headless = 1;
    prepare(o, refresh);
    rc = run_job();
    if (rc == 0) {
        printf("%s", L.st.report);
        printf("New list: %s\n", L.st.new_path);
    } else {
        printf("%s %s\n", L.st.stage, L.st.error);
    }
    return rc == 0 ? 0 : 1;
}
