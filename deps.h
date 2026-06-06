#ifndef DEPS_H
#define DEPS_H

#include <stddef.h>

/* Maximum dependents per package */
#define MAX_DEPS 64

/* Maximum packages in a cascade result — sized for full system cascades */
#define MAX_CASCADE 512

/* Special sentinel value in deps.conf */
#define TOOLCHAIN_SENTINEL "__WARN_TOOLCHAIN__"

/* Default config path — override with DEPS_CONF env var */
#define DEPS_CONF_DEFAULT "/usr/local/etc/deps.conf"

/*
 * Loaded dependency map entry.
 * pkg  -> the package whose update triggers rebuilds
 * deps -> NULL-terminated list of packages that must be rebuilt
 */
typedef struct {
    char  *pkg;
    char  *deps[MAX_DEPS];
    int    ndeps;
    int    is_toolchain;   /* 1 if mapped to __WARN_TOOLCHAIN__ */
} DepEntry;

/*
 * deps_load()
 * Parse deps.conf into an internal table.
 * Returns 0 on success, -1 on failure (check stderr).
 * Call once before any lookup.
 */
int deps_load(const char *path);

/*
 * deps_free()
 * Release all memory allocated by deps_load().
 */
void deps_free(void);

/*
 * deps_cascade()
 * Given a NULL-terminated list of outdated packages, walk the
 * dependency map transitively and fill `out` with every package
 * that needs rebuilding (deduplicated, BFS order).
 *
 * Returns:
 *   0  - normal cascade, results in out[] / out_count
 *   1  - toolchain package detected, warning emitted by deps_print_cascade
 *  -1  - error (deps_load not called)
 *
 * out[]      : caller-allocated array of char* (MAX_CASCADE entries)
 * out_count  : number of entries written to out[]
 */
int deps_cascade(const char **outdated, char **out, int *out_count);

/*
 * deps_print_cascade()
 * Pretty-print the cascade result to stdout after the stats block.
 * Shows per-trigger direct deps, numbered full rebuild order,
 * and the toolchain warning if relevant.
 */
void deps_print_cascade(const char **outdated, int toolchain_hit);

#endif /* DEPS_H */
