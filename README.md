# Bogart Linux Package Manager

A source-based package management ecosystem for Bogart Linux. A custom
LFS/BLFS-based rolling Linux distribution built entirely from source.
Includes a source-based package builder, a version tracker for 459
packages, and a bidirectional dependency graph tool built in C.

## Architecture
```
bogartpkg install <pkg>
    │  download (3-URL fallback) → build (autotools/cmake/meson/cargo/custom) → porg install → rollback on failure
    ▼
porg database (/var/lib/porg/)
    │  bogartgraph --rescan <pkg>
    ▼
ELF dependency cache (/var/cache/bogartpkg-graph.db)
    │  bogartgraph --rdeps / --cascade
    ▼
bogartpkg cascade <pkg>
    │  rebuild dependents in topo-sorted order, report failures without aborting
    ▼
check-updates
    │  Arch Linux API (371 packages)
    │  GitHub releases / tags / refs/tags (Hyprland + BLFS)
    │  parallel fetch via libcurl multi (PARALLEL=3)
    ▼
outdated report — Arch | Hyprland | BLFS
```

## check-updates
Version tracker for 459 packages across Arch Linux API, GitHub releases, 
and BLFS sources. Written in C with libcurl for parallel fetching.

Stable: main.c catalog.c parse.c

Compilation
```
make
make install
```

## bogartpkg
Package builder & installer. Supports autotools, cmake, meson, cargo,
and custom build types. Tracks installed files via porg with 3-URL
fallback downloads and automatic dependency graph updates on install.

Stable: bogartpkgv3

``` 
install -m 755 bogartpkgv3 /usr/sbin/bogartpkg

```
## bogartpkg conf 
Total confs : 313

Directory
```
mkdir -p /etc/bogartpkg/packages.d/
```

## bogartgraph
Bidirectional dependency graph. It scans installed binaries via readelf
to build PROVIDES/NEEDS relationships. Supports reverse lookup,
topo-sorted cascade order, and Graphviz dot output.

Compilation 

```
cd bogartgraph
make
make install
```

## Current Limitations
- Source builds only
- No binary repository
- No package signatures yet
- Package definitions maintained manually

