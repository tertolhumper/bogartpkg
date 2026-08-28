#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <ctype.h>
#include <unistd.h>
#include <stdint.h>
#include "deps.h"
#include "catalog.h"
#include "parse.h"

typedef enum { ST_OUTDATED=0, ST_CURRENT=1, ST_AHEAD=2,
               ST_NOTFOUND=3, ST_SKIP=4 } Status;
typedef struct { char *data; size_t size; } Buf;
typedef struct {
    char     porg[MAX_LEN];
    char     name[MAX_LEN];
    char     arch[MAX_LEN];
    char     inst[MAX_LEN];
    char     got[MAX_LEN];
    Status   status;
    Category cat;
    SrcType  src_type;
    Buf      buf;
    CURL    *h;
    char     url[512];
    char     tag_prefix[32];
    int      queued;
    int      retries;
    struct curl_slist *headers;
} Pkg;

#define MAX_RESPONSE_BYTES (1024U * 1024U)

static int curl_setopt_ok(CURLcode rc, const char *option) {
    if (rc != CURLE_OK) {
        fprintf(stderr, "curl: %s failed: %s\n", option, curl_easy_strerror(rc));
        return 0;
    }
    return 1;
}

static size_t wcb(void *p, size_t sz, size_t n, void *ud) {
    Buf *b = ud;
    if (n != 0 && sz > SIZE_MAX / n) return 0;
    size_t t = sz * n;
    if (t > MAX_RESPONSE_BYTES || b->size > MAX_RESPONSE_BYTES - t - 1)
        return 0;
    char *tmp = realloc(b->data, b->size + t + 1);
    if (!tmp) return 0;
    b->data = tmp;
    memcpy(b->data + b->size, p, t);
    b->size += t;
    b->data[b->size] = '\0';
    return t;
}

static int pkg_exists(Pkg *pkgs, int n, const char *name) {
    for (int i = 0; i < n; i++)
        if (!strcmp(pkgs[i].name, name)) return 1;
    return 0;
}

static struct curl_slist *add_auth_header(CURL *h, const char *auth_header) {
    if (auth_header[0]) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, auth_header);
        if (!headers || !curl_setopt_ok(curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers), "HTTPHEADER")) {
            curl_slist_free_all(headers);
            return NULL;
        }
        return headers;
    }
    return NULL;
}

static int setup_handle(Pkg *pk, const char *auth_header) {
    pk->h = curl_easy_init();
    if (!pk->h) return 0;
    if (!curl_setopt_ok(curl_easy_setopt(pk->h, CURLOPT_URL, pk->url), "URL") ||
        !curl_setopt_ok(curl_easy_setopt(pk->h, CURLOPT_WRITEFUNCTION, wcb), "WRITEFUNCTION") ||
        !curl_setopt_ok(curl_easy_setopt(pk->h, CURLOPT_WRITEDATA, &pk->buf), "WRITEDATA") ||
        !curl_setopt_ok(curl_easy_setopt(pk->h, CURLOPT_TIMEOUT, 12L), "TIMEOUT") ||
        !curl_setopt_ok(curl_easy_setopt(pk->h, CURLOPT_CONNECTTIMEOUT, 5L), "CONNECTTIMEOUT") ||
        !curl_setopt_ok(curl_easy_setopt(pk->h, CURLOPT_PRIVATE, pk), "PRIVATE") ||
        !curl_setopt_ok(curl_easy_setopt(pk->h, CURLOPT_USERAGENT, USERAGENT), "USERAGENT") ||
        !curl_setopt_ok(curl_easy_setopt(pk->h, CURLOPT_ACCEPT_ENCODING, ""), "ACCEPT_ENCODING") ||
        !curl_setopt_ok(curl_easy_setopt(pk->h, CURLOPT_NOSIGNAL, 1L), "NOSIGNAL")) {
        curl_easy_cleanup(pk->h);
        pk->h = NULL;
        return 0;
    }
    if (pk->src_type == SRC_GITHUB || pk->src_type == SRC_GHTAG ||
        pk->src_type == SRC_GHREFTAG) {
        pk->headers = add_auth_header(pk->h, auth_header);
        if (auth_header[0] && !pk->headers) {
            curl_easy_cleanup(pk->h);
            pk->h = NULL;
            return 0;
        }
    }
    return 1;
}

static int retryable_http(long code) {
    return code == 408 || code == 425 || code == 429 || code >= 500;
}

static int retryable_curl(CURLcode code) {
    return code == CURLE_COULDNT_RESOLVE_HOST || code == CURLE_COULDNT_CONNECT ||
           code == CURLE_OPERATION_TIMEDOUT || code == CURLE_RECV_ERROR ||
           code == CURLE_SEND_ERROR;
}

int main(void) {
    if (deps_load(NULL) != 0)
        fprintf(stderr, "warning: dep map unavailable, cascade check skipped\n");

    FILE *tf = fopen("/root/.github_token", "r");
    char token[256] = "";
    char auth_header[280] = "";
    if (tf) {
        fgets(token, sizeof(token), tf);
        fclose(tf);
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
        if (pkg_exists(pkgs, np, name)) continue;
        Category cat = get_category(name);
        snprintf(pkgs[np].porg,  MAX_LEN, "%.*s", (int)(MAX_LEN - 1), line);
        snprintf(pkgs[np].name,  MAX_LEN, "%.*s", (int)(MAX_LEN - 1), name);
        snprintf(pkgs[np].inst,  MAX_LEN, "%.*s", (int)(MAX_LEN - 1), ver);
        snprintf(pkgs[np].arch,  MAX_LEN, "%.*s", (int)(MAX_LEN - 1), arch_name(name));
        pkgs[np].got[0]        = '\0';
        pkgs[np].tag_prefix[0] = '\0';
        pkgs[np].buf.data      = calloc(1, 1);
        if (!pkgs[np].buf.data) { fprintf(stderr, "OOM\n"); return 1; }
        pkgs[np].buf.size = 0;
        pkgs[np].h        = NULL;
        pkgs[np].queued   = 0;
        pkgs[np].retries  = 0;
        pkgs[np].headers  = NULL;
        pkgs[np].cat      = cat;
        pkgs[np].src_type = SRC_SKIP;

        if (cat == CAT_HYPRLAND) {
            const char *repo = gh_repo(name);
            if (repo) {
                pkgs[np].status   = ST_NOTFOUND;
                pkgs[np].src_type = SRC_GITHUB;
                snprintf(pkgs[np].url, sizeof(pkgs[np].url),
                    "https://api.github.com/repos/%s/releases/latest", repo);
            } else {
                pkgs[np].status = ST_SKIP;
            }
        } else if (cat == CAT_BLFS) {
            const BlfsSrc *bs = blfs_src_lookup(name);
            if (!bs || bs->src == SRC_SKIP) {
                pkgs[np].status   = ST_SKIP;
                pkgs[np].src_type = SRC_SKIP;
            } else {
                pkgs[np].status   = ST_NOTFOUND;
                pkgs[np].src_type = bs->src;
                if (bs->prefix)
                    snprintf(pkgs[np].tag_prefix, sizeof(pkgs[np].tag_prefix), "%s", bs->prefix);
                if (bs->src == SRC_ARCH) {
                    const char *apkg = bs->repo ? bs->repo : name;
                    snprintf(pkgs[np].arch, MAX_LEN, "%.*s", (int)(MAX_LEN - 1), apkg);
                    snprintf(pkgs[np].url, sizeof(pkgs[np].url),
                        "https://archlinux.org/packages/search/json/?name=%s", apkg);
                } else if (bs->src == SRC_GITHUB) {
                    snprintf(pkgs[np].url, sizeof(pkgs[np].url),
                        "https://api.github.com/repos/%s/releases/latest", bs->repo);
                } else if (bs->src == SRC_GHTAG) {
                    snprintf(pkgs[np].url, sizeof(pkgs[np].url),
                        "https://api.github.com/repos/%s/tags", bs->repo);
                } else if (bs->src == SRC_GHREFTAG) {
                    snprintf(pkgs[np].url, sizeof(pkgs[np].url),
                        "https://api.github.com/repos/%s/git/refs/tags", bs->repo);
                }
            }
        } else if (cat == CAT_TRACK) {
            pkgs[np].status   = ST_NOTFOUND;
            pkgs[np].src_type = SRC_ARCH;
            char arch_query[MAX_LEN];
            snprintf(arch_query, sizeof(arch_query), "%s", pkgs[np].arch);
            snprintf(pkgs[np].url, sizeof(pkgs[np].url),
                "https://archlinux.org/packages/search/json/?name=%s",
                arch_query);
        } else {
            pkgs[np].status   = ST_SKIP;
            pkgs[np].src_type = SRC_SKIP;
            pkgs[np].url[0]   = '\0';
        }
        np++;
    }
    pclose(f);

    int n_track = 0, n_hypr = 0, n_blfs = 0, n_skip = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].cat == CAT_TRACK) n_track++;
        if (pkgs[i].cat == CAT_HYPRLAND && pkgs[i].status != ST_SKIP) n_hypr++;
        if (pkgs[i].cat == CAT_BLFS && pkgs[i].status != ST_SKIP) n_blfs++;
        if (pkgs[i].cat == CAT_LFS || pkgs[i].cat == CAT_XORG ||
            pkgs[i].cat == CAT_TOOLCHAIN || pkgs[i].cat == CAT_NOISE) n_skip++;
    }
    printf(CYAN "Tracking %d Arch | %d Hyprland | %d BLFS Packages \n\n" NC,
        n_track, n_hypr, n_blfs);
    fflush(stdout);

    if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
        fprintf(stderr, "curl: global initialization failed\n");
        for (int i = 0; i < np; i++) free(pkgs[i].buf.data);
        deps_free();
        return 1;
    }
    CURLM *multi = curl_multi_init();
    if (!multi) {
        fprintf(stderr, "curl: multi initialization failed\n");
        curl_global_cleanup();
        for (int i = 0; i < np; i++) free(pkgs[i].buf.data);
        deps_free();
        return 1;
    }
    curl_multi_setopt(multi, CURLMOPT_MAXCONNECTS, (long)PARALLEL);
    curl_multi_setopt(multi, CURLMOPT_MAX_TOTAL_CONNECTIONS, (long)PARALLEL);
    curl_multi_setopt(multi, CURLMOPT_MAX_HOST_CONNECTIONS, 2L);

    int n_total = n_track + n_hypr + n_blfs;
    int active = 0, done = 0, next = 0;
    for (int i = 0; i < np && active < PARALLEL; i++) {
        if (pkgs[i].status == ST_SKIP) continue;
        if (pkgs[i].cat != CAT_TRACK && pkgs[i].cat != CAT_HYPRLAND && pkgs[i].cat != CAT_BLFS) continue;
        if (!setup_handle(&pkgs[i], auth_header) ||
            curl_multi_add_handle(multi, pkgs[i].h) != CURLM_OK) {
            fprintf(stderr, "curl: could not queue %s\n", pkgs[i].name);
            if (pkgs[i].h) curl_easy_cleanup(pkgs[i].h);
            curl_slist_free_all(pkgs[i].headers);
            pkgs[i].headers = NULL;
            pkgs[i].h = NULL;
            pkgs[i].status = ST_NOTFOUND;
            done++;
            continue;
        }
        pkgs[i].queued = 1; active++; next = i + 1;
    }

    int running = 1;
    while (running || done < n_total) {
        CURLMcode mrc;
        do { mrc = curl_multi_perform(multi, &running); } while (mrc == CURLM_CALL_MULTI_PERFORM);
        if (mrc != CURLM_OK) {
            fprintf(stderr, "curl: multi perform failed: %s\n", curl_multi_strerror(mrc));
            break;
        }
        CURLMsg *msg; int ml;
        while ((msg = curl_multi_info_read(multi, &ml))) {
            if (msg->msg != CURLMSG_DONE) continue;
            Pkg *pk = NULL;
            curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &pk);
            if (pk) {
                long http_code = 0;
                curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &http_code);
                if ((retryable_http(http_code) || retryable_curl(msg->data.result)) && pk->retries < 3) {
                    pk->retries++;
                    free(pk->buf.data);
                    pk->buf.data = calloc(1, 1);
                    if (!pk->buf.data) {
                        fprintf(stderr, "OOM while retrying %s\n", pk->name);
                        pk->buf.size = 0;
                        pk->retries = 3;
                    }
                    pk->buf.size = 0;
                    curl_multi_remove_handle(multi, msg->easy_handle);
                    if (pk->buf.data) {
                        sleep((unsigned int)(1U << pk->retries));
                        if (curl_multi_add_handle(multi, msg->easy_handle) == CURLM_OK)
                            continue;
                        fprintf(stderr, "curl: could not requeue %s\n", pk->name);
                    }
                }
                if (msg->data.result != CURLE_OK || http_code < 200 || http_code >= 300) {
                    snprintf(pk->got, MAX_LEN, "NOT_FOUND");
                } else if (pk->src_type == SRC_ARCH)
                    parse_ver(pk->buf.data, pk->got, MAX_LEN);
                else if (pk->src_type == SRC_GHTAG)
                    parse_gh_tag_ver(pk->buf.data, pk->got, MAX_LEN,
                        pk->tag_prefix[0] ? pk->tag_prefix : NULL);
                else if (pk->src_type == SRC_GHREFTAG)
                    parse_gh_reftag_ver(pk->buf.data, pk->got, MAX_LEN,
                        pk->tag_prefix[0] ? pk->tag_prefix : NULL);
                else
                    parse_gh_ver(pk->buf.data, pk->got, MAX_LEN);
            }
            curl_multi_remove_handle(multi, msg->easy_handle);
            curl_easy_cleanup(msg->easy_handle);
            if (pk && pk->headers) {
                curl_slist_free_all(pk->headers);
                pk->headers = NULL;
            }
            active--; done++;
            printf("\r" CYAN "Fetching %d/%d" NC, done, n_total); fflush(stdout);
            while (next < np) {
                if (pkgs[next].queued || pkgs[next].status == ST_SKIP ||
                    (pkgs[next].cat != CAT_TRACK && pkgs[next].cat != CAT_HYPRLAND &&
                     pkgs[next].cat != CAT_BLFS))
                    { next++; continue; }
                free(pkgs[next].buf.data);
                pkgs[next].buf.data = calloc(1, 1);
                if (!pkgs[next].buf.data) { fprintf(stderr, "OOM\n"); return 1; }
                pkgs[next].buf.size = 0;
                pkgs[next].headers  = NULL;
                if (!setup_handle(&pkgs[next], auth_header) ||
                    curl_multi_add_handle(multi, pkgs[next].h) != CURLM_OK) {
                    fprintf(stderr, "curl: could not queue %s\n", pkgs[next].name);
                    if (pkgs[next].h) curl_easy_cleanup(pkgs[next].h);
                    curl_slist_free_all(pkgs[next].headers);
                    pkgs[next].headers = NULL;
                    pkgs[next].h = NULL;
                    pkgs[next].status = ST_NOTFOUND;
                    done++;
                    next++;
                    continue;
                }
                pkgs[next].queued = 1; active++; next++; break;
            }
        }
        if (running) {
            int numfds = 0;
            curl_multi_wait(multi, NULL, 0, 50, &numfds);
        }
    }
    printf("\r" GREEN "Complete! (%d packages)\n\n" NC, n_total);

    for (int i = 0; i < np; i++) {
        if (pkgs[i].got[0] && strcmp(pkgs[i].got, "NOT_FOUND")) {
            char cleaned[MAX_LEN];
            ver_clean(pkgs[i].got, cleaned, MAX_LEN);
            snprintf(pkgs[i].got, MAX_LEN, "%.*s", (int)(MAX_LEN - 1), cleaned);
        }
    }

    int no = 0, nc_count = 0, na = 0, nn = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].cat != CAT_TRACK) continue;
        if (!pkgs[i].got[0] || !strcmp(pkgs[i].got, "NOT_FOUND"))
            { pkgs[i].status = ST_NOTFOUND; nn++; continue; }
        int c = ver_cmp(pkgs[i].inst, pkgs[i].got);
        if      (c == 0) { pkgs[i].status = ST_CURRENT;  nc_count++; }
        else if (c  > 0) { pkgs[i].status = ST_AHEAD;    na++; }
        else             { pkgs[i].status = ST_OUTDATED;  no++; }
    }
    int hno = 0, hnc = 0, hna = 0, hnn = 0, hskip = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].cat != CAT_HYPRLAND) continue;
        if (pkgs[i].status == ST_SKIP) { hskip++; continue; }
        if (!pkgs[i].got[0] || !strcmp(pkgs[i].got, "NOT_FOUND"))
            { pkgs[i].status = ST_NOTFOUND; hnn++; continue; }
        int c = ver_cmp(pkgs[i].inst, pkgs[i].got);
        if      (c == 0) { pkgs[i].status = ST_CURRENT;  hnc++; }
        else if (c  > 0) { pkgs[i].status = ST_AHEAD;    hna++; }
        else             { pkgs[i].status = ST_OUTDATED;  hno++; }
    }
    int bno = 0, bnc = 0, bna = 0, bnn = 0, bskip = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].cat != CAT_BLFS) continue;
        if (pkgs[i].status == ST_SKIP) { bskip++; continue; }
        if (!pkgs[i].got[0] || !strcmp(pkgs[i].got, "NOT_FOUND"))
            { pkgs[i].status = ST_NOTFOUND; bnn++; continue; }
        int c = ver_cmp(pkgs[i].inst, pkgs[i].got);
        if      (c == 0) { pkgs[i].status = ST_CURRENT;  bnc++; }
        else if (c  > 0) { pkgs[i].status = ST_AHEAD;    bna++; }
        else             { pkgs[i].status = ST_OUTDATED;  bno++; }
    }

    printf("\n" CYAN BOLD "Package(s) not found Arch Repo API (Needs ARCHMAP/SKIP)\n" NC);
    int any1 = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].status != ST_NOTFOUND || pkgs[i].cat != CAT_TRACK) continue;
        printf(YELLOW "~" NC " %-20s (queried: %s)\n", pkgs[i].name, pkgs[i].arch);
        any1 = 1;
    }
    if (!any1) printf(GREEN "None\n" NC);
    printf("\n" CYAN "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" NC);
    printf("Arch:     " RED "%3d outdated" NC " | " GREEN "%3d current" NC " | " BLUE "%3d ahead" NC
           " | " CYAN "%3d not found" NC " | " DIM "%3d skipped\n" NC,
           no, nc_count, na, nn, 0);
    printf("Hyprland: " RED "%3d outdated" NC " | " GREEN "%3d current" NC " | " BLUE "%3d ahead" NC
           " | " CYAN "%3d not found" NC " | " DIM "%3d skipped\n" NC,
           hno, hnc, hna, hnn, hskip);
    printf("BLFS:     " RED "%3d outdated" NC " | " GREEN "%3d current" NC " | " BLUE "%3d ahead" NC
           " | " CYAN "%3d not found" NC " | " DIM "%3d skipped\n\n" NC,
           bno, bnc, bna, bnn, bskip);
    printf("Skipped:  " DIM "%3d (Xorg/Toolchain/EOL)\n" NC, n_skip);
    printf("Total:    " BOLD GREEN "%3d packages\n\n" NC, np);
    printf(RED "OUTDATED\n" NC);
    int any = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].status != ST_OUTDATED || pkgs[i].cat != CAT_TRACK) continue;
        printf(YELLOW "%-23s" NC " installed: " RED "%-13s" NC " arch: " GREEN "%s\n" NC,
            pkgs[i].name, pkgs[i].inst, pkgs[i].got);
        any = 1;
    }
    if (!any) printf(GREEN "All current!\n" NC);
    int hany = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].status != ST_OUTDATED || pkgs[i].cat != CAT_HYPRLAND) continue;
        if (!hany) printf(RED "Hyprland\n" NC);
        printf(YELLOW "%-23s" NC " installed: " RED "%-13s" NC " github: " GREEN "%s\n" NC,
            pkgs[i].name, pkgs[i].inst, pkgs[i].got);
        hany = 1;
    }
    int bany = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].status != ST_OUTDATED || pkgs[i].cat != CAT_BLFS) continue;
        if (!bany) printf(RED "BLFS\n" NC);
        const char *src_label =
            pkgs[i].src_type == SRC_ARCH     ? "arch"   :
            pkgs[i].src_type == SRC_GITHUB   ? "github" :
            pkgs[i].src_type == SRC_GHTAG    ? "github" :
            pkgs[i].src_type == SRC_GHREFTAG ? "github" :
            pkgs[i].src_type == SRC_GITLAB   ? "gitlab" : "?";
        printf(YELLOW "%-23s" NC " installed: " RED "%-13s" NC " %s: " GREEN "%s\n" NC,
            pkgs[i].name, pkgs[i].inst, src_label, pkgs[i].got);
        bany = 1;
    }
    printf("\n");
    curl_multi_cleanup(multi);
    curl_global_cleanup();
    for (int i = 0; i < np; i++) {
        free(pkgs[i].buf.data);
        if (pkgs[i].headers)
            curl_slist_free_all(pkgs[i].headers);
    }
    deps_free();
    return 0;
}
