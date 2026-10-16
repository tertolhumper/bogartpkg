#include <archive.h>
#include <archive_entry.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utime.h>
#include <sys/stat.h>
#include "archdb.h"

#define HSIZE 65536
#define CACHEDIR "/var/cache/check-updates"
typedef struct { char *k, *v; } Ent;
static Ent tab[HSIZE];

static unsigned hsh(const char *s) {
    unsigned x = 5381;
    while (*s) x = x * 33 + (unsigned char)*s++;
    return x & (HSIZE - 1);
}

static void put(const char *k, const char *v, int overwrite) {
    unsigned i = hsh(k);
    while (tab[i].k) {
        if (!strcmp(tab[i].k, k)) {
            if (overwrite) { free(tab[i].v); tab[i].v = strdup(v); }
            return;
        }
        i = (i + 1) & (HSIZE - 1);
    }
    tab[i].k = strdup(k);
    tab[i].v = strdup(v);
}

const char *archdb_ver(const char *n) {
    unsigned i = hsh(n);
    while (tab[i].k) {
        if (!strcmp(tab[i].k, n)) return tab[i].v;
        i = (i + 1) & (HSIZE - 1);
    }
    return NULL;
}

static char *field(const char *buf, const char *tag) {
    char key[32];
    snprintf(key, sizeof key, "%s\n", tag);
    const char *s = strstr(buf, key);
    if (!s) return NULL;
    s += strlen(key);
    const char *e = strchr(s, '\n');
    return strndup(s, e ? (size_t)(e - s) : strlen(s));
}

static void clean(char *v) {
    char *c = strchr(v, ':');
    if (c) memmove(v, c + 1, strlen(c + 1) + 1);
    char *d = strrchr(v, '-');
    if (d) *d = 0;
}

/* 0 if a usable file exists afterwards (fresh, 304, or stale cache) */
static int fetch(const char *url, const char *path) {
    struct stat st;
    long since = stat(path, &st) ? 0 : (long)st.st_mtime;
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return since ? 0 : -1;
    CURL *c = curl_easy_init();
    if (!c) { fclose(f); unlink(tmp); return since ? 0 : -1; }
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_FILETIME, 1L);
    if (since) {
        curl_easy_setopt(c, CURLOPT_TIMECONDITION, (long)CURL_TIMECOND_IFMODSINCE);
        curl_easy_setopt(c, CURLOPT_TIMEVALUE, since);
    }
    CURLcode rc = curl_easy_perform(c);
    long code = 0, ft = -1;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_getinfo(c, CURLINFO_FILETIME, &ft);
    fclose(f);
    curl_easy_cleanup(c);
    if (rc == CURLE_OK && code == 200) {
        rename(tmp, path);
        if (ft > 0) { struct utimbuf u = { ft, ft }; utime(path, &u); }
        return 0;
    }
    unlink(tmp);
    return since ? 0 : -1;
}

static int parse(const char *path) {
    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    if (archive_read_open_filename(a, path, 65536) != ARCHIVE_OK) {
        archive_read_free(a);
        return -1;
    }
    struct archive_entry *e;
    while (archive_read_next_header(a, &e) == ARCHIVE_OK) {
        const char *p = archive_entry_pathname(e);
        size_t n = strlen(p);
        if (n < 5 || strcmp(p + n - 5, "/desc")) { archive_read_data_skip(a); continue; }
        size_t sz = (size_t)archive_entry_size(e);
        char *buf = malloc(sz + 1);
        if (!buf) break;
        ssize_t r = archive_read_data(a, buf, sz);
        buf[r > 0 ? r : 0] = 0;
        char *name = field(buf, "%NAME%");
        char *base = field(buf, "%BASE%");
        char *ver  = field(buf, "%VERSION%");
        if (name && ver) {
            clean(ver);
            put(name, ver, 1);
            if (base && strcmp(base, name)) put(base, ver, 0);
        }
        free(name); free(base); free(ver); free(buf);
    }
    archive_read_free(a);
    return 0;
}

int archdb_load(void) {
    static const char *repos[] = { "core", "extra" };
    int ok = 0;
    mkdir(CACHEDIR, 0755);
    for (int i = 0; i < 2; i++) {
        char url[256], path[256];
        snprintf(url, sizeof url, "https://geo.mirror.pkgbuild.com/%s/os/x86_64/%s.db", repos[i], repos[i]);
        snprintf(path, sizeof path, CACHEDIR "/%s.db", repos[i]);
        if (fetch(url, path) == 0 && parse(path) == 0) ok++;
    }
    return ok ? 0 : -1;
}
