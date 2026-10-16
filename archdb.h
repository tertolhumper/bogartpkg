#ifndef ARCHDB_H
#define ARCHDB_H
int archdb_load(void);                  /* 0 ok, -1 nothing usable */
const char *archdb_ver(const char *n);  /* pkgver with epoch/pkgrel stripped; by name or pkgbase */
#endif
