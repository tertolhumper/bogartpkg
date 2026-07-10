# Bogart Linux Package Manager

A complete package management ecosystem for Bogart Linux — a custom
LFS/BLFS-based rolling Linux distribution built entirely from source.
Includes a source-based package builder, a version tracker for 456
packages, and a bidirectional dependency graph tool built in C.


## check-updates
Version tracker for 456 packages across Arch Linux API, GitHub releases, 
GitLab, and BLFS sources. Written in C with libcurl for parallel fetching.

Stable: update-checkv9a.c | update-checkv9b.c for debugging

Compilation
```
gcc -o check-updates update-checkv9a.c deps.c -lcurl
install -m 755 check-updates /usr/sbin/check-updates
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
Total confs : 309

Directory
```
mkdir -p /etc/bogartpkg/packages.d/
```

## bogartgraph
Bidirectional dependency graph — scans installed binaries via readelf
to build PROVIDES/NEEDS relationships. Supports reverse lookup,
topo-sorted cascade order, and Graphviz dot output.

Compilation 

```
gcc -o bogartgraph bogartgraph.c
install -m 755 bogartgraph /usr/sbin/bogartgraph
```
