#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "graph.h"
#include "scan.h"

#define VSET_SIZE (MAX_ENTRIES * 2)

typedef struct {
    char keys[VSET_SIZE][MAX_PKGNAME];
    int  used[VSET_SIZE];
} VSet;

static void vset_init(VSet *v) {
    memset(v->used, 0, sizeof(v->used));
}

static unsigned int vset_hash(const char *s) {
    unsigned int h = 5381;
    while (*s) h = h * 31 + (unsigned char)*s++;
    return h % VSET_SIZE;
}

static int vset_contains(VSet *v, const char *key) {
    unsigned int h = vset_hash(key);
    unsigned int i = h;
    do {
        if (!v->used[i]) return 0;
        if (strcmp(v->keys[i], key) == 0) return 1;
        i = (i + 1) % VSET_SIZE;
    } while (i != h);
    return 0;
}

static void vset_insert(VSet *v, const char *key) {
    unsigned int h = vset_hash(key);
    unsigned int i = h;
    do {
        if (!v->used[i]) {
            v->used[i] = 1;
            strncpy(v->keys[i], key, MAX_PKGNAME - 1);
            v->keys[i][MAX_PKGNAME - 1] = '\0';
            return;
        }
        if (strcmp(v->keys[i], key) == 0) return;
        i = (i + 1) % VSET_SIZE;
    } while (i != h);
}

typedef struct {
    char pkg[MAX_PKGNAME];
    char soname[MAX_SONAME];
} Edge;

typedef struct {
    Edge *edges;
    int   count;
    int   cap;
} AdjList;

static void adj_push(AdjList *a, const char *pkg, const char *soname) {
    if (a->count >= a->cap) {
        a->cap   = a->cap ? a->cap * 2 : 8;
        a->edges = realloc(a->edges, a->cap * sizeof(Edge));
    }
    strncpy(a->edges[a->count].pkg,    pkg,    MAX_PKGNAME - 1);
    strncpy(a->edges[a->count].soname, soname, MAX_SONAME  - 1);
    a->edges[a->count].pkg[MAX_PKGNAME - 1]    = '\0';
    a->edges[a->count].soname[MAX_SONAME  - 1] = '\0';
    a->count++;
}

typedef struct {
    char    soname[MAX_SONAME];
    AdjList needers;
} SonameEntry;

typedef struct {
    SonameEntry *entries;
    int          count;
    int          cap;
} SonameMap;

static SonameMap *somap_build(Graph *g) {
    SonameMap *m = calloc(1, sizeof(SonameMap));
    for (int i = 0; i < g->count; i++) {
        GraphEntry *e = &g->entries[i];
        if (e->type != ENTRY_NEEDS) continue;
        int idx = -1;
        for (int k = 0; k < m->count; k++)
            if (strcmp(m->entries[k].soname, e->soname) == 0) { idx = k; break; }
        if (idx < 0) {
            if (m->count >= m->cap) {
                m->cap     = m->cap ? m->cap * 2 : 64;
                m->entries = realloc(m->entries, m->cap * sizeof(SonameEntry));
            }
            idx = m->count++;
            strncpy(m->entries[idx].soname, e->soname, MAX_SONAME - 1);
            m->entries[idx].soname[MAX_SONAME - 1] = '\0';
            memset(&m->entries[idx].needers, 0, sizeof(AdjList));
        }
        adj_push(&m->entries[idx].needers, e->pkg, e->soname);
    }
    return m;
}

static void somap_free(SonameMap *m) {
    for (int i = 0; i < m->count; i++) free(m->entries[i].needers.edges);
    free(m->entries);
    free(m);
}

static void collect_cascade(Graph *g, SonameMap *sm, const char *root,
                             char out[][MAX_PKGNAME], int *out_count, int max_out)
{
    char queue[MAX_ENTRIES][MAX_PKGNAME];
    int  qhead = 0, qtail = 0;
    VSet visited;
    vset_init(&visited);

    strncpy(queue[qtail], root, MAX_PKGNAME - 1);
    queue[qtail++][MAX_PKGNAME - 1] = '\0';

    while (qhead < qtail) {
        char cur[MAX_PKGNAME];
        strncpy(cur, queue[qhead++], MAX_PKGNAME - 1);
        cur[MAX_PKGNAME - 1] = '\0';

        if (vset_contains(&visited, cur)) continue;
        vset_insert(&visited, cur);

        if (strcmp(cur, root) != 0 && *out_count < max_out) {
            strncpy(out[*out_count], cur, MAX_PKGNAME - 1);
            out[(*out_count)++][MAX_PKGNAME - 1] = '\0';
        }

        for (int i = 0; i < g->count; i++) {
            GraphEntry *e = &g->entries[i];
            if (e->type != ENTRY_PROVIDES || strcmp(e->pkg, cur) != 0) continue;
            for (int s = 0; s < sm->count; s++) {
                if (strcmp(sm->entries[s].soname, e->soname) != 0) continue;
                AdjList *nl = &sm->entries[s].needers;
                for (int k = 0; k < nl->count; k++) {
                    if (strcmp(nl->edges[k].pkg, cur) == 0) continue;
                    if (!vset_contains(&visited, nl->edges[k].pkg) && qtail < MAX_ENTRIES) {
                        strncpy(queue[qtail], nl->edges[k].pkg, MAX_PKGNAME - 1);
                        queue[qtail++][MAX_PKGNAME - 1] = '\0';
                    }
                }
                break;
            }
        }
    }
}

static void topo_sort(Graph *g, SonameMap *sm,
                      char nodes[][MAX_PKGNAME], int ncount,
                      char sorted[][MAX_PKGNAME], int *scount)
{
    int  *in_degree  = calloc(ncount, sizeof(int));
    int **edges      = calloc(ncount, sizeof(int *));
    int  *edge_count = calloc(ncount, sizeof(int));
    int  *edge_cap   = calloc(ncount, sizeof(int));

    for (int i = 0; i < ncount; i++) {
        for (int ei = 0; ei < g->count; ei++) {
            GraphEntry *e = &g->entries[ei];
            if (e->type != ENTRY_PROVIDES || strcmp(e->pkg, nodes[i]) != 0) continue;
            for (int s = 0; s < sm->count; s++) {
                if (strcmp(sm->entries[s].soname, e->soname) != 0) continue;
                AdjList *nl = &sm->entries[s].needers;
                for (int k = 0; k < nl->count; k++) {
                    for (int j = 0; j < ncount; j++) {
                        if (i == j) continue;
                        if (strcmp(nl->edges[k].pkg, nodes[j]) != 0) continue;
                        if (edge_count[i] >= edge_cap[i]) {
                            edge_cap[i] = edge_cap[i] ? edge_cap[i] * 2 : 4;
                            edges[i]    = realloc(edges[i], edge_cap[i] * sizeof(int));
                        }
                        edges[i][edge_count[i]++] = j;
                        in_degree[j]++;
                        break;
                    }
                }
                break;
            }
        }
    }

    int *queue = malloc(ncount * sizeof(int));
    int  qh = 0, qt = 0;
    for (int i = 0; i < ncount; i++)
        if (in_degree[i] == 0) queue[qt++] = i;

    *scount = 0;
    while (qh < qt) {
        int n = queue[qh++];
        strncpy(sorted[*scount], nodes[n], MAX_PKGNAME - 1);
        sorted[(*scount)++][MAX_PKGNAME - 1] = '\0';
        for (int k = 0; k < edge_count[n]; k++) {
            int m = edges[n][k];
            if (--in_degree[m] == 0) queue[qt++] = m;
        }
    }

    VSet seen;
    vset_init(&seen);
    for (int i = 0; i < *scount; i++) vset_insert(&seen, sorted[i]);
    for (int i = 0; i < ncount; i++) {
        if (!vset_contains(&seen, nodes[i])) {
            strncpy(sorted[*scount], nodes[i], MAX_PKGNAME - 1);
            sorted[(*scount)++][MAX_PKGNAME - 1] = '\0';
        }
    }

    for (int i = 0; i < ncount; i++) free(edges[i]);
    free(edges);
    free(edge_count);
    free(edge_cap);
    free(in_degree);
    free(queue);
}

static void cmd_rebuild(void) {
    DIR *d = opendir(PORG_DB);
    if (!d) { perror("bogartgraph"); exit(1); }

    char **pnames = malloc(512 * sizeof(char *));
    int    pcount = 0, pcap = 512;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (pcount >= pcap) {
            pcap  *= 2;
            pnames = realloc(pnames, pcap * sizeof(char *));
        }
        pnames[pcount++] = strdup(ent->d_name);
    }
    closedir(d);

    Graph *g = graph_new();
    printf("bogartgraph: scanning %d packages...\n", pcount);
    for (int i = 0; i < pcount; i++) {
        printf("\r  [%d/%d] %-40s", i + 1, pcount, pnames[i]);
        fflush(stdout);
        scan_package(g, pnames[i]);
        free(pnames[i]);
    }
    free(pnames);
    printf("\nbogartgraph: %d entries\n", g->count);
    if (graph_write(g, GRAPH_CACHE) == 0)
        printf("bogartgraph: cache written to %s\n", GRAPH_CACHE);
    graph_free(g);
}

static void cmd_rescan(const char *pkgname) {
    DIR *d = opendir(PORG_DB);
    if (!d) { perror("bogartgraph"); exit(1); }

    char porg_name[MAX_PKGNAME * 2] = "";
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        char stripped[MAX_PKGNAME];
        strip_version(ent->d_name, stripped, sizeof(stripped));
        if (strcmp(stripped, pkgname) == 0) {
            strncpy(porg_name, ent->d_name, sizeof(porg_name) - 1);
            porg_name[sizeof(porg_name) - 1] = '\0';
            break;
        }
    }
    closedir(d);

    if (porg_name[0] == '\0') {
        fprintf(stderr, "bogartgraph: '%s' not found in porg db\n", pkgname);
        exit(1);
    }

    Graph *g = graph_load(GRAPH_CACHE);
    if (!g) g = graph_new();

    int new_count = 0;
    for (int i = 0; i < g->count; i++)
        if (strcmp(g->entries[i].pkg, pkgname) != 0)
            g->entries[new_count++] = g->entries[i];
    g->count = new_count;

    printf("bogartgraph: rescanning %s (%s)...\n", pkgname, porg_name);
    scan_package(g, porg_name);
    printf("bogartgraph: done\n");
    graph_write(g, GRAPH_CACHE);
    graph_free(g);
}

static void cmd_needs(const char *pkgname) {
    Graph *g = graph_load(GRAPH_CACHE);
    if (!g) { fprintf(stderr, "bogartgraph: no cache\n"); exit(1); }
    printf("Needs (%s):\n", pkgname);
    int found = 0;
    for (int i = 0; i < g->count; i++) {
        GraphEntry *e = &g->entries[i];
        if (e->type == ENTRY_NEEDS && strcmp(e->pkg, pkgname) == 0) {
            printf("  %s\n", e->soname); found++;
        }
    }
    if (!found) printf("  (none)\n");
    graph_free(g);
}

static void cmd_provides(const char *pkgname) {
    Graph *g = graph_load(GRAPH_CACHE);
    if (!g) { fprintf(stderr, "bogartgraph: no cache\n"); exit(1); }
    printf("Provides (%s):\n", pkgname);
    int found = 0;
    for (int i = 0; i < g->count; i++) {
        GraphEntry *e = &g->entries[i];
        if (e->type == ENTRY_PROVIDES && strcmp(e->pkg, pkgname) == 0) {
            printf("  %s\n", e->soname); found++;
        }
    }
    if (!found) printf("  (none)\n");
    graph_free(g);
}

static void cmd_rdeps(const char *pkgname) {
    Graph *g = graph_load(GRAPH_CACHE);
    if (!g) { fprintf(stderr, "bogartgraph: no cache\n"); exit(1); }

    char provided[256][MAX_SONAME];
    int  nprovided = 0;
    for (int i = 0; i < g->count; i++) {
        GraphEntry *e = &g->entries[i];
        if (e->type == ENTRY_PROVIDES && strcmp(e->pkg, pkgname) == 0 && nprovided < 256) {
            strncpy(provided[nprovided], e->soname, MAX_SONAME - 1);
            provided[nprovided++][MAX_SONAME - 1] = '\0';
        }
    }
    if (nprovided == 0) {
        printf("No sonames provided by %s\n", pkgname);
        graph_free(g); return;
    }

    char rdeps[MAX_ENTRIES][MAX_PKGNAME];
    int  nrdeps = 0;
    VSet seen;
    vset_init(&seen);
    for (int i = 0; i < g->count; i++) {
        GraphEntry *e = &g->entries[i];
        if (e->type != ENTRY_NEEDS || strcmp(e->pkg, pkgname) == 0) continue;
        for (int j = 0; j < nprovided; j++) {
            if (strcmp(e->soname, provided[j]) != 0) continue;
            if (!vset_contains(&seen, e->pkg) && nrdeps < MAX_ENTRIES) {
                vset_insert(&seen, e->pkg);
                strncpy(rdeps[nrdeps], e->pkg, MAX_PKGNAME - 1);
                rdeps[nrdeps++][MAX_PKGNAME - 1] = '\0';
            }
            break;
        }
    }

    printf("Packages depending on %s (%d):\n", pkgname, nrdeps);
    for (int i = 0; i < nrdeps; i++) printf("  %s\n", rdeps[i]);
    graph_free(g);
}

static void cmd_cascade(const char *pkgname) {
    Graph *g = graph_load(GRAPH_CACHE);
    if (!g) { fprintf(stderr, "bogartgraph: no cache\n"); exit(1); }

    SonameMap *sm = somap_build(g);

    static char cascade[MAX_ENTRIES][MAX_PKGNAME];
    int ncascade = 0;
    collect_cascade(g, sm, pkgname, cascade, &ncascade, MAX_ENTRIES);

    if (ncascade == 0) {
        printf("No dependents for %s\n", pkgname);
        somap_free(sm); graph_free(g); return;
    }

    static char sorted[MAX_ENTRIES][MAX_PKGNAME];
    int nsorted = 0;
    topo_sort(g, sm, cascade, ncascade, sorted, &nsorted);

    printf("Cascade order for %s (%d):\n", pkgname, nsorted);
    for (int i = 0; i < nsorted; i++)
        printf("  %3d. %s\n", i + 1, sorted[i]);

    somap_free(sm);
    graph_free(g);
}

static void cmd_dot(const char *pkgname) {
    Graph *g = graph_load(GRAPH_CACHE);
    if (!g) { fprintf(stderr, "bogartgraph: no cache\n"); exit(1); }

    SonameMap *sm = somap_build(g);

    static char cascade[MAX_ENTRIES][MAX_PKGNAME];
    int ncascade = 0;
    collect_cascade(g, sm, pkgname, cascade, &ncascade, MAX_ENTRIES);

    printf("digraph \"%s\" {\n", pkgname);
    printf("  rankdir=LR;\n");
    printf("  node [shape=box fontname=\"monospace\"];\n");
    printf("  \"%s\" [style=filled fillcolor=lightblue];\n", pkgname);

    for (int i = 0; i < ncascade; i++) {
        for (int ei = 0; ei < g->count; ei++) {
            GraphEntry *e = &g->entries[ei];
            if (e->type != ENTRY_PROVIDES || strcmp(e->pkg, cascade[i]) != 0) continue;
            for (int j = 0; j < ncascade; j++) {
                if (i == j) continue;
                for (int s = 0; s < sm->count; s++) {
                    if (strcmp(sm->entries[s].soname, e->soname) != 0) continue;
                    AdjList *nl = &sm->entries[s].needers;
                    for (int k = 0; k < nl->count; k++) {
                        if (strcmp(nl->edges[k].pkg, cascade[j]) != 0) continue;
                        printf("  \"%s\" -> \"%s\" [label=\"%s\"];\n",
                               cascade[i], cascade[j], e->soname);
                        break;
                    }
                    break;
                }
            }
        }
        for (int ei = 0; ei < g->count; ei++) {
            GraphEntry *e = &g->entries[ei];
            if (e->type != ENTRY_PROVIDES || strcmp(e->pkg, pkgname) != 0) continue;
            for (int s = 0; s < sm->count; s++) {
                if (strcmp(sm->entries[s].soname, e->soname) != 0) continue;
                AdjList *nl = &sm->entries[s].needers;
                for (int k = 0; k < nl->count; k++) {
                    if (strcmp(nl->edges[k].pkg, cascade[i]) != 0) continue;
                    printf("  \"%s\" -> \"%s\" [label=\"%s\"];\n",
                           pkgname, cascade[i], e->soname);
                    break;
                }
                break;
            }
        }
    }
    printf("}\n");
    somap_free(sm);
    graph_free(g);
}

static void usage(void) {
    fprintf(stderr,
        "bogartgraph — Bogart Linux package dependency graph\n\n"
        "Usage:\n"
        "  bogartgraph                  full cache rebuild\n"
        "  bogartgraph --rescan <pkg>   rescan single package\n"
        "  bogartgraph --needs <pkg>    sonames pkg needs\n"
        "  bogartgraph --provides <pkg> sonames pkg provides\n"
        "  bogartgraph --rdeps <pkg>    packages that depend on pkg\n"
        "  bogartgraph --cascade <pkg>  topo-sorted cascade order\n"
        "  bogartgraph --dot <pkg>      graphviz dot output\n\n"
        "Cache: %s\n", GRAPH_CACHE);
}

int main(int argc, char *argv[]) {
    if (argc == 1)                            { cmd_rebuild();                                        return 0; }
    if (strcmp(argv[1], "--rescan")   == 0)   { if (argc < 3) { usage(); return 1; } cmd_rescan(argv[2]);   return 0; }
    if (strcmp(argv[1], "--needs")    == 0)   { if (argc < 3) { usage(); return 1; } cmd_needs(argv[2]);    return 0; }
    if (strcmp(argv[1], "--provides") == 0)   { if (argc < 3) { usage(); return 1; } cmd_provides(argv[2]); return 0; }
    if (strcmp(argv[1], "--rdeps")    == 0)   { if (argc < 3) { usage(); return 1; } cmd_rdeps(argv[2]);    return 0; }
    if (strcmp(argv[1], "--cascade")  == 0)   { if (argc < 3) { usage(); return 1; } cmd_cascade(argv[2]);  return 0; }
    if (strcmp(argv[1], "--dot")      == 0)   { if (argc < 3) { usage(); return 1; } cmd_dot(argv[2]);      return 0; }
    usage();
    return 1;
}
