/*
 * bogartgraph.c — Bogart Linux package dependency graph builder
 *
 * Scans porg-tracked packages via readelf -d to build a bidirectional
 * dependency graph stored as a flat text cache.
 *
 * Cache format: /var/cache/bogartpkg-graph.db
 *   PROVIDES <pkg> <soname>
 *   NEEDS    <pkg> <soname>
 *
 * Usage:
 *   bogartgraph                  full cache rebuild
 *   bogartgraph --rescan <pkg>   rescan single package
 *   bogartgraph --needs <pkg>    sonames pkg needs
 *   bogartgraph --provides <pkg> sonames pkg provides
 *   bogartgraph --rdeps <pkg>    packages that depend on pkg
 *   bogartgraph --cascade <pkg>  full topo-sorted cascade order
 *   bogartgraph --dot <pkg>      graphviz dot output for subgraph
 *
 * Build:
 *   gcc -o bogartgraph bogartgraph.c
 *
 * Install:
 *   install -m755 bogartgraph /usr/local/bin/bogartgraph
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#define PORG_DB      "/var/lib/porg"
#define GRAPH_CACHE  "/var/cache/bogartpkg-graph.db"
#define MAX_LINE     512
#define MAX_SONAME   128
#define MAX_PKGNAME  64
#define MAX_ENTRIES  32768

typedef enum { ENTRY_PROVIDES, ENTRY_NEEDS } EntryType;

typedef struct {
    EntryType type;
    char      pkg[MAX_PKGNAME];
    char      soname[MAX_SONAME];
} GraphEntry;

typedef struct {
    GraphEntry *entries;
    int         count;
    int         capacity;
} Graph;

typedef struct {
    char **files;
    int    count;
    int    cap;
} FileList;

static Graph *graph_new(void) {
    Graph *g   = calloc(1, sizeof(Graph));
    g->capacity = 4096;
    g->entries  = calloc(g->capacity, sizeof(GraphEntry));
    return g;
}

static void graph_free(Graph *g) {
    if (!g) return;
    free(g->entries);
    free(g);
}

static void graph_add(Graph *g, EntryType type,
                      const char *pkg, const char *soname)
{
    if (g->count >= g->capacity) {
        g->capacity *= 2;
        g->entries   = realloc(g->entries, g->capacity * sizeof(GraphEntry));
    }
    GraphEntry *e = &g->entries[g->count++];
    e->type = type;
    strncpy(e->pkg,    pkg,    MAX_PKGNAME - 1); e->pkg[MAX_PKGNAME - 1]   = '\0';
    strncpy(e->soname, soname, MAX_SONAME  - 1); e->soname[MAX_SONAME - 1] = '\0';
}

static int graph_has(Graph *g, EntryType type,
                     const char *pkg, const char *soname)
{
    for (int i = 0; i < g->count; i++) {
        GraphEntry *e = &g->entries[i];
        if (e->type == type &&
            strcmp(e->pkg,    pkg)    == 0 &&
            strcmp(e->soname, soname) == 0)
            return 1;
    }
    return 0;
}

static void strip_version(const char *porg_name, char *out, int outlen) {
    strncpy(out, porg_name, outlen - 1);
    out[outlen - 1] = '\0';
    for (char *p = out; *p; p++) {
        if (*p == '-' && *(p + 1) >= '0' && *(p + 1) <= '9') {
            *p = '\0';
            return;
        }
    }
}

static void filelist_add(FileList *fl, const char *path) {
    if (fl->count >= fl->cap) {
        fl->cap   = fl->cap ? fl->cap * 2 : 256;
        fl->files = realloc(fl->files, fl->cap * sizeof(char *));
    }
    fl->files[fl->count++] = strdup(path);
}

static void filelist_free(FileList *fl) {
    for (int i = 0; i < fl->count; i++) free(fl->files[i]);
    free(fl->files);
    memset(fl, 0, sizeof(*fl));
}

static int filelist_from_porg(const char *porg_name, FileList *fl) {
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

static int is_regular_elf(const char *path) {
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

static void readelf_scan(const char *path,
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

static void scan_package(Graph *g, const char *porg_name) {
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

static int graph_write(const Graph *g, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "bogartgraph: cannot write %s: %s\n",
                path, strerror(errno));
        return -1;
    }
    fprintf(f, "# bogartpkg-graph.db — generated by bogartgraph\n");
    fprintf(f, "# PROVIDES <pkg> <soname>\n");
    fprintf(f, "# NEEDS    <pkg> <soname>\n\n");
    for (int i = 0; i < g->count; i++) {
        const GraphEntry *e = &g->entries[i];
        fprintf(f, "%s %s %s\n",
                e->type == ENTRY_PROVIDES ? "PROVIDES" : "NEEDS",
                e->pkg, e->soname);
    }
    fclose(f);
    return 0;
}

static Graph *graph_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    Graph *g = graph_new();
    char   line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char type_s[16], pkg[MAX_PKGNAME], soname[MAX_SONAME];
        if (sscanf(line, "%15s %63s %127s", type_s, pkg, soname) != 3) continue;
        EntryType t;
        if      (strcmp(type_s, "PROVIDES") == 0) t = ENTRY_PROVIDES;
        else if (strcmp(type_s, "NEEDS")    == 0) t = ENTRY_NEEDS;
        else continue;
        graph_add(g, t, pkg, soname);
    }
    fclose(f);
    return g;
}

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
