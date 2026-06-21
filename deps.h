#ifndef DEPS_H
#define DEPS_H
#include <stddef.h>
#define MAX_DEPS 64
#define MAX_CASCADE 512
#define TOOLCHAIN_SENTINEL "__WARN_TOOLCHAIN__"
#define DEPS_CONF_DEFAULT "/usr/local/etc/deps.conf"

typedef struct {
    char  *pkg;
    char  *deps[MAX_DEPS];
    int    ndeps;
    int    is_toolchain;
} DepEntry;

int deps_load(const char *path);

void deps_free(void);

int deps_cascade(const char **outdated, char **out, int *out_count);

void deps_print_cascade(const char **outdated, int toolchain_hit);

#endif 
