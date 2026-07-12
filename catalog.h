#ifndef CATALOG_H
#define CATALOG_H

#define MAX_PACKAGES 512
#define MAX_LEN      256
#define PARALLEL     3
#define USERAGENT    "BogartLinux-check-updates/10.0"

#define RED     "\033[0;31m"
#define GREEN   "\033[0;32m"
#define YELLOW  "\033[1;33m"
#define CYAN    "\033[0;36m"
#define BLUE    "\033[0;34m"
#define MAGENTA "\033[0;35m"
#define BOLD    "\033[1m"
#define DIM     "\033[2m"
#define NC      "\033[0m"

typedef enum {
    CAT_TRACK    = 0,
    CAT_LFS      = 1,
    CAT_BLFS     = 2,
    CAT_XORG     = 3,
    CAT_HYPRLAND = 4,
    CAT_TOOLCHAIN= 5,
    CAT_NOISE    = 6,
} Category;

typedef enum {
    SRC_ARCH      = 0,
    SRC_GITHUB    = 1,
    SRC_GHTAG     = 2,
    SRC_GITLAB    = 3,
    SRC_SKIP      = 4,
    SRC_GHREFTAG  = 5,
} SrcType;

typedef struct {
    const char *name;
    SrcType     src;
    const char *repo;
    const char *prefix;
} BlfsSrc;

typedef struct { const char *name; const char *repo; } GHMap;
typedef struct { const char *from; const char *to; }   Map;
typedef struct { const char *name; Category cat; }     CatEntry;

const BlfsSrc  *blfs_src_lookup(const char *name);
const char     *gh_repo(const char *name);
const char     *arch_name(const char *name);
Category        get_category(const char *name);

#endif
