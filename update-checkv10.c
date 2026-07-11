#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <ctype.h>
#include <unistd.h>
#include "deps.h"

#define MAX_PACKAGES 512
#define MAX_LEN      256
#define PARALLEL     3
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
static const char *cat_label[] = {
    [CAT_TRACK]     = "TRACKED",
    [CAT_LFS]       = "LFS base",
    [CAT_BLFS]      = "BLFS",
    [CAT_XORG]      = "Xorg/X11 (legacy)",
    [CAT_HYPRLAND]  = "Hyprland",
    [CAT_TOOLCHAIN] = "Toolchain",
    [CAT_NOISE]     = "Noise",
};
typedef enum {
    SRC_ARCH      = 0,
    SRC_GITHUB    = 1,  /* /releases/latest — object response */
    SRC_GHTAG     = 2,  /* /tags — array response, optional prefix filter */
    SRC_GITLAB    = 3,  /* GitLab /releases API */
    SRC_SKIP      = 4,
    SRC_GHREFTAG  = 5,  /* /git/refs/tags — full ref list, prefix filter, version-sorted */
} SrcType;
typedef struct {
    const char *name;
    SrcType     src;
    const char *repo;
    const char *prefix;  /* optional tag prefix filter for SRC_GHTAG */
} BlfsSrc;
static const BlfsSrc BLFSSRC[] = {
    /* SRC_ARCH */
    {"asciidoc",            SRC_ARCH,   NULL,                   NULL},
    {"cython",              SRC_ARCH,   NULL,                   NULL},
    {"docutils",            SRC_ARCH,   "python-docutils",      NULL},
    {"intltool",            SRC_ARCH,   NULL,                   NULL},
    {"itstool",             SRC_ARCH,   NULL,                   NULL},
    {"libapparmor",         SRC_ARCH,   "apparmor",             NULL},
    {"linux-pam",           SRC_ARCH,   "pam",                  NULL},
    {"nasm",                SRC_ARCH,   NULL,                   NULL},
    {"nghttp2",             SRC_ARCH,   "libnghttp2",           NULL},
    {"pyyaml",              SRC_ARCH,   "python-yaml",          NULL},
    {"scdoc",               SRC_ARCH,   NULL,                   NULL},
    {"swig",                SRC_ARCH,   NULL,                   NULL},
    {"virglrenderer",       SRC_ARCH,   NULL,                   NULL},
    {"xml-parser",          SRC_ARCH,   "perl-xml-parser",      NULL},
    {"yaml-cpp",            SRC_ARCH,   NULL,                   NULL},
    {"yasm",                SRC_ARCH,   NULL,                   NULL},
    /* SRC_GITHUB — /releases/latest, skips prerelease */
    {"agg",                 SRC_GITHUB, "asciinema/agg",        NULL},
    {"bochs",               SRC_GITHUB, "bochs-emu/Bochs",      NULL}, 
    {"doxygen",             SRC_GITHUB, "doxygen/doxygen",      NULL},
    {"ironbar",             SRC_GITHUB, "JakeStanger/ironbar",  NULL},
    {"kitty",               SRC_GITHUB, "kovidgoyal/kitty",     NULL},
    {"lynis",               SRC_GITHUB, "CISOfy/lynis",         NULL},
    {"make-ca",             SRC_GITHUB, "lfs-book/make-ca",     NULL},
    {"nv-codec-headers",    SRC_GITHUB, "FFmpeg/nv-codec-headers", NULL},
    {"nvidia-vaapi-driver", SRC_GITHUB, "elFarto/nvidia-vaapi-driver", NULL},
    {"rofi-wayland",        SRC_GITHUB, "lbonn/rofi",           NULL},
    {"shadow",              SRC_GITHUB, "shadow-maint/shadow",  NULL},
    {"swww",                SRC_GITHUB, "LGFae/swww",           NULL},
    {"tailscale",           SRC_GITHUB, "tailscale/tailscale",  NULL},
    {"waypaper",            SRC_GITHUB, "anufrievroman/waypaper", NULL},
    /* SRC_GHTAG — /tags with optional prefix filter */
    {"ncpamixer",           SRC_GHTAG,  "fulhax/ncpamixer",     NULL},
    {"ps_mem",              SRC_GHTAG,  "pixelb/ps_mem",        NULL},
    {"qt6",                 SRC_GHTAG,  "qt/qtbase",           "v6."},
    {"udis86",              SRC_GHTAG,  "vmt/udis86",           NULL},
    {"porg",                SRC_GHTAG,  "drfiemost/porg",       NULL},
    /*SRC_GHREFTAG*/
    {"go",                  SRC_GHREFTAG, "golang/go",          "go1."},
    /* SRC_SKIP */
    {"qt5",                 SRC_SKIP,   NULL,                   NULL},
    {"xorriso",             SRC_SKIP,   NULL,                   NULL},
    {"usb-modeswitch",      SRC_SKIP,    NULL,                  NULL},
    {NULL, 0, NULL, NULL}
};
const BlfsSrc *blfs_src_lookup(const char *name) {
    for (int i = 0; BLFSSRC[i].name; i++)
        if (!strcmp(name, BLFSSRC[i].name)) return &BLFSSRC[i];
    return NULL;
}
typedef struct { const char *name; Category cat; } CatEntry;
static const CatEntry CATALOG[] = {
    /* LFS BASE */
    /* TOOLCHAIN */
    {"gcc",              CAT_TOOLCHAIN},
    {"glibc",            CAT_TOOLCHAIN},
    {"binutils",         CAT_TOOLCHAIN},
    {"clangd",           CAT_TOOLCHAIN},
    /* HYPRLAND / SLFS (untouched) */
    {"tomlplusplus",        CAT_HYPRLAND},
    {"re",                  CAT_HYPRLAND},
    {"glm",                 CAT_HYPRLAND},
    {"hyprutils",           CAT_HYPRLAND},
    {"hyprlang",            CAT_HYPRLAND},
    {"hyprwayland-scanner", CAT_HYPRLAND},
    {"hyprcursor",          CAT_HYPRLAND},
    {"hyprgraphics",        CAT_HYPRLAND},
    {"aquamarine",          CAT_HYPRLAND},
    {"hyprland",            CAT_HYPRLAND},
    {"hyprland-guiutils",   CAT_HYPRLAND},
    {"hyprtoolkit",         CAT_HYPRLAND},
    {"iniparser",           CAT_HYPRLAND},
    {"hyprpaper",           CAT_HYPRLAND},
    {"hyprland-protocols",  CAT_HYPRLAND},
    {"hyprland-source",     CAT_HYPRLAND},
    {"hyprwire",            CAT_HYPRLAND},
    {"wlroots",             CAT_HYPRLAND},
    /* XORG / X11 (legacy) */
    {"xorg-server",           CAT_XORG}, {"encodings",           CAT_XORG},
    {"xrandr",                CAT_XORG}, {"xcursorgen",          CAT_XORG},
    {"xcursor-themes",        CAT_XORG}, {"xkbcomp",             CAT_XORG},
    {"mkfontscale",           CAT_XORG}, {"xcb-proto",           CAT_XORG},
    {"xcb-util",              CAT_XORG}, {"xcb-util-cursor",     CAT_XORG},
    {"xcb-util-errors",       CAT_XORG}, {"xcb-util-image",      CAT_XORG},
    {"xcb-util-keysyms",      CAT_XORG}, {"xcb-util-renderutil", CAT_XORG},
    {"xcb-util-wm",           CAT_XORG}, {"xorgproto",           CAT_XORG},
    {"util-macros",           CAT_XORG}, {"libxscrnsaver",       CAT_XORG},
    {"libxxf86dga",           CAT_XORG}, {"eglexternalplatform", CAT_XORG},
    {"font-adobe-utopia-type1", CAT_XORG}, {"font-alias",        CAT_XORG},
    {"font-bh-ttf",             CAT_XORG}, {"font-bh-type1",     CAT_XORG},
    {"font-ibm-type1",          CAT_XORG}, {"font-misc-ethiopic", CAT_XORG},
    {"font-util",               CAT_XORG}, {"font-xfree86-type1", CAT_XORG},
    {"encodings",               CAT_XORG},
    /* BLFS */
    {"pyyaml",           CAT_BLFS}, {"docutils",      CAT_BLFS},
    {"cython",           CAT_BLFS}, {"go",            CAT_BLFS},
    {"intltool",         CAT_BLFS}, {"xml-parser",    CAT_BLFS},
    {"itstool",          CAT_BLFS}, {"scdoc",         CAT_BLFS},
    {"nasm",             CAT_BLFS}, {"yasm",          CAT_BLFS},
    {"asciidoc",         CAT_BLFS}, {"doxygen",       CAT_BLFS},
    {"swig",             CAT_BLFS}, {"xorriso",       CAT_BLFS},
    {"linux-pam",        CAT_BLFS}, {"make-ca",       CAT_BLFS},
    {"libapparmor",      CAT_BLFS}, {"nghttp2",       CAT_BLFS},
    {"qt6",    	         CAT_BLFS}, {"lynis",         CAT_BLFS},
    {"virglrenderer",    CAT_BLFS}, {"kitty",         CAT_BLFS}, 
    {"nv-codec-headers", CAT_BLFS}, {"qt5",           CAT_BLFS},  
    {"yaml-cpp",         CAT_BLFS}, {"ncpamixer",     CAT_BLFS},
    {"ironbar",          CAT_BLFS}, {"rofi-wayland",  CAT_BLFS},
    {"waypaper",         CAT_BLFS}, {"swww",          CAT_BLFS},
    {"porg",             CAT_BLFS}, {"ps_mem",        CAT_BLFS},
    {"agg",              CAT_BLFS}, {"udis86",        CAT_BLFS},
    {"tailscale",        CAT_BLFS}, {"shadow",        CAT_BLFS},
    {"bochs",		 CAT_BLFS}, {"nvidia-vaapi-driver", CAT_BLFS},   
    {"usb-modeswitch",   CAT_BLFS},    
    /* NOISE */
    {NULL, 0}
};
typedef struct { const char *name; const char *repo; } GHMap;
static const GHMap GHMAP[] = {
    {"hyprland",            "hyprwm/Hyprland"},
    {"aquamarine",          "hyprwm/aquamarine"},
    {"hyprutils",           "hyprwm/hyprutils"},
    {"hyprlang",            "hyprwm/hyprlang"},
    {"hyprcursor",          "hyprwm/hyprcursor"},
    {"hyprwayland-scanner", "hyprwm/hyprwayland-scanner"},
    {"hyprtoolkit",         "hyprwm/hyprtoolkit"},
    {"hyprgraphics",        "hyprwm/hyprgraphics"},
    {"hyprwire",            "hyprwm/hyprwire"},
    {"hyprpaper",           "hyprwm/hyprpaper"},
    {"hyprland-guiutils",   "hyprwm/hyprland-guiutils"},
    {"hyprland-protocols",  "hyprwm/hyprland-protocols"},
    {"tomlplusplus",        "marzer/tomlplusplus"},
    {"iniparser",           "ndevilla/iniparser"},
    {"glm",                 "g-truc/glm"},
    {"wlroots",             "swaywm/wlroots"},
    {"re",                  "google/re2"},
    {NULL, NULL}
};
const char *gh_repo(const char *name) {
    for (int i = 0; GHMAP[i].name; i++)
        if (!strcmp(name, GHMAP[i].name)) return GHMAP[i].repo;
    return NULL;
}
typedef struct { const char *from; const char *to; } Map;
static const Map ARCHMAP[] = {
    {"audit-userspace",  "audit"},
    {"freetype",         "freetype2"},
    {"fuse",             "fuse3"},
    {"gdk-pixbuf",       "gdk-pixbuf2"},
    {"glib",             "glib2"},
    {"graphite2",        "graphite"},
    {"gtk",              "gtk3"},
    {"sqlite-autoconf",  "sqlite"},
    {"vulkan-loader",    "vulkan-icd-loader"},
    {"linux-pam",        "pam"},
    {"xwayland",         "xorg-xwayland"},
    {"wlroots",          "wlroots0.19"},
    {"pulseaudio",       "libpulse"},
    {"libsoup",          "libsoup3"},
    {"libostree",        "ostree"},
    {"gst-plugins-bad",  "gst-plugins-bad-libs"},
    {"libaom",           "aom"},
    {"yaml",             "libyaml"},
    {"abseil-cpp",       "abseil-cpp"},
    {"libtasn1",         "libtasn1"},
    {"clangd",           "clang"},
    {"openjpeg",         "openjpeg2"},
    {"qrcodegencpp",     "qrcodegencpp-cmake"},
    {"noto-cjk",         "noto-fonts-cjk"},
    {"qemu",             "qemu-system-x86"},
    {"pygobject",        "python-gobject"},
    {"fdk-aac",          "libfdk-aac"},
    {"x265",             "x265"},
    {"inih",             "libinih"},
    {"qt5",              "qt5-base"},
    {"flit-core", "python-flit-core"},
    {"jinja2",      "python-jinja"},
    {"markupsafe",  "python-markupsafe"},
    {"packaging",   "python-packaging"},
    {"setuptools",  "python-setuptools"},
    {"wheel",       "python-wheel"},
    {NULL, NULL}
};
typedef enum { ST_OUTDATED=0, ST_CURRENT=1, ST_AHEAD=2,
               ST_NOTFOUND=3, ST_SKIP=4 } Status;
typedef struct { char *data; size_t size; } Buf;
typedef struct {
    char     porg[MAX_LEN];
    char     name[MAX_LEN];
    char     arch[MAX_LEN];
    char     inst[MAX_LEN];
    char     got[MAX_LEN];
    Status   status;
    Category cat;
    SrcType  src_type;
    Buf      buf;
    CURL    *h;
    char     url[512];
    char     tag_prefix[32]; /* optional prefix filter for SRC_GHTAG */
    int      queued;
    int      retries;
} Pkg;
static size_t wcb(void *p, size_t sz, size_t n, void *ud) {
    Buf *b = ud;
    size_t t = sz * n;
    char *tmp = realloc(b->data, b->size + t + 1);
    if (!tmp) return 0;
    b->data = tmp;
    memcpy(b->data + b->size, p, t);
    b->size += t;
    b->data[b->size] = '\0';
    return t;
}
Category get_category(const char *name) {
    for (int i = 0; CATALOG[i].name; i++)
        if (!strcmp(name, CATALOG[i].name)) return CATALOG[i].cat;
    if (strlen(name) <= 1) return CAT_NOISE;
    return CAT_TRACK;
}
const char *arch_name(const char *n) {
    for (int i = 0; ARCHMAP[i].from; i++)
        if (!strcmp(n, ARCHMAP[i].from)) return ARCHMAP[i].to;
    return n;
}
int pkg_exists(Pkg *pkgs, int n, const char *name) {
    for (int i = 0; i < n; i++)
        if (!strcmp(pkgs[i].name, name)) return 1;
    return 0;
}
void split_porg(const char *entry, char *name, char *ver, size_t len) {
    name[0] = ver[0] = '\0';
    const char *us = strchr(entry, '_');
    if (us && isdigit(*(us + 1))) {
        size_t nl = us - entry; if (nl >= len) nl = len - 1;
        snprintf(name, MAX_LEN, "%.*s", (int)(nl), entry); name[nl] = '\0';
        snprintf(ver, MAX_LEN, "%.*s", (int)(len - 1), us + 1); return;
    }
    int has_digit = 0;
    for (const char *p = entry; *p; p++)
        if (isdigit((unsigned char)*p)) { has_digit = 1; break; }
    if (!has_digit) { snprintf(name, MAX_LEN, "%.*s", (int)(len - 1), entry); return; }
    const char *candidates[64]; int nc = 0;
    for (const char *p = entry; *p; p++) {
        if (*p == '-') {
            const char *v = p + 1;
            if (isdigit((unsigned char)*v) ||
                (*v == 'v' && isdigit((unsigned char)*(v + 1))) ||
                (*v == 'r' && isdigit((unsigned char)*(v + 1))))
                if (nc < 64) candidates[nc++] = p;
        }
    }
    if (nc == 0) {
        const char *p = entry;
        while (*p && !isdigit((unsigned char)*p)) p++;
        if (*p && p != entry) {
            size_t nl = p - entry; if (nl >= len) nl = len - 1;
            snprintf(name, MAX_LEN, "%.*s", (int)(nl), entry); name[nl] = '\0';
            snprintf(ver, MAX_LEN, "%.*s", (int)(len - 1), p);
        } else snprintf(name, MAX_LEN, "%.*s", (int)(len - 1), entry);
        return;
    }
    const char *best = NULL;
    for (int i = nc - 1; i >= 0; i--) {
        if (candidates[i] > entry && !isdigit((unsigned char)*(candidates[i] - 1)))
            { best = candidates[i]; break; }
    }
    if (!best) best = candidates[nc - 1];
    size_t nl = best - entry; if (nl >= len) nl = len - 1;
    snprintf(name, MAX_LEN, "%.*s", (int)(nl), entry); name[nl] = '\0';
    const char *vs = best + 1;
    if ((*vs == 'v' || *vs == 'r') && isdigit((unsigned char)*(vs + 1))) vs++;
    snprintf(ver, MAX_LEN, "%.*s", (int)(len - 1), vs);
    char *dash = strrchr(ver, '-');
    if (dash && *(dash + 1)) {
        int all_dig = 1;
        for (char *c = dash + 1; *c; c++)
            if (!isdigit((unsigned char)*c)) { all_dig = 0; break; }
        if (all_dig) *dash = '\0';
        else {
            char first[MAX_LEN]; size_t fl = dash - ver;
            if (fl < MAX_LEN) {
                snprintf(first, MAX_LEN, "%.*s", (int)(fl), ver); first[fl] = '\0';
                if (!strcmp(first, dash + 1)) *dash = '\0';
            }
        }
    }
}
void parse_ver(const char *json, char *out, size_t outlen) {
    out[0] = '\0';
    const char *results = strstr(json, "\"results\"");
    if (!results) { snprintf(out, MAX_LEN, "%.*s", (int)(outlen), "NOT_FOUND"); return; }
    const char *arr = strchr(results, '[');
    if (!arr) { snprintf(out, MAX_LEN, "%.*s", (int)(outlen), "NOT_FOUND"); return; }
    char best_ver[MAX_LEN] = ""; int found_x86 = 0;
    const char *p = arr + 1;
    while (*p && *p != ']') {
        if (*p != '{') { p++; continue; }
        int depth = 0; const char *oe = p;
        while (*oe) {
            if (*oe == '{') depth++;
            else if (*oe == '}') { if (--depth == 0) { oe++; break; } }
            oe++;
        }
        char obj[8192]; size_t ol = oe - p;
        if (ol >= sizeof(obj)) ol = sizeof(obj) - 1;
        snprintf(obj, sizeof(obj), "%.*s", (int)(ol), p); obj[ol] = '\0';
        char ver[MAX_LEN] = "", arc[MAX_LEN] = "";
        const char *vt = strstr(obj, "\"pkgver\"");
        if (vt) { vt = strchr(vt, ':'); if (vt) { vt = strchr(vt, '"'); if (vt) { vt++;
            const char *ve = strchr(vt, '"');
            if (ve) { size_t l = ve - vt; if (l >= MAX_LEN) l = MAX_LEN - 1;
                snprintf(ver, MAX_LEN, "%.*s", (int)(l), vt); ver[l] = '\0'; }}}}
        const char *at = strstr(obj, "\"arch\"");
        if (at) { at = strchr(at, ':'); if (at) { at = strchr(at, '"'); if (at) { at++;
            const char *ae = strchr(at, '"');
            if (ae) { size_t l = ae - at; if (l >= MAX_LEN) l = MAX_LEN - 1;
                snprintf(arc, MAX_LEN, "%.*s", (int)(l), at); arc[l] = '\0'; }}}}
        if (ver[0]) {
            if (!strcmp(arc, "x86_64")) { snprintf(best_ver, MAX_LEN, "%.*s", (int)(MAX_LEN - 1), ver); found_x86 = 1; break; }
            else if (!found_x86 && !best_ver[0]) snprintf(best_ver, MAX_LEN, "%.*s", (int)(MAX_LEN - 1), ver);
        }
        p = oe;
    }
    snprintf(out, MAX_LEN, "%.*s", (int)(outlen), best_ver[0] ? best_ver : "NOT_FOUND");
}
void parse_gh_ver(const char *json, char *out, size_t outlen) {
    out[0] = '\0';
    const char *pr = strstr(json, "\"prerelease\"");
    if (pr) {
        const char *colon = strchr(pr, ':');
        if (colon) {
            while (*colon == ':' || *colon == ' ') colon++;
            if (strncmp(colon, "true", 4) == 0) {
                snprintf(out, MAX_LEN, "%.*s", (int)(outlen), "NOT_FOUND");
                return;
            }
        }
    }
    const char *t = strstr(json, "\"tag_name\"");
    if (!t) { snprintf(out, MAX_LEN, "%.*s", (int)(outlen), "NOT_FOUND"); return; }
    t = strchr(t, ':');
    if (!t) { snprintf(out, MAX_LEN, "%.*s", (int)(outlen), "NOT_FOUND"); return; }
    t = strchr(t, '"');
    if (!t) { snprintf(out, MAX_LEN, "%.*s", (int)(outlen), "NOT_FOUND"); return; }
    t++;
    const char *e = strchr(t, '"');
    if (!e) { snprintf(out, MAX_LEN, "%.*s", (int)(outlen), "NOT_FOUND"); return; }
    size_t l = e - t;
    if (l >= outlen) l = outlen - 1;
    snprintf(out, MAX_LEN, "%.*s", (int)(l), t);
    out[l] = '\0';
    if (out[0] == 'v') memmove(out, out + 1, strlen(out));
}
void parse_gh_tag_ver(const char *json, char *out, size_t outlen, const char *prefix) {
    out[0] = '\0';
    const char *arr = strchr(json, '[');
    if (!arr) { snprintf(out, MAX_LEN, "%.*s", (int)(outlen), "NOT_FOUND"); return; }
    const char *p = arr + 1;
    while (*p && *p != ']') {
        const char *t = strstr(p, "\"name\"");
        if (!t) break;
        t = strchr(t, ':');
        if (!t) break;
        t = strchr(t, '"');
        if (!t) break;
        t++;
        const char *e = strchr(t, '"');
        if (!e) break;
        size_t l = e - t;
        if (l >= outlen) l = outlen - 1;
        char candidate[MAX_LEN];
        snprintf(candidate, MAX_LEN, "%.*s", (int)(l), t);
        candidate[l] = '\0';
        if (!prefix || strncmp(candidate, prefix, strlen(prefix)) == 0) {
            snprintf(out, MAX_LEN, "%.*s", (int)(outlen - 1), candidate);
            if (out[0] == 'v') memmove(out, out + 1, strlen(out));
            return;
        }
        p = e + 1;
    }
    snprintf(out, MAX_LEN, "%.*s", (int)(outlen), "NOT_FOUND");
}
int ver_cmp(const char *a, const char *b);
void parse_gh_reftag_ver(const char *json, char *out, size_t outlen, const char *prefix) {
    out[0] = '\0';
    char best[MAX_LEN] = "";
    const char *p = json;
    while (*p) {
        const char *t = strstr(p, "\"refs/tags/");
        if (!t) break;
        t += 11; 
        const char *e = strchr(t, '"');
        if (!e) break;
        size_t l = e - t;
        if (l >= MAX_LEN) { p = e + 1; continue; }
        char candidate[MAX_LEN];
        snprintf(candidate, MAX_LEN, "%.*s", (int)(l), t);
        candidate[l] = '\0';
        if (!prefix || strncmp(candidate, prefix, strlen(prefix)) == 0) {
            const char *cv = prefix ? candidate + strlen(prefix) : candidate;
            const char *bv = prefix && best[0] ? best + strlen(prefix) : best;
            if (cv[0] && isdigit((unsigned char)cv[0])) {
                if (!best[0] || ver_cmp(cv, bv) > 0)
                    snprintf(best, MAX_LEN, "%.*s", (int)(MAX_LEN - 1), candidate);
            }
        }
        p = e + 1;
    }
    if (best[0]) {
        snprintf(out, MAX_LEN, "%.*s", (int)(outlen - 1), best);
        out[outlen - 1] = '\0';
    } else {
        snprintf(out, MAX_LEN, "%.*s", (int)(outlen), "NOT_FOUND");
    }
}

void ver_clean(const char *in, char *out, size_t n) {
    snprintf(out, MAX_LEN, "%.*s", (int)(n - 1), in); out[n - 1] = '\0';
    if (strncmp(out, "Release_", 8) == 0) {
        memmove(out, out + 8, strlen(out) - 7);
        for (char *p = out; *p; p++)
            if (*p == '_') *p = '.';
        return;
    }
    if ((out[0] == 'v' || out[0] == 'n') && isdigit((unsigned char)out[1]))
        memmove(out, out + 1, strlen(out));
    if (strncmp(out, "go", 2) == 0 && isdigit((unsigned char)out[2]))
        memmove(out, out + 2, strlen(out) - 1);
    char *c = strchr(out, '+'); if (c) *c = '\0';
    const char *pre_suffixes[] = {"-pre", "-alpha", "-beta", "-rc", "-lts-lgpl", "-lts", NULL};
    for (int i = 0; pre_suffixes[i]; i++) {
        char *s = strstr(out, pre_suffixes[i]);
        if (s) { *s = '\0'; break; }
    }
    char *d = strrchr(out, '-');
    if (d) { int ok = 1; for (char *x = d + 1; *x; x++)
        if (!isdigit((unsigned char)*x)) { ok = 0; break; }
        if (ok && *(d + 1)) *d = '\0'; }
    char *dp = strstr(out, ".p");
    if (dp && isdigit((unsigned char)dp[2])) memmove(dp, dp + 1, strlen(dp));
}
int ver_cmp(const char *a, const char *b) {
    char ac[MAX_LEN], bc[MAX_LEN];
    ver_clean(a, ac, sizeof(ac)); ver_clean(b, bc, sizeof(bc));
    char av[MAX_LEN], bv[MAX_LEN];
    snprintf(av, MAX_LEN, "%.*s", (int)(MAX_LEN), ac); snprintf(bv, MAX_LEN, "%.*s", (int)(MAX_LEN), bc);
    char *ap = av, *bp = bv;
    while (*ap || *bp) {
        char *ad = strchr(ap, '.'), *bd = strchr(bp, '.');
        if (ad) *ad = '\0'; if (bd) *bd = '\0';
        int adi = isdigit((unsigned char)*ap), bdi = isdigit((unsigned char)*bp);
        int cmp;
        if (adi && bdi) { long ai = strtol(ap, NULL, 10), bi = strtol(bp, NULL, 10);
            cmp = ai > bi ? 1 : ai < bi ? -1 : 0; }
        else { cmp = strcmp(ap, bp); cmp = cmp > 0 ? 1 : cmp < 0 ? -1 : 0; }
        if (cmp) return cmp;
        ap = ad ? ad + 1 : ap + strlen(ap);
        bp = bd ? bd + 1 : bp + strlen(bp);
    }
    return 0;
}
static void add_auth_header(CURL *h, const char *auth_header) {
    if (auth_header[0]) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, auth_header);
        curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers);
    }
}
int main(void) {
    if (deps_load(NULL) != 0)
        fprintf(stderr, "warning: dep map unavailable, cascade check skipped\n");
    FILE *tf = fopen("/root/.github_token", "r");
    char token[256] = "";
    char auth_header[280] = "";
    if (tf) {
        fgets(token, sizeof(token), tf);
        fclose(tf);
        token[strcspn(token, "\n")] = '\0';
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);
    }
    static Pkg pkgs[MAX_PACKAGES];
    int np = 0;
    FILE *f = popen("porg -a 2>/dev/null", "r");
    if (!f) { fprintf(stderr, "porg -a failed\n"); return 1; }
    char line[512];
    while (fgets(line, sizeof(line), f) && np < MAX_PACKAGES) {
        line[strcspn(line, "\n")] = '\0';
        if (!line[0]) continue;
        char name[MAX_LEN], ver[MAX_LEN];
        split_porg(line, name, ver, MAX_LEN);
        if (!name[0] || strlen(name) < 2) continue;
        if (pkg_exists(pkgs, np, name)) continue;
        Category cat = get_category(name);
        snprintf(pkgs[np].porg, MAX_LEN, "%.*s", (int)(MAX_LEN - 1), line);
        snprintf(pkgs[np].name, MAX_LEN, "%.*s", (int)(MAX_LEN - 1), name);
        snprintf(pkgs[np].inst, MAX_LEN, "%.*s", (int)(MAX_LEN - 1), ver);
        snprintf(pkgs[np].arch, MAX_LEN, "%.*s", (int)(MAX_LEN - 1), arch_name(name));
        pkgs[np].got[0]        = '\0';
        pkgs[np].tag_prefix[0] = '\0';
        pkgs[np].buf.data      = calloc(1, 1);
        if (!pkgs[np].buf.data) { fprintf(stderr, "OOM\n"); return 1; }
        pkgs[np].buf.size = 0;
        pkgs[np].h        = NULL;
        pkgs[np].queued   = 0;
	pkgs[np].retries  = 0;
        pkgs[np].cat      = cat;
        pkgs[np].src_type = SRC_SKIP;

        if (cat == CAT_HYPRLAND) {
            const char *repo = gh_repo(name);
            if (repo) {
                pkgs[np].status   = ST_NOTFOUND;
                pkgs[np].src_type = SRC_GITHUB;
                snprintf(pkgs[np].url, sizeof(pkgs[np].url),
                    "https://api.github.com/repos/%s/releases/latest", repo);
            } else {
                pkgs[np].status = ST_SKIP;
            }
        } else if (cat == CAT_BLFS) {
            const BlfsSrc *bs = blfs_src_lookup(name);
            if (!bs || bs->src == SRC_SKIP) {
                pkgs[np].status   = ST_SKIP;
                pkgs[np].src_type = SRC_SKIP;
            } else {
                pkgs[np].status   = ST_NOTFOUND;
                pkgs[np].src_type = bs->src;
                if (bs->prefix)
                    snprintf(pkgs[np].tag_prefix, sizeof(pkgs[np].tag_prefix), "%s", bs->prefix);
                if (bs->src == SRC_ARCH) {
                    const char *apkg = bs->repo ? bs->repo : name;
                    snprintf(pkgs[np].arch, MAX_LEN, "%.*s", (int)(MAX_LEN - 1), apkg);
                    snprintf(pkgs[np].url, sizeof(pkgs[np].url),
                        "https://archlinux.org/packages/search/json/?name=%s", apkg);
                } else if (bs->src == SRC_GITHUB) {
                    snprintf(pkgs[np].url, sizeof(pkgs[np].url),
                        "https://api.github.com/repos/%s/releases/latest", bs->repo);
                } else if (bs->src == SRC_GHTAG) {
                    snprintf(pkgs[np].url, sizeof(pkgs[np].url),
                        "https://api.github.com/repos/%s/tags", bs->repo);
                } else if (bs->src == SRC_GHREFTAG) {
                    snprintf(pkgs[np].url, sizeof(pkgs[np].url),
                        "https://api.github.com/repos/%s/git/refs/tags", bs->repo);
                }
            }
        } else if (cat == CAT_TRACK) {
            pkgs[np].status   = ST_NOTFOUND;
            pkgs[np].src_type = SRC_ARCH;
            snprintf(pkgs[np].url, sizeof(pkgs[np].url),
                "https://archlinux.org/packages/search/json/?name=%s",
                pkgs[np].arch);
        } else {
            pkgs[np].status   = ST_SKIP;
            pkgs[np].src_type = SRC_SKIP;
            pkgs[np].url[0]   = '\0';
        }
        np++;
    }
    pclose(f);
	int n_track = 0, n_hypr = 0, n_blfs = 0, n_blfs_total = 0, n_skip = 0;
	for (int i = 0; i < np; i++) {
        if (pkgs[i].cat == CAT_TRACK) n_track++;
        if (pkgs[i].cat == CAT_HYPRLAND && pkgs[i].status != ST_SKIP) n_hypr++;
		if (pkgs[i].cat == CAT_BLFS) { n_blfs_total++; if (pkgs[i].status != ST_SKIP) n_blfs++; }
      		if (pkgs[i].cat == CAT_LFS || pkgs[i].cat == CAT_XORG ||
   			 pkgs[i].cat == CAT_TOOLCHAIN || pkgs[i].cat == CAT_NOISE) n_skip++;
	}
    printf(CYAN "Tracking %d Arch | %d Hyprland | %d BLFS Packages \n\n" NC,
       n_track, n_hypr, n_blfs);
	fflush(stdout);
    curl_global_init(CURL_GLOBAL_ALL);
    CURLM *multi = curl_multi_init();
    curl_multi_setopt(multi, CURLMOPT_MAXCONNECTS, (long)PARALLEL);
    int n_total = n_track + n_hypr + n_blfs;
    int active = 0, done = 0, next = 0;
    for (int i = 0; i < np && active < PARALLEL; i++) {
        if (pkgs[i].status == ST_SKIP) continue;
        if (pkgs[i].cat != CAT_TRACK && pkgs[i].cat != CAT_HYPRLAND && pkgs[i].cat != CAT_BLFS) continue;
        pkgs[i].h = curl_easy_init();
        curl_easy_setopt(pkgs[i].h, CURLOPT_URL, pkgs[i].url);
        curl_easy_setopt(pkgs[i].h, CURLOPT_WRITEFUNCTION, wcb);
        curl_easy_setopt(pkgs[i].h, CURLOPT_WRITEDATA, &pkgs[i].buf);
        curl_easy_setopt(pkgs[i].h, CURLOPT_TIMEOUT, 12L);
        curl_easy_setopt(pkgs[i].h, CURLOPT_PRIVATE, &pkgs[i]);
        curl_easy_setopt(pkgs[i].h, CURLOPT_USERAGENT, "BogartLinux-check-updates/5.0");
        if (pkgs[i].src_type == SRC_GITHUB || pkgs[i].src_type == SRC_GHTAG ||
            pkgs[i].src_type == SRC_GHREFTAG)
            add_auth_header(pkgs[i].h, auth_header);
        curl_multi_add_handle(multi, pkgs[i].h);
        pkgs[i].queued = 1; active++; next = i + 1;
    }
    int running = 1;
    while (running || done < n_total) {
        curl_multi_perform(multi, &running);
        CURLMsg *msg; int ml;
	while ((msg = curl_multi_info_read(multi, &ml))) {
            if (msg->msg != CURLMSG_DONE) continue;
            Pkg *pk = NULL;
            curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &pk);
            if (pk) {
                long http_code = 0;
                curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &http_code);
                if (http_code == 429 && pk->retries < 3) {
                    pk->retries++;
                    free(pk->buf.data);
                    pk->buf.data = calloc(1, 1);
                    pk->buf.size = 0;
                    curl_multi_remove_handle(multi, msg->easy_handle);
                    usleep(2000000);
                    curl_multi_add_handle(multi, msg->easy_handle);
                    continue;
                }
                if (pk->src_type == SRC_ARCH)
                    parse_ver(pk->buf.data, pk->got, MAX_LEN);
                else if (pk->src_type == SRC_GHTAG)
                    parse_gh_tag_ver(pk->buf.data, pk->got, MAX_LEN,
                        pk->tag_prefix[0] ? pk->tag_prefix : NULL);
                else if (pk->src_type == SRC_GHREFTAG)
                    parse_gh_reftag_ver(pk->buf.data, pk->got, MAX_LEN,
                        pk->tag_prefix[0] ? pk->tag_prefix : NULL);
                else
                    parse_gh_ver(pk->buf.data, pk->got, MAX_LEN);
            }
            curl_multi_remove_handle(multi, msg->easy_handle);
            curl_easy_cleanup(msg->easy_handle);
            active--; done++;
            printf("\r" CYAN "Fetching %d/%d" NC, done, n_total); fflush(stdout);
            while (next < np) {
                if (pkgs[next].queued || pkgs[next].status == ST_SKIP ||
                    (pkgs[next].cat != CAT_TRACK && pkgs[next].cat != CAT_HYPRLAND &&
                     pkgs[next].cat != CAT_BLFS))
                    { next++; continue; }
                free(pkgs[next].buf.data);
                pkgs[next].buf.data = calloc(1, 1);
                if (!pkgs[next].buf.data) { fprintf(stderr, "OOM\n"); return 1; }
                pkgs[next].buf.size = 0;
                pkgs[next].h = curl_easy_init();
                curl_easy_setopt(pkgs[next].h, CURLOPT_URL, pkgs[next].url);
                curl_easy_setopt(pkgs[next].h, CURLOPT_WRITEFUNCTION, wcb);
                curl_easy_setopt(pkgs[next].h, CURLOPT_WRITEDATA, &pkgs[next].buf);
                curl_easy_setopt(pkgs[next].h, CURLOPT_TIMEOUT, 12L);
                curl_easy_setopt(pkgs[next].h, CURLOPT_PRIVATE, &pkgs[next]);
                curl_easy_setopt(pkgs[next].h, CURLOPT_USERAGENT, "BogartLinux-check-updates/10.0");
                if (pkgs[next].src_type == SRC_GITHUB || pkgs[next].src_type == SRC_GHTAG ||
                    pkgs[next].src_type == SRC_GHREFTAG)
                    add_auth_header(pkgs[next].h, auth_header);
                curl_multi_add_handle(multi, pkgs[next].h);
                pkgs[next].queued = 1; active++; next++; break;
            }
        }
        if (running) {
            struct timeval tv = {0, 50000};
            fd_set r, w, e; int mx = -1;
            FD_ZERO(&r); FD_ZERO(&w); FD_ZERO(&e);
            curl_multi_fdset(multi, &r, &w, &e, &mx);
            if (mx >= 0) select(mx + 1, &r, &w, &e, &tv);
        }
    }
    printf("\r" GREEN "Complete! (%d packages)\n\n" NC, n_total);
    int no = 0, nc_count = 0, na = 0, nn = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].cat != CAT_TRACK) continue;
        if (!pkgs[i].got[0] || !strcmp(pkgs[i].got, "NOT_FOUND"))
            { pkgs[i].status = ST_NOTFOUND; nn++; continue; }
        int c = ver_cmp(pkgs[i].inst, pkgs[i].got);
        if      (c == 0) { pkgs[i].status = ST_CURRENT;  nc_count++; }
        else if (c  > 0) { pkgs[i].status = ST_AHEAD;    na++; }
        else             { pkgs[i].status = ST_OUTDATED;  no++; }
    }
    int hno = 0, hnc = 0, hna = 0, hnn = 0, hskip = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].cat != CAT_HYPRLAND) continue;
        if (pkgs[i].status == ST_SKIP) { hskip++; continue; }
        if (!pkgs[i].got[0] || !strcmp(pkgs[i].got, "NOT_FOUND"))
            { pkgs[i].status = ST_NOTFOUND; hnn++; continue; }
        int c = ver_cmp(pkgs[i].inst, pkgs[i].got);
        if      (c == 0) { pkgs[i].status = ST_CURRENT;  hnc++; }
        else if (c  > 0) { pkgs[i].status = ST_AHEAD;    hna++; }
        else             { pkgs[i].status = ST_OUTDATED;  hno++; }
    }
    int bno = 0, bnc = 0, bna = 0, bnn = 0, bskip = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].cat != CAT_BLFS) continue;
        if (pkgs[i].status == ST_SKIP) { bskip++; continue; }
        if (!pkgs[i].got[0] || !strcmp(pkgs[i].got, "NOT_FOUND"))
            { pkgs[i].status = ST_NOTFOUND; bnn++; continue; }
        int c = ver_cmp(pkgs[i].inst, pkgs[i].got);
        if      (c == 0) { pkgs[i].status = ST_CURRENT;  bnc++; }
        else if (c  > 0) { pkgs[i].status = ST_AHEAD;    bna++; }
        else             { pkgs[i].status = ST_OUTDATED;  bno++; }
    }
    for (int i = 0; i < np; i++) {
        if (pkgs[i].got[0] && strcmp(pkgs[i].got, "NOT_FOUND")) {
            char cleaned[MAX_LEN];
            ver_clean(pkgs[i].got, cleaned, MAX_LEN);
            snprintf(pkgs[i].got, MAX_LEN, "%.*s", (int)(MAX_LEN - 1), cleaned);
        }
    }
    printf("\n" CYAN BOLD "Package(s) not found Arch Repo API (Needs ARCHMAP/SKIP)\n" NC);
    int any1 = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].status != ST_NOTFOUND || pkgs[i].cat != CAT_TRACK) continue;
        printf(YELLOW "~" NC " %-20s (queried: %s)\n", pkgs[i].name, pkgs[i].arch);
        any1 = 1;
    }
    if (!any1) printf(GREEN"None\n" NC);
    printf("\n" CYAN "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" NC);
    printf("Arch:     " RED "%3d outdated" NC " | " GREEN "%3d current" NC " | " BLUE "%3d ahead" NC
           " | " CYAN "%3d not found" NC " | " DIM "%3d skipped\n" NC,
           no, nc_count, na, nn, 0);
    printf("Hyprland: " RED "%3d outdated" NC " | " GREEN "%3d current" NC " | " BLUE "%3d ahead" NC
           " | " CYAN "%3d not found" NC " | " DIM "%3d skipped\n" NC,
           hno, hnc, hna, hnn, hskip);
    printf("BLFS:     " RED "%3d outdated" NC " | " GREEN "%3d current" NC " | " BLUE "%3d ahead" NC
           " | " CYAN "%3d not found" NC " | " DIM "%3d skipped\n\n" NC,
           bno, bnc, bna, bnn, bskip);
    printf("Skipped:  " DIM "%3d (Xorg/Toolchain/EOL)\n" NC, n_skip);
    printf("Total:    " BOLD GREEN "%3d packages\n\n" NC, np);
    printf(RED "OUTDATED\n" NC);
    int any = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].status != ST_OUTDATED || pkgs[i].cat != CAT_TRACK) continue;
        printf(YELLOW "%-23s" NC " installed: " RED "%-13s" NC " arch: " GREEN "%s\n" NC,
            pkgs[i].name, pkgs[i].inst, pkgs[i].got);
        any = 1;
    }
    if (!any) printf(GREEN "All current!\n" NC);
    int hany = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].status != ST_OUTDATED || pkgs[i].cat != CAT_HYPRLAND) continue;
        if (!hany) printf(RED "Hyprland\n" NC);
        printf(YELLOW "%-23s" NC " installed: " RED "%-13s" NC " github: " GREEN "%s\n" NC,
            pkgs[i].name, pkgs[i].inst, pkgs[i].got);
        hany = 1;
    }
    int bany = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].status != ST_OUTDATED || pkgs[i].cat != CAT_BLFS) continue;
        if (!bany) printf(RED "BLFS\n" NC);
        const char *src_label =
            pkgs[i].src_type == SRC_ARCH      ? "arch"   :
            pkgs[i].src_type == SRC_GITHUB    ? "github" :
            pkgs[i].src_type == SRC_GHTAG     ? "github" :
            pkgs[i].src_type == SRC_GHREFTAG  ? "github" :
            pkgs[i].src_type == SRC_GITLAB    ? "gitlab" : "?";
        printf(YELLOW "%-23s" NC " installed: " RED "%-13s" NC " %s: " GREEN "%s\n" NC,
            pkgs[i].name, pkgs[i].inst, src_label, pkgs[i].got);
        bany = 1;
    }
    printf("\n");
    curl_multi_cleanup(multi);
    curl_global_cleanup();
    for (int i = 0; i < np; i++) free(pkgs[i].buf.data);
    deps_free();

    return 0;
}
