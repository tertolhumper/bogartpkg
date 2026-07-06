# Bogart Linux Package Manager

Custom package manager for Bogart Linux (LFS/BLFS-based).

## check-updates
Version checker for all tracked packages (456 packages).

Stable: update-checkv9a.c | update-checkv9b.c for debugging

Compilation
```
gcc -o check-updates update-checkv9a.c deps.c -lcurl
install -m 755 check-updates /usr/sbin/check-updates
```

## bogartpkg
Package builder & installer.

Stable: bogartpkgv3

``` 
install -m 755 bogartpkgv3 /usr/sbin/bogartpkg

```
## bogartpkg conf 
Total confs : 303

Directory
```
mkdir -p /etc/bogartpkg/packages.d/
```

## bogartgraph
Graph for dependencies

Compilation 

```
gcc -o bogartgraph bogartgraph.c
install -m 755 bogartgraph /usr/sbin/bogartgraph
```
