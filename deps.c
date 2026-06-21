#include "deps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#define MAX_ENTRIES 512
#define MAX_LINE    1024

static DepEntry  table[MAX_ENTRIES];
static int       table_size = 0;
static int       loaded     = 0;


static char *xstrdup(const char *s) {
    char *p = strdup(s);
    if (!p) { perror("strdup"); exit(1); }
    return p;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end >= s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

static DepEntry *find_entry(const char *pkg) {
    for (int i = 0; i < table_size; i++)
        if (strcmp(table[i].pkg, pkg) == 0)
            return &table[i];
    return NULL;
}

static int already_seen(char **seen, int n, const char *pkg) {
    for (int i = 0; i < n; i++)
        if (strcmp(seen[i], pkg) == 0)
            return 1;
    return 0;
}


int deps_load(const char *path) {
    if (!path) path = DEPS_CONF_DEFAULT;

    /* Allow override via env */
    const char *env = getenv("DEPS_CONF");
    if (env) path = env;

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "deps: cannot open %s: ", path);
        perror("");
        return -1;
    }

    char line[MAX_LINE];
    int  lineno = 0;

    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char *p = trim(line);

        /* Skip blank lines and comments */
        if (*p == '\0' || *p == '#') continue;

        /* Find the colon separator */
        char *colon = strchr(p, ':');
        if (!colon) {
            fprintf(stderr, "deps: %s:%d: missing colon, skipping\n", path, lineno);
            continue;
        }

        *colon = '\0';
        char *pkg_raw  = trim(p);
        char *deps_raw = trim(colon + 1);

        if (*pkg_raw == '\0') continue;

        if (table_size >= MAX_ENTRIES) {
            fprintf(stderr, "deps: MAX_ENTRIES (%d) exceeded at line %d\n",
                    MAX_ENTRIES, lineno);
            break;
        }

        DepEntry *e = &table[table_size++];
        memset(e, 0, sizeof(*e));
        e->pkg = xstrdup(pkg_raw);

        /* Check for toolchain sentinel */
        if (strcmp(deps_raw, TOOLCHAIN_SENTINEL) == 0) {
            e->is_toolchain = 1;
            continue;
        }

        /* Tokenize space-separated dependents */
        char *tok = strtok(deps_raw, " \t");
        while (tok && e->ndeps < MAX_DEPS) {
            e->deps[e->ndeps++] = xstrdup(tok);
            tok = strtok(NULL, " \t");
        }
    }

    fclose(f);
    loaded = 1;
    return 0;
}

void deps_free(void) {
    for (int i = 0; i < table_size; i++) {
        free(table[i].pkg);
        for (int j = 0; j < table[i].ndeps; j++)
            free(table[i].deps[j]);
    }
    table_size = 0;
    loaded     = 0;
}

int deps_cascade(const char **outdated, char **out, int *out_count) {
    if (!loaded) {
        fprintf(stderr, "deps: call deps_load() before deps_cascade()\n");
        return -1;
    }

    *out_count = 0;
    int toolchain_hit = 0;

    /* BFS queue */
    char  *queue[MAX_CASCADE];
    int    q_head = 0, q_tail = 0;

    /* Seed queue with the outdated packages themselves */
    for (int i = 0; outdated[i] != NULL; i++) {
        if (q_tail < MAX_CASCADE && !already_seen(queue, q_tail, outdated[i]))
            queue[q_tail++] = (char *)outdated[i];
    }

    /* Seen list — pre-populated with triggers so they don't appear in output */
    char *seen[MAX_CASCADE];
    int   n_seen = 0;

    for (int i = 0; outdated[i] != NULL; i++) {
        if (n_seen < MAX_CASCADE)
            seen[n_seen++] = (char *)outdated[i];
    }

    while (q_head < q_tail) {
        const char *pkg = queue[q_head++];
        DepEntry   *e   = find_entry(pkg);
        if (!e) continue;

        if (e->is_toolchain) {
            toolchain_hit = 1;
            continue;
        }

        for (int i = 0; i < e->ndeps; i++) {
            const char *dep = e->deps[i];

            if (!already_seen(seen, n_seen, dep)) {
                /* Guard both seen and output arrays */
                if (n_seen < MAX_CASCADE)
                    seen[n_seen++] = (char *)dep;

                if (*out_count < MAX_CASCADE)
                    out[(*out_count)++] = (char *)dep;

                /* Enqueue for further expansion */
                if (q_tail < MAX_CASCADE)
                    queue[q_tail++] = (char *)dep;
            }
        }
    }

    return toolchain_hit ? 1 : 0;
}

void deps_print_cascade(const char **outdated, int toolchain_hit) {
    /* Toolchain warning */
    if (toolchain_hit) {
        fprintf(stdout,
            "\n"
            "  TOOLCHAIN UPDATE DETECTED\n"
            "  One or more toolchain packages (gcc / glibc / binutils) are outdated.\n"
            "  Do NOT blindly rebuild — scope depends on the type of change:\n"
            "\n"
            "  gcc minor  (15.x → 15.y) : usually nothing, verify changelog\n"
            "  gcc major  (14   → 15)   : rebuild all C++ packages at minimum\n"
            "  glibc, no soname change  : packages using new symbols only\n"
            "  glibc soname bump        : every compiled binary on the system\n"
            "  binutils                 : rarely runtime-breaking, check changelog\n"
            "\n"
            "  Check changelog first, then decide scope before touching anything.\n"
        );
    }

    char *cascade[MAX_CASCADE];
    int   n_cascade = 0;
    int   rc = deps_cascade(outdated, cascade, &n_cascade);
    (void)rc;

    if (n_cascade == 0) return;

    fprintf(stdout,
        "\nREBUILD REQUIRED (dependency cascade)\n");

    for (int i = 0; outdated[i] != NULL; i++) {
        DepEntry *e = find_entry(outdated[i]);
        if (!e || e->is_toolchain || e->ndeps == 0) continue;
        fprintf(stdout, "  %-28s →", outdated[i]);
        for (int j = 0; j < e->ndeps; j++)
            fprintf(stdout, " %s", e->deps[j]);
        fprintf(stdout, "\n");
    }

    fprintf(stdout, "\n  Full rebuild order (%d packages):\n", n_cascade);
    for (int i = 0; i < n_cascade; i++)
        fprintf(stdout, "  %3d. %s\n", i + 1, cascade[i]);
}
