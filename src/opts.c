/**
 * @file opts.c
 * @brief parsing de opções comuns ao CLI e ao visualizador.
 */
#include "opts.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

int parse_int(const char *s, long lo, long hi, long *out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || end == s || *end || v < lo || v > hi) return -1;
    *out = v;
    return 0;
}

int parse_u64(const char *s, uint64_t *out) {
    char *end;
    errno = 0;
    if (!s[0] || s[0] == '-') return -1;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno || *end) return -1;
    *out = v;
    return 0;
}

int parse_double(const char *s, double *out) {
    char *end;
    errno = 0;
    double v = strtod(s, &end);
    if (errno || end == s || *end) return -1;
    *out = v;
    return 0;
}

int parse_sim_opt(int argc, char **argv, int *i, SimConfig *c, uint64_t *world_seed) {
    const char *a = argv[*i];
    if (strcmp(a, "--no-perturb") == 0) { c->perturb = 0; return 1; }
    static const char *with_val[] = { "--w", "--h", "--boundary", "--steps", "--density", "--frac", "--world-seed" };
    int k = -1;
    for (int j = 0; j < 7; j++) if (strcmp(a, with_val[j]) == 0) k = j;
    if (k < 0) return 0;
    if (*i + 1 >= argc) { fprintf(stderr, "erro: %s exige um valor\n", a); return -1; }
    const char *v = argv[++*i];
    long n;
    int bad = 0;
    switch (k) {
    case 0: bad = parse_int(v, 3, 4096, &n); if (!bad) c->w = (int)n; break;
    case 1: bad = parse_int(v, 3, 4096, &n); if (!bad) c->h = (int)n; break;
    case 2: bad = boundary_parse(v, &c->bound); break;
    case 3: bad = parse_int(v, 1, 1000000, &n); if (!bad) c->steps = (int)n; break;
    case 4: bad = parse_double(v, &c->density); break;
    case 5: bad = parse_double(v, &c->soup_frac); break;
    case 6: bad = parse_u64(v, world_seed); break;
    }
    if (bad) { fprintf(stderr, "erro: valor inválido para %s: \"%s\"\n", a, v); return -1; }
    return 1;
}
