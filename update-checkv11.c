/*
 * update-checkv11.c — Bogart Linux version checker
 * KISS rewrite: single flat sources table, no Arch API dependency.
 * Compile: gcc -o check-updatesv9 update-checkv11.c deps.c -lcurl
 * Edit sources.h to add/remove/update tracked packages — not this file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <ctype.h>
#include "deps.h"
#include "sources.h"

/* ── Defines ────────────────────────────────────────────────────────── */
#define MAX_PACKAGES  512
#define MAX_LEN       256
#define PARALLEL      8

#define RED     "\033[0;31m"
#define GREEN   "\033[0;32m"
#define YELLOW  "\033[1;33m"
#define CYAN    "\033[0;36m"
#define BLUE    "\033[0;34m"
#define DIM     "\033[2m"
#define NC      "\033[0m"

/* ── Package struct ─────────────────────────────────────────────────── */
typedef enum { ST_OUTDATED=0, ST_CURRENT=1, ST_AHEAD=2,
               ST_NOTFOUND=3, ST_SKIP=4 } Status;
typedef struct { char *data; size_t size; } Buf;
typedef struct {
    char     name[MAX_LEN];
    char     inst[MAX_LEN];
    char     got[MAX_LEN];
    char     url[512];
    char     tag_prefix[32];
    SrcType  src_type;
    Status   status;
    Buf      buf;
    CURL    *h;
    int      queued;
} Pkg;

const Source *src_lookup(const char *name) {
    for (int i = 0; SOURCES[i].name; i++)
        if (!strcmp(name, SOURCES[i].name)) return &SOURCES[i];
    return NULL;
}

/* ── Write callback ─────────────────────────────────────────────────── */
static size_t wcb(void *p, size_t sz, size_t n, void *ud) {
    Buf *b = ud;
    size_t t = sz * n;
    char *tmp = realloc(b->data, b->size + t + 1);
    if (!tmp) return 0;
    b->data = tmp;
    memcpy(b->data + b->size, p, t);
    b->size += t;
    b->data[b->size] = '\0';
    return t;
}

/* ── porg log splitter ──────────────────────────────────────────────── */
void split_porg(const char *entry, char *name, char *ver, size_t len) {
    name[0] = ver[0] = '\0';
    const char *us = strchr(entry, '_');
    if (us && isdigit(*(us + 1))) {
        size_t nl = us - entry; if (nl >= len) nl = len - 1;
        strncpy(name, entry, nl); name[nl] = '\0';
        strncpy(ver, us + 1, len - 1); return;
    }
    int has_digit = 0;
    for (const char *p = entry; *p; p++)
        if (isdigit((unsigned char)*p)) { has_digit = 1; break; }
    if (!has_digit) { strncpy(name, entry, len - 1); return; }
    const char *candidates[64]; int nc = 0;
    for (const char *p = entry; *p; p++) {
        if (*p == '-') {
            const char *v = p + 1;
            if (isdigit((unsigned char)*v) ||
                (*v == 'v' && isdigit((unsigned char)*(v + 1))) ||
                (*v == 'r' && isdigit((unsigned char)*(v + 1))))
                if (nc < 64) candidates[nc++] = p;
        }
    }
    if (nc == 0) {
        const char *p = entry;
        while (*p && !isdigit((unsigned char)*p)) p++;
        if (*p && p != entry) {
            size_t nl = p - entry; if (nl >= len) nl = len - 1;
            strncpy(name, entry, nl); name[nl] = '\0';
            strncpy(ver, p, len - 1);
        } else strncpy(name, entry, len - 1);
        return;
    }
    const char *best = NULL;
    for (int i = nc - 1; i >= 0; i--) {
        if (candidates[i] > entry && !isdigit((unsigned char)*(candidates[i] - 1)))
            { best = candidates[i]; break; }
    }
    if (!best) best = candidates[nc - 1];
    size_t nl = best - entry; if (nl >= len) nl = len - 1;
    strncpy(name, entry, nl); name[nl] = '\0';
    const char *vs = best + 1;
    if ((*vs == 'v' || *vs == 'r') && isdigit((unsigned char)*(vs + 1))) vs++;
    strncpy(ver, vs, len - 1);
    char *dash = strrchr(ver, '-');
    if (dash && *(dash + 1)) {
        int all_dig = 1;
        for (char *c = dash + 1; *c; c++)
            if (!isdigit((unsigned char)*c)) { all_dig = 0; break; }
        if (all_dig) *dash = '\0';
        else {
            char first[MAX_LEN]; size_t fl = dash - ver;
            if (fl < MAX_LEN) {
                strncpy(first, ver, fl); first[fl] = '\0';
                if (!strcmp(first, dash + 1)) *dash = '\0';
            }
        }
    }
}

/* ── Version parsers ────────────────────────────────────────────────── */
void parse_gh_ver(const char *json, char *out, size_t outlen) {
    out[0] = '\0';
    const char *pr = strstr(json, "\"prerelease\"");
    if (pr) {
        const char *colon = strchr(pr, ':');
        if (colon) {
            while (*colon == ':' || *colon == ' ') colon++;
            if (strncmp(colon, "true", 4) == 0)
                { strncpy(out, "NOT_FOUND", outlen); return; }
        }
    }
    const char *t = strstr(json, "\"tag_name\"");
    if (!t) { strncpy(out, "NOT_FOUND", outlen); return; }
    t = strchr(t, ':'); if (!t) { strncpy(out, "NOT_FOUND", outlen); return; }
    t = strchr(t, '"'); if (!t) { strncpy(out, "NOT_FOUND", outlen); return; }
    t++;
    const char *e = strchr(t, '"');
    if (!e) { strncpy(out, "NOT_FOUND", outlen); return; }
    size_t l = e - t; if (l >= outlen) l = outlen - 1;
    strncpy(out, t, l); out[l] = '\0';
    if (out[0] == 'v') memmove(out, out + 1, strlen(out));
}

void parse_gh_tag_ver(const char *json, char *out, size_t outlen, const char *prefix) {
    out[0] = '\0';
    const char *arr = strchr(json, '[');
    if (!arr) { strncpy(out, "NOT_FOUND", outlen); return; }
    const char *p = arr + 1;
    while (*p && *p != ']') {
        const char *t = strstr(p, "\"name\"");
        if (!t) break;
        t = strchr(t, ':'); if (!t) break;
        t = strchr(t, '"'); if (!t) break;
        t++;
        const char *e = strchr(t, '"'); if (!e) break;
        size_t l = e - t; if (l >= outlen) l = outlen - 1;
        char candidate[MAX_LEN];
        strncpy(candidate, t, l); candidate[l] = '\0';
        if (!prefix || strncmp(candidate, prefix, strlen(prefix)) == 0) {
            strncpy(out, candidate, outlen - 1);
            if (out[0] == 'v') memmove(out, out + 1, strlen(out));
            return;
        }
        p = e + 1;
    }
    strncpy(out, "NOT_FOUND", outlen);
}

int ver_cmp(const char *a, const char *b);

void parse_gh_reftag_ver(const char *json, char *out, size_t outlen, const char *prefix) {
    out[0] = '\0';
    char best[MAX_LEN] = "";
    const char *p = json;
    while (*p) {
        const char *t = strstr(p, "\"refs/tags/");
        if (!t) break;
        t += 11;
        const char *e = strchr(t, '"'); if (!e) break;
        size_t l = e - t; if (l >= MAX_LEN) { p = e + 1; continue; }
        char candidate[MAX_LEN];
        strncpy(candidate, t, l); candidate[l] = '\0';
        if (!prefix || strncmp(candidate, prefix, strlen(prefix)) == 0) {
            const char *cv = prefix ? candidate + strlen(prefix) : candidate;
            const char *bv = prefix && best[0] ? best + strlen(prefix) : best;
            if (cv[0] && isdigit((unsigned char)cv[0]))
                if (!best[0] || ver_cmp(cv, bv) > 0)
                    strncpy(best, candidate, MAX_LEN - 1);
        }
        p = e + 1;
    }
    if (best[0]) { strncpy(out, best, outlen - 1); out[outlen - 1] = '\0'; }
    else strncpy(out, "NOT_FOUND", outlen);
}

void parse_gnu_ver(const char *html, const char *pkg, char *out, size_t outlen) {
    out[0] = '\0';
    char best[MAX_LEN] = "";
    char pat[MAX_LEN];
    snprintf(pat, sizeof(pat), "%s-", pkg);
    const char *p = html;
    while (*p) {
        const char *t = strstr(p, pat);
        if (!t) break;
        t += strlen(pat);
        if (!isdigit((unsigned char)*t)) { p = t; continue; }
        const char *e = strstr(t, ".tar");
        if (!e) { p = t; continue; }
        size_t l = e - t; if (l >= MAX_LEN) { p = t; continue; }
        char candidate[MAX_LEN];
        strncpy(candidate, t, l); candidate[l] = '\0';
        if (strstr(candidate, "alpha") || strstr(candidate, "beta") ||
            strstr(candidate, "rc")    || strstr(candidate, "pre"))
            { p = e; continue; }
        if (!best[0] || ver_cmp(candidate, best) > 0)
            strncpy(best, candidate, MAX_LEN - 1);
        p = e;
    }
    if (best[0]) strncpy(out, best, outlen - 1);
    else strncpy(out, "NOT_FOUND", outlen);
}

void parse_pypi_ver(const char *json, char *out, size_t outlen) {
    out[0] = '\0';
    const char *t = strstr(json, "\"version\"");
    if (!t) { strncpy(out, "NOT_FOUND", outlen); return; }
    t = strchr(t, ':'); if (!t) { strncpy(out, "NOT_FOUND", outlen); return; }
    t = strchr(t, '"'); if (!t) { strncpy(out, "NOT_FOUND", outlen); return; }
    t++;
    const char *e = strchr(t, '"');
    if (!e) { strncpy(out, "NOT_FOUND", outlen); return; }
    size_t l = e - t; if (l >= outlen) l = outlen - 1;
    strncpy(out, t, l); out[l] = '\0';
}

void parse_freedesktop_ver(const char *json, char *out, size_t outlen) {
    parse_gh_tag_ver(json, out, outlen, NULL);
}

void ver_clean(const char *in, char *out, size_t n) {
    strncpy(out, in, n - 1); out[n - 1] = '\0';
    /* R_X_Y_Z format (expat) */
    if (out[0] == 'R' && out[1] == '_') {
        memmove(out, out + 2, strlen(out) - 1);
        for (char *p = out; *p; p++) if (*p == '_') *p = '.';
        return;
    }
    if (strncmp(out, "Release_", 8) == 0) {
        memmove(out, out + 8, strlen(out) - 7);
        for (char *p = out; *p; p++) if (*p == '_') *p = '.';
        return;
    }
    /* strip known prefixes */
    const char *pfx[] = {"llvmorg-", "FILE5_", "go", "n", "v", NULL};
    for (int i = 0; pfx[i]; i++) {
        size_t pl = strlen(pfx[i]);
        if (strncmp(out, pfx[i], pl) == 0 && isdigit((unsigned char)out[pl])) {
            memmove(out, out + pl, strlen(out) - pl + 1); break;
        }
    }
    /* strip pkgname- prefix: openssl-4.0.0→4.0.0, pcre2-10.47→10.47
     * find first '-' followed by digit, strip everything before it
     * but only if what precedes looks like a package name (has letters) */
    {
        char *dash = out;
        while ((dash = strchr(dash, '-')) != NULL) {
            if (isdigit((unsigned char)*(dash + 1))) {
                /* check if there are any letters before this dash */
                int has_alpha = 0;
                for (char *c = out; c < dash; c++)
                    if (isalpha((unsigned char)*c)) { has_alpha = 1; break; }
                if (has_alpha)
                    memmove(out, dash + 1, strlen(dash));
                break;
            }
            dash++;
        }
    }
    /* strip pkgname prefix without dash: lcms2.19.1→2.19.1, libnl3.12.0→3.12.0
     * if starts with letters followed by digit.digit pattern, strip the letters */
    {
        char *p = out;
        while (*p && isalpha((unsigned char)*p)) p++;
        if (p != out && *p && isdigit((unsigned char)*p) &&
            *(p+1) == '.') {
            memmove(out, p, strlen(p) + 1);
        }
    }
    /* strip + suffix */
    char *plus = strchr(out, '+'); if (plus) *plus = '\0';
    /* strip pre/alpha/beta/rc suffix */
    const char *pre[] = {"-pre", "-alpha", "-beta", "-rc", "-lts-lgpl", "-lts", NULL};
    for (int i = 0; pre[i]; i++) {
        char *s = strstr(out, pre[i]); if (s) { *s = '\0'; break; }
    }
    /* strip numeric pkgrel: 8.20.0-1 → 8.20.0 */
    char *dash = strrchr(out, '-');
    if (dash && *(dash + 1)) {
        int ok = 1; for (char *x = dash + 1; *x; x++)
            if (!isdigit((unsigned char)*x)) { ok = 0; break; }
        if (ok) *dash = '\0';
    }
    /* FILE5_X_Y_Z leftover underscores */
    for (char *p = out; *p; p++) if (*p == '_' && isdigit((unsigned char)*(p+1))) *p = '.';
}

int ver_cmp(const char *a, const char *b) {
    char ac[MAX_LEN], bc[MAX_LEN];
    ver_clean(a, ac, sizeof(ac)); ver_clean(b, bc, sizeof(bc));
    char av[MAX_LEN], bv[MAX_LEN];
    strncpy(av, ac, MAX_LEN); strncpy(bv, bc, MAX_LEN);
    char *ap = av, *bp = bv;
    while (*ap || *bp) {
        char *ad = strchr(ap, '.'), *bd = strchr(bp, '.');
        if (ad) *ad = '\0'; if (bd) *bd = '\0';
        int adi = isdigit((unsigned char)*ap), bdi = isdigit((unsigned char)*bp);
        int cmp;
        if (adi && bdi) { long ai = strtol(ap, NULL, 10), bi = strtol(bp, NULL, 10);
            cmp = ai > bi ? 1 : ai < bi ? -1 : 0; }
        else { cmp = strcmp(ap, bp); cmp = cmp > 0 ? 1 : cmp < 0 ? -1 : 0; }
        if (cmp) return cmp;
        ap = ad ? ad + 1 : ap + strlen(ap);
        bp = bd ? bd + 1 : bp + strlen(bp);
    }
    return 0;
}

static void add_auth_header(CURL *h, const char *hdr) {
    if (hdr[0]) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, hdr);
        curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers);
    }
}

static void build_url(Pkg *pk, const Source *s) {
    switch (s->src) {
    case SRC_GITHUB:
        snprintf(pk->url, sizeof(pk->url),
            "https://api.github.com/repos/%s/releases/latest", s->repo);
        break;
    case SRC_GHTAG:
        snprintf(pk->url, sizeof(pk->url),
            "https://api.github.com/repos/%s/tags", s->repo);
        break;
    case SRC_GHREFTAG:
        snprintf(pk->url, sizeof(pk->url),
            "https://api.github.com/repos/%s/git/refs/tags", s->repo);
        break;
    case SRC_GNU:
        snprintf(pk->url, sizeof(pk->url),
            "https://ftp.gnu.org/gnu/%s/", s->repo);
        break;
    case SRC_KERNEL:
        snprintf(pk->url, sizeof(pk->url),
            "https://www.kernel.org/pub/%s/", s->repo);
        break;
    case SRC_GCRYPT:
        snprintf(pk->url, sizeof(pk->url),
            "https://gnupg.org/ftp/gcrypt/%s/", s->repo);
        break;	
 //   case SRC_GNOME:
 //       snprintf(pk->url, sizeof(pk->url),
 //           "https://download.gnome.org/sources/%s/", s->repo);
 //       break;
 //   case SRC_FREEDESKTOP:
 //       snprintf(pk->url, sizeof(pk->url),
 //           "https://gitlab.freedesktop.org/%s/-/tags?format=json", s->repo);
 //       break;
    case SRC_PYPI:
        snprintf(pk->url, sizeof(pk->url),
            "https://pypi.org/pypi/%s/json", s->repo);
        break;
    default:
        pk->url[0] = '\0';
        break;
    }
}

static void parse_response(Pkg *pk) {
    switch (pk->src_type) {
    case SRC_GITHUB:
        parse_gh_ver(pk->buf.data, pk->got, MAX_LEN);
        break;
    case SRC_GHTAG:
        parse_gh_tag_ver(pk->buf.data, pk->got, MAX_LEN,
            pk->tag_prefix[0] ? pk->tag_prefix : NULL);
        break;
    case SRC_GHREFTAG:
        parse_gh_reftag_ver(pk->buf.data, pk->got, MAX_LEN,
            pk->tag_prefix[0] ? pk->tag_prefix : NULL);
        break;
    case SRC_GNU:
    case SRC_KERNEL:
        parse_gnu_ver(pk->buf.data, pk->name, pk->got, MAX_LEN);
        break;
    case SRC_PYPI:
        parse_pypi_ver(pk->buf.data, pk->got, MAX_LEN);
        break;
    default:
        strncpy(pk->got, "NOT_FOUND", MAX_LEN - 1);
        break;
    case SRC_GCRYPT:
        parse_gnu_ver(pk->buf.data, pk->name, pk->got, MAX_LEN);
        break;
    }
}

/* ── Main ───────────────────────────────────────────────────────────── */
int main(void) {
    FILE *tf = fopen("/root/.github_token", "r");
    char token[256] = "", auth_header[280] = "";
    if (tf) {
        fgets(token, sizeof(token), tf); fclose(tf);
        token[strcspn(token, "\n")] = '\0';
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);
    }

    static Pkg pkgs[MAX_PACKAGES];
    int np = 0;
    FILE *f = popen("porg -a 2>/dev/null", "r");
    if (!f) { fprintf(stderr, "porg -a failed\n"); return 1; }
    char line[512];
    while (fgets(line, sizeof(line), f) && np < MAX_PACKAGES) {
        line[strcspn(line, "\n")] = '\0';
        if (!line[0]) continue;
        char name[MAX_LEN], ver[MAX_LEN];
        split_porg(line, name, ver, MAX_LEN);
        if (!name[0] || strlen(name) < 2) continue;
        const Source *s = src_lookup(name);
        if (!s) continue;
        int dup = 0;
        for (int i = 0; i < np; i++)
            if (!strcmp(pkgs[i].name, name)) { dup = 1; break; }
        if (dup) continue;
        strncpy(pkgs[np].name, name, MAX_LEN - 1);
        strncpy(pkgs[np].inst, ver,  MAX_LEN - 1);
        pkgs[np].got[0]        = '\0';
        pkgs[np].tag_prefix[0] = '\0';
        pkgs[np].buf.data      = calloc(1, 1);
        if (!pkgs[np].buf.data) { fprintf(stderr, "OOM\n"); return 1; }
        pkgs[np].buf.size = 0;
        pkgs[np].h        = NULL;
        pkgs[np].queued   = 0;
        pkgs[np].src_type = s->src;
        if (s->src == SRC_SKIP) {
            pkgs[np].status = ST_SKIP;
        } else {
            pkgs[np].status = ST_NOTFOUND;
            if (s->prefix)
                strncpy(pkgs[np].tag_prefix, s->prefix, sizeof(pkgs[np].tag_prefix) - 1);
            build_url(&pkgs[np], s);
        }
        np++;
    }
    pclose(f);

    int n_active = 0;
    for (int i = 0; i < np; i++)
        if (pkgs[i].status != ST_SKIP) n_active++;

    printf(CYAN "Tracking %d packages (%d active | %d skipped)\n\n" NC,
           np, n_active, np - n_active);
    fflush(stdout);

    curl_global_init(CURL_GLOBAL_ALL);
    CURLM *multi = curl_multi_init();
    curl_multi_setopt(multi, CURLMOPT_MAXCONNECTS, (long)PARALLEL);

    int active = 0, done = 0, next = 0;
    for (int i = 0; i < np && active < PARALLEL; i++) {
        if (pkgs[i].status == ST_SKIP || !pkgs[i].url[0]) continue;
        pkgs[i].h = curl_easy_init();
        curl_easy_setopt(pkgs[i].h, CURLOPT_URL, pkgs[i].url);
        curl_easy_setopt(pkgs[i].h, CURLOPT_WRITEFUNCTION, wcb);
        curl_easy_setopt(pkgs[i].h, CURLOPT_WRITEDATA, &pkgs[i].buf);
        curl_easy_setopt(pkgs[i].h, CURLOPT_TIMEOUT, 12L);
        curl_easy_setopt(pkgs[i].h, CURLOPT_PRIVATE, &pkgs[i]);
        curl_easy_setopt(pkgs[i].h, CURLOPT_USERAGENT, "BogartLinux-check-updates/7.0");
        if (pkgs[i].src_type == SRC_GITHUB  ||
            pkgs[i].src_type == SRC_GHTAG   ||
            pkgs[i].src_type == SRC_GHREFTAG)
            add_auth_header(pkgs[i].h, auth_header);
        curl_multi_add_handle(multi, pkgs[i].h);
        pkgs[i].queued = 1; active++; next = i + 1;
    }

    int running = 1;
    while (running || done < n_active) {
        curl_multi_perform(multi, &running);
        CURLMsg *msg; int ml;
        while ((msg = curl_multi_info_read(multi, &ml))) {
            if (msg->msg != CURLMSG_DONE) continue;
            Pkg *pk = NULL;
            curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &pk);
            if (pk) parse_response(pk);
            curl_multi_remove_handle(multi, msg->easy_handle);
            curl_easy_cleanup(msg->easy_handle);
            active--; done++;
            printf("\r" CYAN "Fetching %d/%d" NC, done, n_active); fflush(stdout);
            while (next < np) {
                if (pkgs[next].queued || pkgs[next].status == ST_SKIP ||
                    !pkgs[next].url[0]) { next++; continue; }
                free(pkgs[next].buf.data);
                pkgs[next].buf.data = calloc(1, 1);
                if (!pkgs[next].buf.data) { fprintf(stderr, "OOM\n"); return 1; }
                pkgs[next].buf.size = 0;
                pkgs[next].h = curl_easy_init();
                curl_easy_setopt(pkgs[next].h, CURLOPT_URL, pkgs[next].url);
                curl_easy_setopt(pkgs[next].h, CURLOPT_WRITEFUNCTION, wcb);
                curl_easy_setopt(pkgs[next].h, CURLOPT_WRITEDATA, &pkgs[next].buf);
                curl_easy_setopt(pkgs[next].h, CURLOPT_TIMEOUT, 12L);
                curl_easy_setopt(pkgs[next].h, CURLOPT_PRIVATE, &pkgs[next]);
                curl_easy_setopt(pkgs[next].h, CURLOPT_USERAGENT, "BogartLinux-check-updates/7.0");
                if (pkgs[next].src_type == SRC_GITHUB  ||
                    pkgs[next].src_type == SRC_GHTAG   ||
                    pkgs[next].src_type == SRC_GHREFTAG)
                    add_auth_header(pkgs[next].h, auth_header);
                curl_multi_add_handle(multi, pkgs[next].h);
                pkgs[next].queued = 1; active++; next++; break;
            }
        }
        if (running) {
            struct timeval tv = {0, 50000};
            fd_set r, w, e; int mx = -1;
            FD_ZERO(&r); FD_ZERO(&w); FD_ZERO(&e);
            curl_multi_fdset(multi, &r, &w, &e, &mx);
            if (mx >= 0) select(mx + 1, &r, &w, &e, &tv);
        }
    }
    printf("\r" GREEN "Complete! (%d packages)               \n\n" NC, np);

    /* Clean got values */
    for (int i = 0; i < np; i++) {
        if (pkgs[i].got[0] && strcmp(pkgs[i].got, "NOT_FOUND")) {
            char cleaned[MAX_LEN];
            ver_clean(pkgs[i].got, cleaned, MAX_LEN);
            strncpy(pkgs[i].got, cleaned, MAX_LEN - 1);
        }
    }

    /* Version comparison */
    int n_outdated = 0, n_current = 0, n_ahead = 0, n_notfound = 0, n_skip = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].status == ST_SKIP) { n_skip++; continue; }
        if (!pkgs[i].got[0] || !strcmp(pkgs[i].got, "NOT_FOUND"))
            { pkgs[i].status = ST_NOTFOUND; n_notfound++; continue; }
        int c = ver_cmp(pkgs[i].inst, pkgs[i].got);
        if      (c == 0) { pkgs[i].status = ST_CURRENT;  n_current++; }
        else if (c  > 0) { pkgs[i].status = ST_AHEAD;    n_ahead++;   }
        else             { pkgs[i].status = ST_OUTDATED;  n_outdated++; }
    }

    /* Display */
    printf(CYAN "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" NC);
    printf(RED "%3d outdated" NC " | " GREEN "%3d current" NC " | " BLUE "%3d ahead" NC
           " | " CYAN "%3d not found" NC " | " DIM "%3d skipped\n\n" NC,
           n_outdated, n_current, n_ahead, n_notfound, n_skip);

    int any = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].status != ST_NOTFOUND) continue;
        if (!any) printf(CYAN "NOT FOUND (check sources.h)\n" NC);
        printf(YELLOW "~" NC " %s\n", pkgs[i].name);
        any = 1;
    }
    if (any) printf("\n");

    any = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].status != ST_OUTDATED) continue;
        if (!any) printf(RED "OUTDATED\n" NC);
        printf(YELLOW "%-23s" NC " installed: " RED "%-13s" NC " latest: " GREEN "%s\n" NC,
            pkgs[i].name, pkgs[i].inst, pkgs[i].got);
        any = 1;
    }
    if (!any) printf(GREEN "All current!\n" NC);
    printf("\n");

    curl_multi_cleanup(multi);
    curl_global_cleanup();
    for (int i = 0; i < np; i++) free(pkgs[i].buf.data);
    return 0;
}
