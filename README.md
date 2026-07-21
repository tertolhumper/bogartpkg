# Bogart Linux Package Manager

A source-based package management ecosystem for Bogart Linux. A custom
LFS/BLFS-based rolling Linux distribution built entirely from source.
Includes a source-based package builder, a version tracker for 461
packages, and a bidirectional dependency graph tool built in C.

## Features

- Source-based package builder
- Automatic build system detection
- ELF dependency graph generation
- Reverse dependency lookup
- Topological cascade rebuilds
- Parallel upstream version checking
- Rollback on installation failure
- File tracking via porg

## Architecture
```
bogartpkg install <pkg>
    │
    │  download (3-URL fallback)
    │  build (Autotools / CMake / Meson / Cargo / Custom)
    │  porg install
    │  rollback on failure
    ▼
porg database (/var/lib/porg/)
    │
    │  bogartgraph --rescan <pkg>
    ▼
ELF dependency cache (/var/cache/bogartpkg-graph.db)
    │
    │  PROVIDES → libfoo.so
    │  NEEDS    → libbar.so
    │
    │  bogartgraph --rdeps
    │  bogartgraph --cascade
    ▼
bogartpkg cascade <pkg>
    │
    │  rebuild dependents in topological order
    │  continue on failures and report summary
    ▼
check-updates
    │
    │  Arch Linux API 
    │  GitHub Releases / Tags / Refs
    │  Hyprland + BLFS sources
    │  parallel fetch via libcurl multi (PARALLEL=3)
    ▼
Outdated package report
    Arch | Hyprland | BLFS
```
## Requirements

- Linux From Scratch (LFS) base system
- porg
- libcurl
- graphviz

## check-updates
Version tracker for 461 packages across Arch Linux API, GitHub releases, 
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
Total confs : 319

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
## Limitations
- Source builds only, no binary repository
- No package signatures
- Package definitions maintained manually

## Roadmap
- GitLab API support
- bogartpkg rewrite in C
- Package signature verification
- Binary package cache
- Dependency Solver
