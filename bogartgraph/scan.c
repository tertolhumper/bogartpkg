#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "scan.h"

void strip_version(const char *porg_name, char *out, int outlen) {
    strncpy(out, porg_name, outlen - 1);
    out[outlen - 1] = '\0';
    for (char *p = out; *p; p++) {
        if (*p == '-' && *(p + 1) >= '0' && *(p + 1) <= '9') {
            *p = '\0';
            return;
        }
    }
}

void filelist_add(FileList *fl, const char *path) {
    if (fl->count >= fl->cap) {
        fl->cap   = fl->cap ? fl->cap * 2 : 256;
        fl->files = realloc(fl->files, fl->cap * sizeof(char *));
    }
    fl->files[fl->count++] = strdup(path);
}

void filelist_free(FileList *fl) {
    for (int i = 0; i < fl->count; i++) free(fl->files[i]);
    free(fl->files);
    memset(fl, 0, sizeof(*fl));
}

int filelist_from_porg(const char *porg_name, FileList *fl) {
    char cmd[MAX_PKGNAME * 2 + 32];
    snprintf(cmd, sizeof(cmd), "porg -f '%s' 2>/dev/null", porg_name);
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    char line[MAX_LINE];
    int  first = 1;
    while (fgets(line, sizeof(line), fp)) {
        if (first) { first = 0; continue; }
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        if (len == 0) continue;
        filelist_add(fl, line);
    }
    pclose(fp);
    return 0;
}

int is_regular_elf(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0)   return 0;
    if (S_ISLNK(st.st_mode))     return 0;
    if (!S_ISREG(st.st_mode))    return 0;
    FILE *f = fopen(path, "rb");
    if (!f)                       return 0;
    unsigned char magic[4];
    int ok = (fread(magic, 1, 4, f) == 4 &&
              magic[0] == 0x7f && magic[1] == 'E' &&
              magic[2] == 'L'  && magic[3] == 'F');
    fclose(f);
    return ok;
}

void readelf_scan(const char *path,
                  char soname_out[MAX_SONAME],
                  char ***needed_out, int *needed_count)
{
    soname_out[0] = '\0';
    *needed_out   = NULL;
    *needed_count = 0;

    char cmd[MAX_LINE];
    snprintf(cmd, sizeof(cmd), "readelf -d '%s' 2>/dev/null", path);
    FILE *fp = popen(cmd, "r");
    if (!fp) return;

    int    needed_cap = 32;
    char **needed     = malloc(needed_cap * sizeof(char *));

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp)) {
        char *tag_start = strchr(line, '(');
        char *tag_end   = strchr(line, ')');
        if (!tag_start || !tag_end || tag_end <= tag_start) continue;

        int  taglen = (int)(tag_end - tag_start - 1);
        char tag[32];
        if (taglen <= 0 || taglen >= (int)sizeof(tag)) continue;
        strncpy(tag, tag_start + 1, taglen);
        tag[taglen] = '\0';

        int is_soname = (strcmp(tag, "SONAME") == 0);
        int is_needed = (strcmp(tag, "NEEDED") == 0);
        if (!is_soname && !is_needed) continue;

        char *lb = strchr(line, '[');
        char *rb = strchr(line, ']');
        if (!lb || !rb || rb <= lb) continue;
        int vlen = (int)(rb - lb - 1);
        if (vlen <= 0 || vlen >= MAX_SONAME) continue;

        char value[MAX_SONAME];
        strncpy(value, lb + 1, vlen);
        value[vlen] = '\0';

        if (is_soname) {
            strncpy(soname_out, value, MAX_SONAME - 1);
            soname_out[MAX_SONAME - 1] = '\0';
        } else {
            if (*needed_count >= needed_cap) {
                needed_cap *= 2;
                needed      = realloc(needed, needed_cap * sizeof(char *));
            }
            needed[(*needed_count)++] = strdup(value);
        }
    }
    pclose(fp);
    *needed_out = needed;
}

void scan_package(Graph *g, const char *porg_name) {
    char pkg[MAX_PKGNAME];
    strip_version(porg_name, pkg, sizeof(pkg));

    FileList fl;
    memset(&fl, 0, sizeof(fl));
    if (filelist_from_porg(porg_name, &fl) != 0) return;

    for (int i = 0; i < fl.count; i++) {
        const char *path = fl.files[i];
        if (!is_regular_elf(path)) continue;

        char   soname[MAX_SONAME];
        char **needed       = NULL;
        int    needed_count = 0;

        readelf_scan(path, soname, &needed, &needed_count);

        if (soname[0] != '\0')
            if (!graph_has(g, ENTRY_PROVIDES, pkg, soname))
                graph_add(g, ENTRY_PROVIDES, pkg, soname);

        for (int j = 0; j < needed_count; j++) {
            if (!graph_has(g, ENTRY_NEEDS, pkg, needed[j]))
                graph_add(g, ENTRY_NEEDS, pkg, needed[j]);
            free(needed[j]);
        }
        free(needed);
    }
    filelist_free(&fl);
}
