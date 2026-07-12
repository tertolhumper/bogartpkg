#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "catalog.h"
#include "parse.h"

void split_porg(const char *entry, char *name, char *ver, size_t len) {
    name[0] = ver[0] = '\0';
    const char *us = strchr(entry, '_');
    if (us && isdigit(*(us + 1))) {
        size_t nl = us - entry; if (nl >= len) nl = len - 1;
        snprintf(name, MAX_LEN, "%.*s", (int)(nl), entry); name[nl] = '\0';
        snprintf(ver, MAX_LEN, "%.*s", (int)(len - 1), us + 1); return;
    }
    int has_digit = 0;
    for (const char *p = entry; *p; p++)
        if (isdigit((unsigned char)*p)) { has_digit = 1; break; }
    if (!has_digit) { snprintf(name, MAX_LEN, "%.*s", (int)(len - 1), entry); return; }
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
            snprintf(name, MAX_LEN, "%.*s", (int)(nl), entry); name[nl] = '\0';
            snprintf(ver, MAX_LEN, "%.*s", (int)(len - 1), p);
        } else snprintf(name, MAX_LEN, "%.*s", (int)(len - 1), entry);
        return;
    }
    const char *best = NULL;
    for (int i = nc - 1; i >= 0; i--) {
        if (candidates[i] > entry && !isdigit((unsigned char)*(candidates[i] - 1)))
            { best = candidates[i]; break; }
    }
    if (!best) best = candidates[nc - 1];
    size_t nl = best - entry; if (nl >= len) nl = len - 1;
    snprintf(name, MAX_LEN, "%.*s", (int)(nl), entry); name[nl] = '\0';
    const char *vs = best + 1;
    if ((*vs == 'v' || *vs == 'r') && isdigit((unsigned char)*(vs + 1))) vs++;
    snprintf(ver, MAX_LEN, "%.*s", (int)(len - 1), vs);
    char *dash = strrchr(ver, '-');
    if (dash && *(dash + 1)) {
        int all_dig = 1;
        for (char *c = dash + 1; *c; c++)
            if (!isdigit((unsigned char)*c)) { all_dig = 0; break; }
        if (all_dig) *dash = '\0';
        else {
            char first[MAX_LEN]; size_t fl = dash - ver;
            if (fl < MAX_LEN) {
                snprintf(first, MAX_LEN, "%.*s", (int)(fl), ver); first[fl] = '\0';
                if (!strcmp(first, dash + 1)) *dash = '\0';
            }
        }
    }
}

void parse_ver(const char *json, char *out, size_t outlen) {
    out[0] = '\0';
    const char *results = strstr(json, "\"results\"");
    if (!results) { snprintf(out, MAX_LEN, "%.*s", (int)(outlen), "NOT_FOUND"); return; }
    const char *arr = strchr(results, '[');
    if (!arr) { snprintf(out, MAX_LEN, "%.*s", (int)(outlen), "NOT_FOUND"); return; }
    char best_ver[MAX_LEN] = ""; int found_x86 = 0;
    const char *p = arr + 1;
    while (*p && *p != ']') {
        if (*p != '{') { p++; continue; }
        int depth = 0; const char *oe = p;
        while (*oe) {
            if (*oe == '{') depth++;
            else if (*oe == '}') { if (--depth == 0) { oe++; break; } }
            oe++;
        }
        char obj[8192]; size_t ol = oe - p;
        if (ol >= sizeof(obj)) ol = sizeof(obj) - 1;
        snprintf(obj, sizeof(obj), "%.*s", (int)(ol), p); obj[ol] = '\0';
        char ver[MAX_LEN] = "", arc[MAX_LEN] = "";
        const char *vt = strstr(obj, "\"pkgver\"");
        if (vt) { vt = strchr(vt, ':'); if (vt) { vt = strchr(vt, '"'); if (vt) { vt++;
            const char *ve = strchr(vt, '"');
            if (ve) { size_t l = ve - vt; if (l >= MAX_LEN) l = MAX_LEN - 1;
                snprintf(ver, MAX_LEN, "%.*s", (int)(l), vt); ver[l] = '\0'; }}}}
        const char *at = strstr(obj, "\"arch\"");
        if (at) { at = strchr(at, ':'); if (at) { at = strchr(at, '"'); if (at) { at++;
            const char *ae = strchr(at, '"');
            if (ae) { size_t l = ae - at; if (l >= MAX_LEN) l = MAX_LEN - 1;
                snprintf(arc, MAX_LEN, "%.*s", (int)(l), at); arc[l] = '\0'; }}}}
        if (ver[0]) {
            if (!strcmp(arc, "x86_64")) { snprintf(best_ver, MAX_LEN, "%.*s", (int)(MAX_LEN - 1), ver); found_x86 = 1; break; }
            else if (!found_x86 && !best_ver[0]) snprintf(best_ver, MAX_LEN, "%.*s", (int)(MAX_LEN - 1), ver);
        }
        p = oe;
    }
    snprintf(out, MAX_LEN, "%.*s", (int)(outlen), best_ver[0] ? best_ver : "NOT_FOUND");
}

void parse_gh_ver(const char *json, char *out, size_t outlen) {
    out[0] = '\0';
    const char *pr = strstr(json, "\"prerelease\"");
    if (pr) {
        const char *colon = strchr(pr, ':');
        if (colon) {
            while (*colon == ':' || *colon == ' ') colon++;
            if (strncmp(colon, "true", 4) == 0) {
                snprintf(out, MAX_LEN, "%.*s", (int)(outlen), "NOT_FOUND");
                return;
            }
        }
    }
    const char *t = strstr(json, "\"tag_name\"");
    if (!t) { snprintf(out, MAX_LEN, "%.*s", (int)(outlen), "NOT_FOUND"); return; }
    t = strchr(t, ':');
    if (!t) { snprintf(out, MAX_LEN, "%.*s", (int)(outlen), "NOT_FOUND"); return; }
    t = strchr(t, '"');
    if (!t) { snprintf(out, MAX_LEN, "%.*s", (int)(outlen), "NOT_FOUND"); return; }
    t++;
    const char *e = strchr(t, '"');
    if (!e) { snprintf(out, MAX_LEN, "%.*s", (int)(outlen), "NOT_FOUND"); return; }
    size_t l = e - t;
    if (l >= outlen) l = outlen - 1;
    snprintf(out, MAX_LEN, "%.*s", (int)(l), t);
    out[l] = '\0';
    if (out[0] == 'v') memmove(out, out + 1, strlen(out));
}

void parse_gh_tag_ver(const char *json, char *out, size_t outlen, const char *prefix) {
    out[0] = '\0';
    const char *arr = strchr(json, '[');
    if (!arr) { snprintf(out, MAX_LEN, "%.*s", (int)(outlen), "NOT_FOUND"); return; }
    const char *p = arr + 1;
    while (*p && *p != ']') {
        const char *t = strstr(p, "\"name\"");
        if (!t) break;
        t = strchr(t, ':');
        if (!t) break;
        t = strchr(t, '"');
        if (!t) break;
        t++;
        const char *e = strchr(t, '"');
        if (!e) break;
        size_t l = e - t;
        if (l >= outlen) l = outlen - 1;
        char candidate[MAX_LEN];
        snprintf(candidate, MAX_LEN, "%.*s", (int)(l), t);
        candidate[l] = '\0';
        if (!prefix || strncmp(candidate, prefix, strlen(prefix)) == 0) {
            snprintf(out, MAX_LEN, "%.*s", (int)(outlen - 1), candidate);
            if (out[0] == 'v') memmove(out, out + 1, strlen(out));
            return;
        }
        p = e + 1;
    }
    snprintf(out, MAX_LEN, "%.*s", (int)(outlen), "NOT_FOUND");
}

void parse_gh_reftag_ver(const char *json, char *out, size_t outlen, const char *prefix) {
    out[0] = '\0';
    char best[MAX_LEN] = "";
    const char *p = json;
    while (*p) {
        const char *t = strstr(p, "\"refs/tags/");
        if (!t) break;
        t += 11;
        const char *e = strchr(t, '"');
        if (!e) break;
        size_t l = e - t;
        if (l >= MAX_LEN) { p = e + 1; continue; }
        char candidate[MAX_LEN];
        snprintf(candidate, MAX_LEN, "%.*s", (int)(l), t);
        candidate[l] = '\0';
        if (!prefix || strncmp(candidate, prefix, strlen(prefix)) == 0) {
            const char *cv = prefix ? candidate + strlen(prefix) : candidate;
            const char *bv = prefix && best[0] ? best + strlen(prefix) : best;
            if (cv[0] && isdigit((unsigned char)cv[0])) {
                if (!best[0] || ver_cmp(cv, bv) > 0)
                    snprintf(best, MAX_LEN, "%.*s", (int)(MAX_LEN - 1), candidate);
            }
        }
        p = e + 1;
    }
    if (best[0]) {
        snprintf(out, MAX_LEN, "%.*s", (int)(outlen - 1), best);
        out[outlen - 1] = '\0';
    } else {
        snprintf(out, MAX_LEN, "%.*s", (int)(outlen), "NOT_FOUND");
    }
}

void ver_clean(const char *in, char *out, size_t n) {
    snprintf(out, MAX_LEN, "%.*s", (int)(n - 1), in); out[n - 1] = '\0';
    if (strncmp(out, "Release_", 8) == 0) {
        memmove(out, out + 8, strlen(out) - 7);
        for (char *p = out; *p; p++)
            if (*p == '_') *p = '.';
        return;
    }
    if ((out[0] == 'v' || out[0] == 'n') && isdigit((unsigned char)out[1]))
        memmove(out, out + 1, strlen(out));
    if (strncmp(out, "go", 2) == 0 && isdigit((unsigned char)out[2]))
        memmove(out, out + 2, strlen(out) - 1);
    char *c = strchr(out, '+'); if (c) *c = '\0';
    const char *pre_suffixes[] = {"-pre", "-alpha", "-beta", "-rc", "-lts-lgpl", "-lts", NULL};
    for (int i = 0; pre_suffixes[i]; i++) {
        char *s = strstr(out, pre_suffixes[i]);
        if (s) { *s = '\0'; break; }
    }
    char *d = strrchr(out, '-');
    if (d) {
        int ok = 1;
        for (char *x = d + 1; *x; x++)
            if (!isdigit((unsigned char)*x)) { ok = 0; break; }
        if (ok && *(d + 1)) *d = '\0';
    }
}
int ver_cmp(const char *a, const char *b) {
    char ac[MAX_LEN], bc[MAX_LEN];
    ver_clean(a, ac, sizeof(ac)); ver_clean(b, bc, sizeof(bc));
    char av[MAX_LEN], bv[MAX_LEN];
    snprintf(av, MAX_LEN, "%.*s", (int)(MAX_LEN), ac);
    snprintf(bv, MAX_LEN, "%.*s", (int)(MAX_LEN), bc);
    char *ap = av, *bp = bv;
    while (*ap || *bp) {
        char *ad = strchr(ap, '.'), *bd = strchr(bp, '.');
        if (ad) *ad = '\0';
        if (bd) *bd = '\0';
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
