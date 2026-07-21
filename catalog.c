#include <string.h>
#include "catalog.h"

static const BlfsSrc BLFSSRC[] = {
    /* SRC_ARCH */
    {"asciidoc",            SRC_ARCH,     NULL,                          NULL},
    {"cython",              SRC_ARCH,     NULL,                          NULL},
    {"docutils",            SRC_ARCH,     "python-docutils",             NULL},
    {"intltool",            SRC_ARCH,     NULL,                          NULL},
    {"itstool",             SRC_ARCH,     NULL,                          NULL},
    {"libapparmor",         SRC_ARCH,     "apparmor",                    NULL},
    {"linux-pam",           SRC_ARCH,     "pam",                         NULL},
    {"nasm",                SRC_ARCH,     NULL,                          NULL},
    {"nghttp2",             SRC_ARCH,     "libnghttp2",                  NULL},
    {"pyyaml",              SRC_ARCH,     "python-yaml",                 NULL},
    {"scdoc",               SRC_ARCH,     NULL,                          NULL},
    {"swig",                SRC_ARCH,     NULL,                          NULL},
    {"virglrenderer",       SRC_ARCH,     NULL,                          NULL},
    {"xml-parser",          SRC_ARCH,     "perl-xml-parser",             NULL},
    {"yaml-cpp",            SRC_ARCH,     NULL,                          NULL},
    {"yasm",                SRC_ARCH,     NULL,                          NULL},
    /* SRC_GITHUB */
    {"agg",                 SRC_GITHUB,   "asciinema/agg",               NULL},
    {"bochs",               SRC_GITHUB,   "bochs-emu/Bochs",             NULL},
    {"doxygen",             SRC_GITHUB,   "doxygen/doxygen",             NULL},
    {"ironbar",             SRC_GITHUB,   "JakeStanger/ironbar",         NULL},
    {"kitty",               SRC_GITHUB,   "kovidgoyal/kitty",            NULL},
    {"lynis",               SRC_GITHUB,   "CISOfy/lynis",                NULL},
    {"make-ca",             SRC_GITHUB,   "lfs-book/make-ca",            NULL},
    {"nv-codec-headers",    SRC_GITHUB,   "FFmpeg/nv-codec-headers",     NULL},
    {"nvidia-vaapi-driver", SRC_GITHUB,   "elFarto/nvidia-vaapi-driver", NULL},
    {"rofi-wayland",        SRC_GITHUB,   "lbonn/rofi",                  NULL},
    {"rusty-psn",           SRC_GITHUB,   "RainbowCookie32/rusty-psn",   NULL},
    {"shadow",              SRC_GITHUB,   "shadow-maint/shadow",         NULL},
    {"sudo",                SRC_GITHUB,   "sudo-project/sudo", 		 NULL},
    {"swww",                SRC_GITHUB,   "LGFae/swww",                  NULL},
    {"tailscale",           SRC_GITHUB,   "tailscale/tailscale",         NULL},
    {"waypaper",            SRC_GITHUB,   "anufrievroman/waypaper",      NULL},
    /* SRC_GHTAG */
    {"ncpamixer",           SRC_GHTAG,    "fulhax/ncpamixer",            NULL},
    {"ps_mem",              SRC_GHTAG,    "pixelb/ps_mem",               NULL},
    {"qt6",                 SRC_GHTAG,    "qt/qtbase",                   "v6."},
    {"udis86",              SRC_GHTAG,    "vmt/udis86",                  NULL},
    {"porg",                SRC_GHTAG,    "drfiemost/porg",              NULL},
    /* SRC_GHREFTAG */
    {"go",                  SRC_GHREFTAG, "golang/go",                   "go1."},
    /* SRC_SKIP */
    {"qt5",                 SRC_SKIP,     NULL,                          NULL},
    {"xorriso",             SRC_SKIP,     NULL,                          NULL},
    {"usb-modeswitch",      SRC_SKIP,     NULL,                          NULL},
    {"android-studio",      SRC_SKIP,     NULL, 			 NULL},
    {NULL, 0, NULL, NULL}
};

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
    {"flit-core",        "python-flit-core"},
    {"jinja2",           "python-jinja"},
    {"markupsafe",       "python-markupsafe"},
    {"packaging",        "python-packaging"},
    {"setuptools",       "python-setuptools"},
    {"wheel",            "python-wheel"},
    {NULL, NULL}
};

static const CatEntry CATALOG[] = {
    /* TOOLCHAIN */
    {"gcc",              CAT_TOOLCHAIN},
    {"glibc",           CAT_TOOLCHAIN},
    {"binutils",        CAT_TOOLCHAIN},
    {"clangd",          CAT_TOOLCHAIN},
    /* HYPRLAND */
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
    /* XORG */
    {"xorg-server",             CAT_XORG}, {"encodings",             CAT_XORG},
    {"xrandr",                  CAT_XORG}, {"xcursorgen",            CAT_XORG},
    {"xcursor-themes",          CAT_XORG}, {"xkbcomp",               CAT_XORG},
    {"mkfontscale",             CAT_XORG}, {"xcb-proto",             CAT_XORG},
    {"xcb-util",                CAT_XORG}, {"xcb-util-cursor",       CAT_XORG},
    {"xcb-util-errors",         CAT_XORG}, {"xcb-util-image",        CAT_XORG},
    {"xcb-util-keysyms",        CAT_XORG}, {"xcb-util-renderutil",   CAT_XORG},
    {"xcb-util-wm",             CAT_XORG}, {"xorgproto",             CAT_XORG},
    {"util-macros",             CAT_XORG}, {"libxscrnsaver",         CAT_XORG},
    {"libxxf86dga",             CAT_XORG}, {"eglexternalplatform",   CAT_XORG},
    {"font-adobe-utopia-type1", CAT_XORG}, {"font-alias",            CAT_XORG},
    {"font-bh-ttf",             CAT_XORG}, {"font-bh-type1",         CAT_XORG},
    {"font-ibm-type1",          CAT_XORG}, {"font-misc-ethiopic",    CAT_XORG},
    {"font-util",               CAT_XORG}, {"font-xfree86-type1",    CAT_XORG},
    /* BLFS */
    {"pyyaml",           CAT_BLFS}, {"docutils",           CAT_BLFS},
    {"cython",           CAT_BLFS}, {"go",                 CAT_BLFS},
    {"intltool",         CAT_BLFS}, {"xml-parser",         CAT_BLFS},
    {"itstool",          CAT_BLFS}, {"scdoc",              CAT_BLFS},
    {"nasm",             CAT_BLFS}, {"yasm",               CAT_BLFS},
    {"asciidoc",         CAT_BLFS}, {"doxygen",            CAT_BLFS},
    {"swig",             CAT_BLFS}, {"xorriso",            CAT_BLFS},
    {"linux-pam",        CAT_BLFS}, {"make-ca",            CAT_BLFS},
    {"libapparmor",      CAT_BLFS}, {"nghttp2",            CAT_BLFS},
    {"qt6",              CAT_BLFS}, {"lynis",              CAT_BLFS},
    {"virglrenderer",    CAT_BLFS}, {"kitty",              CAT_BLFS},
    {"nv-codec-headers", CAT_BLFS}, {"qt5",                CAT_BLFS},
    {"yaml-cpp",         CAT_BLFS}, {"ncpamixer",          CAT_BLFS},
    {"ironbar",          CAT_BLFS}, {"rofi-wayland",       CAT_BLFS},
    {"waypaper",         CAT_BLFS}, {"swww",               CAT_BLFS},
    {"porg",             CAT_BLFS}, {"ps_mem",             CAT_BLFS},
    {"agg",              CAT_BLFS}, {"udis86",             CAT_BLFS},
    {"tailscale",        CAT_BLFS}, {"shadow",             CAT_BLFS},
    {"bochs",            CAT_BLFS}, {"nvidia-vaapi-driver",CAT_BLFS},
    {"usb-modeswitch",   CAT_BLFS}, {"rusty-psn",          CAT_BLFS},
    {"sudo", 		 CAT_BLFS}, {"android-studio",     CAT_BLFS},
    {NULL, 0}
};

const BlfsSrc *blfs_src_lookup(const char *name) {
    for (int i = 0; BLFSSRC[i].name; i++)
        if (!strcmp(name, BLFSSRC[i].name)) return &BLFSSRC[i];
    return NULL;
}

const char *gh_repo(const char *name) {
    for (int i = 0; GHMAP[i].name; i++)
        if (!strcmp(name, GHMAP[i].name)) return GHMAP[i].repo;
    return NULL;
}

const char *arch_name(const char *n) {
    for (int i = 0; ARCHMAP[i].from; i++)
        if (!strcmp(n, ARCHMAP[i].from)) return ARCHMAP[i].to;
    return n;
}

Category get_category(const char *name) {
    for (int i = 0; CATALOG[i].name; i++)
        if (!strcmp(name, CATALOG[i].name)) return CATALOG[i].cat;
    if (strlen(name) <= 1) return CAT_NOISE;
    return CAT_TRACK;
}
