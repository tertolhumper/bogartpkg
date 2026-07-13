#ifndef GRAPH_H
#define GRAPH_H

#define PORG_DB     "/var/lib/porg"
#define GRAPH_CACHE "/var/cache/bogartpkg-graph.db"
#define MAX_LINE    512
#define MAX_SONAME  128
#define MAX_PKGNAME 64
#define MAX_ENTRIES 32768

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

Graph *graph_new(void);
void   graph_free(Graph *g);
void   graph_add(Graph *g, EntryType type, const char *pkg, const char *soname);
int    graph_has(Graph *g, EntryType type, const char *pkg, const char *soname);
int    graph_write(const Graph *g, const char *path);
Graph *graph_load(const char *path);

#endif
