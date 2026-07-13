#ifndef SCAN_H
#define SCAN_H

#include "graph.h"

typedef struct {
    char **files;
    int    count;
    int    cap;
} FileList;

void strip_version(const char *porg_name, char *out, int outlen);
void filelist_add(FileList *fl, const char *path);
void filelist_free(FileList *fl);
int  filelist_from_porg(const char *porg_name, FileList *fl);
int  is_regular_elf(const char *path);
void readelf_scan(const char *path, char soname_out[MAX_SONAME],
                  char ***needed_out, int *needed_count);
void scan_package(Graph *g, const char *porg_name);

#endif
