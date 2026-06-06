#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <ctype.h>
#include "deps.h"

#define MAX_PACKAGES 512
#define MAX_LEN      256
#define PARALLEL     1

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
    CAT_FONT     = 6,
    CAT_PYBUILD  = 7,
    CAT_BUILDTOOL= 8,
    CAT_CUSTOM   = 9,
    CAT_NOISE    = 10,
} Category;

static const char *cat_label[] = {
    [CAT_TRACK]     = "TRACKED",
    [CAT_LFS]       = "LFS base",
    [CAT_BLFS]      = "BLFS",
    [CAT_XORG]      = "Xorg/X11",
    [CAT_HYPRLAND]  = "Hyprland",
    [CAT_TOOLCHAIN] = "Toolchain",
    [CAT_FONT]      = "Font",
    [CAT_PYBUILD]   = "Python build",
    [CAT_BUILDTOOL] = "Build tool",
    [CAT_CUSTOM]    = "Custom",
    [CAT_NOISE]     = "Noise",
};

typedef struct { const char *name; Category cat; } CatEntry;
static const CatEntry CATALOG[] = {
    {"zlib",         CAT_LFS}, {"bzip2",       CAT_LFS},
    {"xz",           CAT_LFS}, {"lz4",         CAT_LFS},
    {"zstd",         CAT_LFS}, {"readline",    CAT_LFS},
    {"m4",           CAT_LFS}, {"flex",        CAT_LFS},
    {"gdbm",         CAT_LFS}, {"inetutils",   CAT_LFS},
    {"less",         CAT_LFS}, {"perl",        CAT_LFS},
    {"bison",        CAT_LFS}, {"grep",        CAT_LFS},
    {"bash",         CAT_LFS}, {"gawk",        CAT_LFS},
    {"coreutils",    CAT_LFS}, {"make",        CAT_LFS},
    {"patch",        CAT_LFS}, {"tar",         CAT_LFS},
    {"man-db",       CAT_LFS}, {"dbus",        CAT_LFS},
    {"iproute2",     CAT_LFS}, {"kbd",         CAT_LFS},
    {"libpipeline",  CAT_LFS}, {"shadow",      CAT_LFS},
    {"kmod",         CAT_LFS}, {"libcap",      CAT_LFS},
    {"attr",         CAT_LFS}, {"acl",         CAT_LFS},
    {"ncurses",      CAT_LFS}, {"sed",         CAT_LFS},
    {"diffutils",    CAT_LFS}, {"findutils",   CAT_LFS},
    {"file",         CAT_LFS}, {"procps-ng",   CAT_LFS},
    {"util-linux",   CAT_LFS}, {"e2fsprogs",   CAT_LFS},
    {"pkgconf",      CAT_LFS}, {"psmisc",      CAT_LFS},
    {"libxcrypt",    CAT_LFS}, {"linux-pam",   CAT_LFS},
    {"openssl",      CAT_LFS},
    {"expat",        CAT_LFS}, {"libffi",      CAT_LFS},
    {"gmp",          CAT_LFS}, {"mpfr",        CAT_LFS},
    {"mpc",          CAT_LFS}, {"bc",          CAT_LFS},
    {"texinfo",      CAT_LFS}, {"gettext",     CAT_LFS},
    {"groff",        CAT_LFS}, {"iana-etc",    CAT_LFS},
    {"man-pages",    CAT_LFS}, {"make-ca",     CAT_LFS},
    {"grub",         CAT_LFS}, {"sqlite-autoconf", CAT_LFS},
    {"tcl",          CAT_LFS},
    {"gcc",          CAT_TOOLCHAIN},
    {"glibc",        CAT_TOOLCHAIN},
    {"binutils",     CAT_TOOLCHAIN},
    {"clangd",       CAT_TOOLCHAIN},
    {"xorg-server",       CAT_XORG}, {"xwayland",         CAT_XORG},
    {"xrandr",            CAT_XORG}, {"xcursorgen",       CAT_XORG},
    {"xcursor-themes",    CAT_XORG}, {"xkbcomp",          CAT_XORG},
    {"mkfontscale",       CAT_XORG}, {"xcb-proto",        CAT_XORG},
    {"xcb-util",          CAT_XORG}, {"xcb-util-cursor",  CAT_XORG},
    {"xcb-util-errors",   CAT_XORG}, {"xcb-util-image",   CAT_XORG},
    {"xcb-util-keysyms",  CAT_XORG}, {"xcb-util-renderutil", CAT_XORG},
    {"xcb-util-wm",       CAT_XORG}, {"xorgproto",        CAT_XORG},
    {"util-macros",       CAT_XORG}, {"libxscrnsaver",    CAT_XORG},
    {"libxxf86dga",       CAT_XORG}, {"eglexternalplatform", CAT_XORG},
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
    {"font-adobe-utopia-type1", CAT_FONT}, {"font-alias",         CAT_FONT},
    {"font-bh-ttf",             CAT_FONT}, {"font-bh-type1",      CAT_FONT},
    {"font-ibm-type1",          CAT_FONT}, {"font-misc-ethiopic", CAT_FONT},
    {"font-util",               CAT_FONT}, {"font-xfree86-type1", CAT_FONT},
    {"encodings",               CAT_FONT},
    {"flit_core",    CAT_PYBUILD}, {"wheel",        CAT_PYBUILD},
    {"packaging",    CAT_PYBUILD}, {"setuptools",   CAT_PYBUILD},
    {"markupsafe",   CAT_PYBUILD}, {"jinja2",       CAT_PYBUILD},
    {"pyyaml",       CAT_PYBUILD}, {"docutils",     CAT_PYBUILD},
    {"cython",       CAT_PYBUILD},
    {"dejagnu",    CAT_BUILDTOOL}, {"expect",     CAT_BUILDTOOL},
    {"intltool",   CAT_BUILDTOOL}, {"xml-parser", CAT_BUILDTOOL},
    {"itstool",    CAT_BUILDTOOL}, {"scdoc",      CAT_BUILDTOOL},
    {"nasm",       CAT_BUILDTOOL}, {"yasm",       CAT_BUILDTOOL},
    {"asciidoc",   CAT_BUILDTOOL}, {"doxygen",    CAT_BUILDTOOL},
    {"gperf",      CAT_BUILDTOOL}, {"autoconf",   CAT_BUILDTOOL},
    {"automake",   CAT_BUILDTOOL}, {"libtool",    CAT_BUILDTOOL},
    {"swig",       CAT_BUILDTOOL}, {"texinfo",    CAT_BUILDTOOL},
    {"ncpamixer",            CAT_CUSTOM},
    {"ironbar",              CAT_CUSTOM},
    {"rofi-wayland",         CAT_CUSTOM},
    {"waypaper",             CAT_CUSTOM},
    {"porg",                 CAT_CUSTOM},
    {"yaml-cpp",             CAT_CUSTOM},
    {"xorriso",              CAT_CUSTOM},
    {"nvidia-vaapi-driver",  CAT_CUSTOM},
    {"libapparmor",          CAT_CUSTOM},
    {"nghttp2",              CAT_CUSTOM},
    {"nv-codec-headers",     CAT_CUSTOM},
    {"swww",                 CAT_CUSTOM},
    {"qt5",                  CAT_CUSTOM},
    {"qt6",                  CAT_CUSTOM},
    {"udis86",               CAT_CUSTOM},
    {"agg",                  CAT_CUSTOM},
    {"ps_mem",               CAT_CUSTOM},
    {"tailscale",            CAT_CUSTOM},
    {"lynis",                CAT_CUSTOM},
    {"kitty",                CAT_CUSTOM},
    {"go",                   CAT_CUSTOM},
    {"virglrenderer",        CAT_CUSTOM},
    {"build",      CAT_NOISE},
    {"src",        CAT_NOISE},
    {"sources",    CAT_NOISE},
    {"utils",      CAT_NOISE},
    {"profiles",   CAT_NOISE},
    {"aom-build",  CAT_NOISE},
    {"p11-build",  CAT_NOISE},
    {"source",     CAT_NOISE},
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
    {"udis86",              "vmt/udis86"},
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
    Buf      buf;
    CURL    *h;
    char     url[512];
    int      queued;
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
        strncpy(name, entry, nl); name[nl] = '\0';
        strncpy(ver, us + 1, len - 1); return;
    }
    int has_digit = 0;
    for (const char *p = entry; *p; p++)
        if (isdigit((unsigned char)*p)) { has_digit = 1; break; }
    if (!has_digit) { strncpy(name, entry, len - 1); return; }
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
            strncpy(name, entry, nl); name[nl] = '\0';
            strncpy(ver, p, len - 1);
        } else strncpy(name, entry, len - 1);
        return;
    }
    const char *best = NULL;
    for (int i = nc - 1; i >= 0; i--) {
        if (candidates[i] > entry && !isdigit((unsigned char)*(candidates[i] - 1)))
            { best = candidates[i]; break; }
    }
    if (!best) best = candidates[nc - 1];
    size_t nl = best - entry; if (nl >= len) nl = len - 1;
    strncpy(name, entry, nl); name[nl] = '\0';
    const char *vs = best + 1;
    if ((*vs == 'v' || *vs == 'r') && isdigit((unsigned char)*(vs + 1))) vs++;
    strncpy(ver, vs, len - 1);
    char *dash = strrchr(ver, '-');
    if (dash && *(dash + 1)) {
        int all_dig = 1;
        for (char *c = dash + 1; *c; c++)
            if (!isdigit((unsigned char)*c)) { all_dig = 0; break; }
        if (all_dig) *dash = '\0';
        else {
            char first[MAX_LEN]; size_t fl = dash - ver;
            if (fl < MAX_LEN) {
                strncpy(first, ver, fl); first[fl] = '\0';
                if (!strcmp(first, dash + 1)) *dash = '\0';
            }
        }
    }
}

void parse_ver(const char *json, char *out, size_t outlen) {
    out[0] = '\0';
    const char *results = strstr(json, "\"results\"");
    if (!results) { strncpy(out, "NOT_FOUND", outlen); return; }
    const char *arr = strchr(results, '[');
    if (!arr) { strncpy(out, "NOT_FOUND", outlen); return; }
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
        strncpy(obj, p, ol); obj[ol] = '\0';
        char ver[MAX_LEN] = "", arc[MAX_LEN] = "";
        const char *vt = strstr(obj, "\"pkgver\"");
        if (vt) { vt = strchr(vt, ':'); if (vt) { vt = strchr(vt, '"'); if (vt) { vt++;
            const char *ve = strchr(vt, '"');
            if (ve) { size_t l = ve - vt; if (l >= MAX_LEN) l = MAX_LEN - 1;
                strncpy(ver, vt, l); ver[l] = '\0'; }}}}
        const char *at = strstr(obj, "\"arch\"");
        if (at) { at = strchr(at, ':'); if (at) { at = strchr(at, '"'); if (at) { at++;
            const char *ae = strchr(at, '"');
            if (ae) { size_t l = ae - at; if (l >= MAX_LEN) l = MAX_LEN - 1;
                strncpy(arc, at, l); arc[l] = '\0'; }}}}
        if (ver[0]) {
            if (!strcmp(arc, "x86_64")) { strncpy(best_ver, ver, MAX_LEN - 1); found_x86 = 1; break; }
            else if (!found_x86 && !best_ver[0]) strncpy(best_ver, ver, MAX_LEN - 1);
        }
        p = oe;
    }
    strncpy(out, best_ver[0] ? best_ver : "NOT_FOUND", outlen);
}

void parse_gh_ver(const char *json, char *out, size_t outlen) {
    out[0] = '\0';
    const char *t = strstr(json, "\"tag_name\"");
    if (!t) { strncpy(out, "NOT_FOUND", outlen); return; }
    t = strchr(t, ':');
    if (!t) { strncpy(out, "NOT_FOUND", outlen); return; }
    t = strchr(t, '"');
    if (!t) { strncpy(out, "NOT_FOUND", outlen); return; }
    t++;
    const char *e = strchr(t, '"');
    if (!e) { strncpy(out, "NOT_FOUND", outlen); return; }
    size_t l = e - t;
    if (l >= outlen) l = outlen - 1;
    strncpy(out, t, l);
    out[l] = '\0';
    if (out[0] == 'v') memmove(out, out + 1, strlen(out));
}

void ver_clean(const char *in, char *out, size_t n) {
    strncpy(out, in, n - 1); out[n - 1] = '\0';
    if (out[0] == 'v') memmove(out, out + 1, strlen(out));
    char *c = strchr(out, '+'); if (c) *c = '\0';
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
    strncpy(av, ac, MAX_LEN); strncpy(bv, bc, MAX_LEN);
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

int main(void) {
    if (deps_load(NULL) != 0)
        fprintf(stderr, "warning: dep map unavailable, cascade check skipped\n");

    /* GitHub token for authenticated API requests */
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
        strncpy(pkgs[np].porg, line, MAX_LEN - 1);
        strncpy(pkgs[np].name, name, MAX_LEN - 1);
        strncpy(pkgs[np].inst, ver, MAX_LEN - 1);
        strncpy(pkgs[np].arch, arch_name(name), MAX_LEN - 1);
        pkgs[np].got[0]   = '\0';
        pkgs[np].cat      = cat;
        pkgs[np].status   = (cat != CAT_TRACK && cat != CAT_HYPRLAND) ? ST_SKIP : ST_NOTFOUND;
        pkgs[np].buf.data = calloc(1, 1);
        if (!pkgs[np].buf.data) { fprintf(stderr, "OOM\n"); return 1; }
        pkgs[np].buf.size = 0;
        pkgs[np].h        = NULL;
        pkgs[np].queued   = 0;
        if (cat == CAT_HYPRLAND) {
            const char *repo = gh_repo(name);
            if (repo)
                snprintf(pkgs[np].url, sizeof(pkgs[np].url),
                    "https://api.github.com/repos/%s/releases/latest", repo);
            else
                pkgs[np].status = ST_SKIP;
        } else {
            snprintf(pkgs[np].url, sizeof(pkgs[np].url),
                "https://archlinux.org/packages/search/json/?name=%s",
                pkgs[np].arch);
        }
        np++;
    }
    pclose(f);

    printf(CYAN "Bogart Linux Package Checker Against Arch/Hyprland Repo\n" NC);
    printf(CYAN "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n" NC);

    int n_track = 0, n_hypr = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].cat == CAT_TRACK) n_track++;
        if (pkgs[i].cat == CAT_HYPRLAND && pkgs[i].status != ST_SKIP) n_hypr++;
    }
    printf(CYAN "Tracking %d Arch | %d Hyprland packages...\n\n" NC, n_track, n_hypr);
    fflush(stdout);

    curl_global_init(CURL_GLOBAL_ALL);
    CURLM *multi = curl_multi_init();
    curl_multi_setopt(multi, CURLMOPT_MAXCONNECTS, (long)PARALLEL);

    int active = 0, done = 0, next = 0;
    for (int i = 0; i < np && active < PARALLEL; i++) {
        if (pkgs[i].cat != CAT_TRACK) continue;
        pkgs[i].h = curl_easy_init();
        curl_easy_setopt(pkgs[i].h, CURLOPT_URL, pkgs[i].url);
        curl_easy_setopt(pkgs[i].h, CURLOPT_WRITEFUNCTION, wcb);
        curl_easy_setopt(pkgs[i].h, CURLOPT_WRITEDATA, &pkgs[i].buf);
        curl_easy_setopt(pkgs[i].h, CURLOPT_TIMEOUT, 12L);
        curl_easy_setopt(pkgs[i].h, CURLOPT_PRIVATE, &pkgs[i]);
        curl_easy_setopt(pkgs[i].h, CURLOPT_USERAGENT, "BogartLinux-check-updates/3.0");
        curl_multi_add_handle(multi, pkgs[i].h);
        pkgs[i].queued = 1; active++; next = i + 1;
    }

    int running = 1;
    while (running || done < n_track) {
        curl_multi_perform(multi, &running);
        CURLMsg *msg; int ml;
        while ((msg = curl_multi_info_read(multi, &ml))) {
            if (msg->msg != CURLMSG_DONE) continue;
            Pkg *pk = NULL;
            curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &pk);
            if (pk) parse_ver(pk->buf.data, pk->got, MAX_LEN);
            curl_multi_remove_handle(multi, msg->easy_handle);
            curl_easy_cleanup(msg->easy_handle);
            active--; done++;
            printf("\r  " CYAN "Arch %d/%d" NC, done, n_track); fflush(stdout);
            while (next < np) {
                if (pkgs[next].queued || pkgs[next].cat != CAT_TRACK) { next++; continue; }
                pkgs[next].h = curl_easy_init();
                curl_easy_setopt(pkgs[next].h, CURLOPT_URL, pkgs[next].url);
                curl_easy_setopt(pkgs[next].h, CURLOPT_WRITEFUNCTION, wcb);
                curl_easy_setopt(pkgs[next].h, CURLOPT_WRITEDATA, &pkgs[next].buf);
                curl_easy_setopt(pkgs[next].h, CURLOPT_TIMEOUT, 12L);
                curl_easy_setopt(pkgs[next].h, CURLOPT_PRIVATE, &pkgs[next]);
                curl_easy_setopt(pkgs[next].h, CURLOPT_USERAGENT, "BogartLinux-check-updates/3.0");
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
    printf("\r  " GREEN "Arch complete!          \n" NC);

    active = 0; done = 0; next = 0;
    for (int i = 0; i < np; i++) pkgs[i].queued = 0;

    for (int i = 0; i < np && active < PARALLEL; i++) {
        if (pkgs[i].cat != CAT_HYPRLAND || pkgs[i].status == ST_SKIP) continue;
        free(pkgs[i].buf.data);
        pkgs[i].buf.data = calloc(1, 1);
        if (!pkgs[i].buf.data) { fprintf(stderr, "OOM\n"); return 1; }
        pkgs[i].buf.size = 0;
        pkgs[i].h = curl_easy_init();
        curl_easy_setopt(pkgs[i].h, CURLOPT_URL, pkgs[i].url);
        curl_easy_setopt(pkgs[i].h, CURLOPT_WRITEFUNCTION, wcb);
        curl_easy_setopt(pkgs[i].h, CURLOPT_WRITEDATA, &pkgs[i].buf);
        curl_easy_setopt(pkgs[i].h, CURLOPT_TIMEOUT, 12L);
        curl_easy_setopt(pkgs[i].h, CURLOPT_PRIVATE, &pkgs[i]);
        curl_easy_setopt(pkgs[i].h, CURLOPT_USERAGENT, "BogartLinux-check-updates/3.0");
        if (auth_header[0]) {
            struct curl_slist *headers = NULL;
            headers = curl_slist_append(headers, auth_header);
            curl_easy_setopt(pkgs[i].h, CURLOPT_HTTPHEADER, headers);
        }
        curl_multi_add_handle(multi, pkgs[i].h);
        pkgs[i].queued = 1; active++; next = i + 1;
    }

    running = 1;
    while (running || done < n_hypr) {
        curl_multi_perform(multi, &running);
        CURLMsg *msg; int ml;
        while ((msg = curl_multi_info_read(multi, &ml))) {
            if (msg->msg != CURLMSG_DONE) continue;
            Pkg *pk = NULL;
            curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &pk);
            if (pk) parse_gh_ver(pk->buf.data, pk->got, MAX_LEN);
            curl_multi_remove_handle(multi, msg->easy_handle);
            curl_easy_cleanup(msg->easy_handle);
            active--; done++;
            printf("\r  " MAGENTA "GitHub %d/%d" NC, done, n_hypr); fflush(stdout);
            while (next < np) {
                if (pkgs[next].queued || pkgs[next].cat != CAT_HYPRLAND || pkgs[next].status == ST_SKIP)
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
                curl_easy_setopt(pkgs[next].h, CURLOPT_USERAGENT, "BogartLinux-check-updates/3.0");
                if (auth_header[0]) {
                    struct curl_slist *headers = NULL;
                    headers = curl_slist_append(headers, auth_header);
                    curl_easy_setopt(pkgs[next].h, CURLOPT_HTTPHEADER, headers);
                }
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
    printf("\r  " GREEN "GitHub complete!          \n\n" NC);

    int no = 0, nc = 0, na = 0, nn = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].cat != CAT_TRACK) continue;
        if (!pkgs[i].got[0] || !strcmp(pkgs[i].got, "NOT_FOUND"))
            { pkgs[i].status = ST_NOTFOUND; nn++; continue; }
        int c = ver_cmp(pkgs[i].inst, pkgs[i].got);
        if      (c == 0) { pkgs[i].status = ST_CURRENT;  nc++; }
        else if (c  > 0) { pkgs[i].status = ST_AHEAD;    na++; }
        else             { pkgs[i].status = ST_OUTDATED;  no++; }
    }

    int hno = 0, hnc = 0, hna = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].cat != CAT_HYPRLAND || pkgs[i].status == ST_SKIP) continue;
        if (!pkgs[i].got[0] || !strcmp(pkgs[i].got, "NOT_FOUND"))
            { pkgs[i].status = ST_NOTFOUND; continue; }
        int c = ver_cmp(pkgs[i].inst, pkgs[i].got);
        if      (c == 0) { pkgs[i].status = ST_CURRENT;  hnc++; }
        else if (c  > 0) { pkgs[i].status = ST_AHEAD;    hna++; }
        else             { pkgs[i].status = ST_OUTDATED;  hno++; }
    }

    printf(RED BOLD "━━━ OUTDATED ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" NC);
    int any = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].status != ST_OUTDATED || pkgs[i].cat != CAT_TRACK) continue;
        printf("  " YELLOW "%-35s" NC " installed: " RED "%-20s" NC " arch: " GREEN "%s\n" NC,
            pkgs[i].name, pkgs[i].inst, pkgs[i].got);
        any = 1;
    }
    if (!any) printf("  " GREEN "All current!\n" NC);

    printf("\n" GREEN BOLD "━━━ UP TO DATE ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" NC);
    for (int i = 0; i < np; i++) {
        if (pkgs[i].status != ST_CURRENT || pkgs[i].cat != CAT_TRACK) continue;
        printf("  " GREEN "✓" NC " %-35s %s\n", pkgs[i].name, pkgs[i].inst);
    }

    printf("\n" BLUE BOLD "━━━ AHEAD OF ARCH ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" NC);
    any = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].status != ST_AHEAD || pkgs[i].cat != CAT_TRACK) continue;
        printf("  " BLUE "↑" NC " %-35s installed: " BLUE "%-20s" NC " arch: %s\n",
            pkgs[i].name, pkgs[i].inst, pkgs[i].got);
        any = 1;
    }
    if (!any) printf("  " GREEN "None\n" NC);

    printf("\n" CYAN BOLD "━━━ NOT FOUND IN ARCH (needs ARCHMAP or SKIP) ───────────────\n" NC);
    any = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].status != ST_NOTFOUND || pkgs[i].cat != CAT_TRACK) continue;
        printf("  " YELLOW "~" NC " %-35s (queried: %s)\n", pkgs[i].name, pkgs[i].arch);
        any = 1;
    }
    if (!any) printf("  " GREEN "None\n" NC);

    printf("\n" MAGENTA BOLD "━━━ HYPRLAND ECOSYSTEM (GitHub) ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" NC);
    for (int i = 0; i < np; i++) {
        if (pkgs[i].cat != CAT_HYPRLAND) continue;
        if (pkgs[i].status == ST_SKIP) {
            printf("  " DIM "~  %-33s (no GH mapping)\n" NC, pkgs[i].name);
            continue;
        }
        if (pkgs[i].status == ST_NOTFOUND) {
            printf("  " YELLOW "?  %-33s installed: %-15s github: NOT_FOUND\n" NC,
                pkgs[i].name, pkgs[i].inst);
            continue;
        }
        if (pkgs[i].status == ST_OUTDATED)
            printf("  " RED "↓  %-33s" NC " installed: " RED "%-15s" NC " github: " GREEN "%s\n" NC,
                pkgs[i].name, pkgs[i].inst, pkgs[i].got);
        else if (pkgs[i].status == ST_AHEAD)
            printf("  " BLUE "↑  %-33s" NC " installed: " BLUE "%-15s" NC " github: %s\n",
                pkgs[i].name, pkgs[i].inst, pkgs[i].got);
        else
            printf("  " GREEN "✓  %-33s" NC " %s\n", pkgs[i].name, pkgs[i].inst);
    }

    printf("\n" DIM BOLD "━━━ SKIPPED ─────────────────────────────────────────────────\n" NC);
    for (int cat = CAT_LFS; cat <= CAT_NOISE; cat++) {
        if (cat == CAT_HYPRLAND) continue;
        int count = 0;
        for (int i = 0; i < np; i++)
            if (pkgs[i].cat == cat) count++;
        if (!count) continue;
        printf("  " DIM "%-18s" NC " %d package%s\n",
            cat_label[cat], count, count == 1 ? "" : "s");
        for (int i = 0; i < np; i++) {
            if (pkgs[i].cat != cat) continue;
            char namever[MAX_LEN * 2];
            if (pkgs[i].inst[0])
                snprintf(namever, sizeof(namever), "%s-%s", pkgs[i].name, pkgs[i].inst);
            else
                snprintf(namever, sizeof(namever), "%s", pkgs[i].name);
            printf("    " DIM "%-36s" NC "\n", namever);
        }
    }

    printf("\n" CYAN "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n" NC);
    printf("  Arch:     " RED "%d outdated" NC " | " GREEN "%d current" NC " | " BLUE "%d ahead" NC
           " | " CYAN "%d not found" NC " | " DIM "%d skipped\n" NC,
           no, nc, na, nn, np - n_track - n_hypr);
    printf("  Hyprland: " RED "%d outdated" NC " | " GREEN "%d current" NC " | " BLUE "%d ahead\n" NC,
           hno, hnc, hna);

    const char *outdated_pkgs[MAX_PACKAGES + 1];
    int n_out = 0;
    for (int i = 0; i < np; i++) {
        if (pkgs[i].status == ST_OUTDATED &&
            (pkgs[i].cat == CAT_TRACK || pkgs[i].cat == CAT_HYPRLAND))
            outdated_pkgs[n_out++] = pkgs[i].name;
    }
    outdated_pkgs[n_out] = NULL;
    if (n_out > 0)
        deps_print_cascade(outdated_pkgs, 0);

    curl_multi_cleanup(multi);
    curl_global_cleanup();
    for (int i = 0; i < np; i++) free(pkgs[i].buf.data);

    deps_free();
    return 0;
}
