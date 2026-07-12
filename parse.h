#ifndef PARSE_H
#define PARSE_H

#include <stddef.h>

void split_porg(const char *entry, char *name, char *ver, size_t len);
void parse_ver(const char *json, char *out, size_t outlen);
void parse_gh_ver(const char *json, char *out, size_t outlen);
void parse_gh_tag_ver(const char *json, char *out, size_t outlen, const char *prefix);
void parse_gh_reftag_ver(const char *json, char *out, size_t outlen, const char *prefix);
void ver_clean(const char *in, char *out, size_t n);
int  ver_cmp(const char *a, const char *b);

#endif
