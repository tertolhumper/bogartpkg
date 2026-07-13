#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "graph.h"
#include "scan.h"

static void collect_cascade(Graph *g, const char *root,
                             char out[][MAX_PKGNAME], int *out_count, int max_out)
{
    char queue[MAX_ENTRIES][MAX_PKGNAME];
    int  qhead = 0, qtail = 0;
    char visited[MAX_ENTRIES][MAX_PKGNAME];
    int  nvisited = 0;

    strncpy(queue[qtail], root, MAX_PKGNAME - 1);
    queue[qtail++][MAX_PKGNAME - 1] = '\0';

    while (qhead < qtail) {
        char cur[MAX_PKGNAME];
        strncpy(cur, queue[qhead], MAX_PKGNAME - 1);
        cur[MAX_PKGNAME - 1] = '\0';
        qhead++;

        int dup = 0;
        for (int i = 0; i < nvisited; i++)
            if (strcmp(visited[i], cur) == 0) { dup = 1; break; }
        if (dup) continue;

        strncpy(visited[nvisited], cur, MAX_PKGNAME - 1);
        visited[nvisited++][MAX_PKGNAME - 1] = '\0';

        if (strcmp(cur, root) != 0 && *out_count < max_out) {
            strncpy(out[*out_count], cur, MAX_PKGNAME - 1);
            out[(*out_count)++][MAX_PKGNAME - 1] = '\0';
        }

        for (int i = 0; i < g->count; i++) {
            GraphEntry *e = &g->entries[i];
            if (e->type != ENTRY_PROVIDES || strcmp(e->pkg, cur) != 0) continue;

            for (int j = 0; j < g->count; j++) {
                GraphEntry *f = &g->entries[j];
                if (f->type != ENTRY_NEEDS || strcmp(f->soname, e->soname) != 0
                    || strcmp(f->pkg, cur) == 0) continue;

                int seen = 0;
                for (int k = 0; k < nvisited; k++)
                    if (strcmp(visited[k], f->pkg) == 0) { seen = 1; break; }
                if (!seen && qtail < MAX_ENTRIES) {
                    strncpy(queue[qtail], f->pkg, MAX_PKGNAME - 1);
                    queue[qtail++][MAX_PKGNAME - 1] = '\0';
                }
            }
        }
    }
}

static void topo_sort(Graph *g,
                      char nodes[][MAX_PKGNAME], int ncount,
                      char sorted[][MAX_PKGNAME], int *scount)
{
    int   in_degree[MAX_ENTRIES]  = {0};
    int   edge_count[MAX_ENTRIES] = {0};
    int   edge_cap[MAX_ENTRIES]   = {0};
    int  *edges[MAX_ENTRIES];
    memset(edges, 0, ncount * sizeof(int *));

    for (int i = 0; i < ncount; i++) {
        for (int ei = 0; ei < g->count; ei++) {
            GraphEntry *e = &g->entries[ei];
            if (e->type != ENTRY_PROVIDES || strcmp(e->pkg, nodes[i]) != 0) continue;

            for (int j = 0; j < ncount; j++) {
                if (i == j) continue;
                for (int ej = 0; ej < g->count; ej++) {
                    GraphEntry *f = &g->entries[ej];
                    if (f->type != ENTRY_NEEDS   ||
                        strcmp(f->pkg,    nodes[j])  != 0 ||
                        strcmp(f->soname, e->soname) != 0) continue;

                    if (edge_count[i] >= edge_cap[i]) {
                        edge_cap[i] = edge_cap[i] ? edge_cap[i] * 2 : 4;
                        edges[i]    = realloc(edges[i], edge_cap[i] * sizeof(int));
                    }
                    edges[i][edge_count[i]++] = j;
                    in_degree[j]++;
                    break;
                }
            }
        }
    }

    int queue[MAX_ENTRIES], qh = 0, qt = 0;
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
    for (int i = 0; i < ncount; i++) {
        int found = 0;
        for (int j = 0; j < *scount; j++)
            if (strcmp(sorted[j], nodes[i]) == 0) { found = 1; break; }
        if (!found) {
            strncpy(sorted[*scount], nodes[i], MAX_PKGNAME - 1);
            sorted[(*scount)++][MAX_PKGNAME - 1] = '\0';
        }
    }
    for (int i = 0; i < ncount; i++) free(edges[i]);
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
            pcap   *= 2;
            pnames  = realloc(pnames, pcap * sizeof(char *));
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
    for (int i = 0; i < g->count; i++) {
        GraphEntry *e = &g->entries[i];
        if (e->type != ENTRY_NEEDS || strcmp(e->pkg, pkgname) == 0) continue;
        for (int j = 0; j < nprovided; j++) {
            if (strcmp(e->soname, provided[j]) != 0) continue;
            int dup = 0;
            for (int k = 0; k < nrdeps; k++)
                if (strcmp(rdeps[k], e->pkg) == 0) { dup = 1; break; }
            if (!dup && nrdeps < MAX_ENTRIES) {
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

    static char cascade[MAX_ENTRIES][MAX_PKGNAME];
    int ncascade = 0;
    collect_cascade(g, pkgname, cascade, &ncascade, MAX_ENTRIES);

    if (ncascade == 0) {
        printf("No dependents for %s\n", pkgname);
        graph_free(g); return;
    }

    static char sorted[MAX_ENTRIES][MAX_PKGNAME];
    int nsorted = 0;
    topo_sort(g, cascade, ncascade, sorted, &nsorted);

    printf("Cascade order for %s (%d):\n", pkgname, nsorted);
    for (int i = 0; i < nsorted; i++)
        printf("  %3d. %s\n", i + 1, sorted[i]);
    graph_free(g);
}

static void cmd_dot(const char *pkgname) {
    Graph *g = graph_load(GRAPH_CACHE);
    if (!g) { fprintf(stderr, "bogartgraph: no cache\n"); exit(1); }

    static char cascade[MAX_ENTRIES][MAX_PKGNAME];
    int ncascade = 0;
    collect_cascade(g, pkgname, cascade, &ncascade, MAX_ENTRIES);

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
                for (int ej = 0; ej < g->count; ej++) {
                    GraphEntry *f = &g->entries[ej];
                    if (f->type != ENTRY_NEEDS   ||
                        strcmp(f->pkg,    cascade[j]) != 0 ||
                        strcmp(f->soname, e->soname)  != 0) continue;
                    printf("  \"%s\" -> \"%s\" [label=\"%s\"];\n",
                           cascade[i], cascade[j], e->soname);
                    break;
                }
            }
        }
        for (int ei = 0; ei < g->count; ei++) {
            GraphEntry *e = &g->entries[ei];
            if (e->type != ENTRY_PROVIDES || strcmp(e->pkg, pkgname) != 0) continue;
            for (int ej = 0; ej < g->count; ej++) {
                GraphEntry *f = &g->entries[ej];
                if (f->type != ENTRY_NEEDS   ||
                    strcmp(f->pkg,    cascade[i]) != 0 ||
                    strcmp(f->soname, e->soname)  != 0) continue;
                printf("  \"%s\" -> \"%s\" [label=\"%s\"];\n",
                       pkgname, cascade[i], e->soname);
                break;
            }
        }
    }
    printf("}\n");
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
    if (argc == 1)                            { cmd_rebuild();         return 0; }
    if (strcmp(argv[1], "--rescan")   == 0)   { if (argc < 3) { usage(); return 1; } cmd_rescan(argv[2]);   return 0; }
    if (strcmp(argv[1], "--needs")    == 0)   { if (argc < 3) { usage(); return 1; } cmd_needs(argv[2]);    return 0; }
    if (strcmp(argv[1], "--provides") == 0)   { if (argc < 3) { usage(); return 1; } cmd_provides(argv[2]); return 0; }
    if (strcmp(argv[1], "--rdeps")    == 0)   { if (argc < 3) { usage(); return 1; } cmd_rdeps(argv[2]);    return 0; }
    if (strcmp(argv[1], "--cascade")  == 0)   { if (argc < 3) { usage(); return 1; } cmd_cascade(argv[2]);  return 0; }
    if (strcmp(argv[1], "--dot")      == 0)   { if (argc < 3) { usage(); return 1; } cmd_dot(argv[2]);      return 0; }
    usage();
    return 1;
}
