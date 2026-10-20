/**
 * @file world.c
 * @brief estado e fronteiras do universo.
 */
#include "mc.h"

#include <stdlib.h>
#include <string.h>

int world_init(World *w, int width, int height, Boundary b) {
    w->c = NULL;
    if (width < 3 || height < 3 || width > 4096 || height > 4096) return -1;
    w->c = calloc((size_t)width * (size_t)height, 1);
    if (!w->c) return -1;
    w->w = width;
    w->h = height;
    w->bound = b;
    return 0;
}

void world_free(World *w) { free(w->c); w->c = NULL; }
void world_clear(World *w) { memset(w->c, 0, (size_t)w->w * (size_t)w->h); }

void world_copy(World *dst, const World *src) {
    memcpy(dst->c, src->c, (size_t)src->w * (size_t)src->h);
    dst->bound = src->bound;
}

void world_soup(World *w, uint64_t seed, double density, double frac) {
    world_clear(w);
    int fw = (int)(w->w * frac), fh = (int)(w->h * frac);
    int ox = (w->w - fw) / 2, oy = (w->h - fh) / 2;
    uint64_t s = seed;
    for (int y = 0; y < fh; y++)
        for (int x = 0; x < fw; x++)
            *world_at(w, ox + x, oy + y) = rng_unit(&s) < density;
}

void world_perturb_center(World *w) {
    int cx = w->w / 2, cy = w->h / 2;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            uint8_t *p = world_at(w, cx + dx, cy + dy);
            *p = !*p;
        }
}

int world_count(const World *w) {
    int n = 0;
    size_t N = (size_t)w->w * (size_t)w->h;
    for (size_t i = 0; i < N; i++) n += w->c[i] == 1; /* viva = estado 1 (Generations: 2.. = morrendo) */
    return n;
}

int world_diff(const World *a, const World *b) {
    int n = 0;
    size_t N = (size_t)a->w * (size_t)a->h;
    for (size_t i = 0; i < N; i++) n += a->c[i] != b->c[i];
    return n;
}

uint64_t world_hash(const World *w) { /* FNV-1a 64 bits */
    uint64_t h = 1469598103934665603ULL;
    size_t N = (size_t)w->w * (size_t)w->h;
    for (size_t i = 0; i < N; i++) { h ^= w->c[i]; h *= 1099511628211ULL; }
    return h;
}

static const char *bnames[] = { "torus", "fixed", "reflect" };

int boundary_parse(const char *s, Boundary *out) {
    for (int i = 0; i < 3; i++)
        if (s && strcmp(s, bnames[i]) == 0) { *out = (Boundary)i; return 0; }
    return -1;
}

const char *boundary_name(Boundary b) { return (unsigned)b < 3 ? bnames[b] : "?"; }
