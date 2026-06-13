# Bogart Linux Package Manager

Custom package manager for Bogart Linux (LFS/BLFS-based).

## check-updates
Version checker for all tracked packages (448 packages).

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
Total confs : 219

Directory
```
mkdir -p /etc/bogartpkg/packages.d/
```

## Experimental
Fetching of packages direct from source. Version checker using flat sources.h - in progress.

Development: update-check11v.c

```
gcc -o check-updates update-checkv11.c deps.c -lcurl
```

Note: Package count may show higher not-found rate while sources.h is being populated.
