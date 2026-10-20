/**
 * @file volume.c
 * @brief Motor volumétrico (2D com d=1 ou 3D): vizinhança como dado, regras com contagem até 26.
 *
 * Primeiro passo da arquitetura 3D (docs/ROADMAP.md). Independente do motor 2D (sim.c); um teste
 * diferencial garante que, com d=1 e vizinhança Moore 2D, os dois produzem estados idênticos.
 */
#include "mc.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

int neighborhood_make(NeighborhoodKind k, Neighborhood *out) {
    out->n = 0;
    for (int dz = -1; dz <= 1; dz++)
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int manhattan = abs(dx) + abs(dy) + abs(dz);
                if (manhattan == 0) continue;
                int keep = (k == NB_MOORE2D && dz == 0) || k == NB_MOORE3D || (k == NB_VN3D && manhattan == 1);
                if (!keep) continue;
                out->off[out->n][0] = (int8_t)dx;
                out->off[out->n][1] = (int8_t)dy;
                out->off[out->n][2] = (int8_t)dz;
                out->n++;
            }
    return (unsigned)k > NB_VN3D ? -1 : 0;
}

int neighborhood_parse(const char *s, NeighborhoodKind *out) {
    static const char *names[] = { "moore2d", "moore", "vn" };
    for (int i = 0; i < 3; i++)
        if (s && strcmp(s, names[i]) == 0) { *out = (NeighborhoodKind)i; return 0; }
    return -1;
}

const char *neighborhood_name(NeighborhoodKind k) {
    static const char *names[] = { "moore2d", "moore", "vn" };
    return (unsigned)k <= NB_VN3D ? names[k] : "?";
}

/**
 * @brief Lê uma parte da regra 3D ("B..." ou "S...") com números, faixas N-M e vírgulas.
 * @param p Cursor (avança). @param mask Máscara de saída. @param tag 'B' ou 'S'. @param maxc Contagem máxima.
 * @param s Texto completo (mensagens). @param err Mensagem. @param n Tamanho de @p err.
 * @return 0 ok; -1 inválida.
 */
static int parse_set3(const char **p, uint32_t *mask, char tag, int maxc, const char *s, char *err, size_t n) {
    if (toupper((unsigned char)**p) != tag) { snprintf(err, n, "regra \"%s\": esperado '%c'", s, tag); return -1; }
    (*p)++;
    *mask = 0;
    int compact = maxc <= 9 && !strchr(s, ',') && !strchr(s, '-'); /* "B3/S23": um dígito por contagem */
    while (isdigit((unsigned char)**p)) {
        long a = 0, b;
        if (compact) a = *(*p)++ - '0';
        else {
            int nd = 0;
            while (isdigit((unsigned char)**p)) { if (++nd > 3) break; a = a * 10 + (*(*p)++ - '0'); }
        }
        b = a;
        if (!compact && **p == '-') {
            (*p)++;
            if (!isdigit((unsigned char)**p)) { snprintf(err, n, "regra \"%s\": faixa incompleta em %c", s, tag); return -1; }
            b = 0;
            int nd = 0;
            while (isdigit((unsigned char)**p)) { if (++nd > 3) break; b = b * 10 + (*(*p)++ - '0'); }
        }
        if (a > maxc || b > maxc || b < a) {
            snprintf(err, n, "regra \"%s\": contagem/faixa %ld-%ld inválida (0..%d)", s, a, b, maxc);
            return -1;
        }
        for (long k = a; k <= b; k++) {
            if (*mask & (1u << k)) { snprintf(err, n, "regra \"%s\": contagem %ld repetida em %c", s, k, tag); return -1; }
            *mask |= 1u << k;
        }
        if (!compact && **p == ',') {
            (*p)++;
            if (!isdigit((unsigned char)**p)) { snprintf(err, n, "regra \"%s\": vírgula sem número em %c", s, tag); return -1; }
        }
    }
    return 0;
}

int rule3_parse(const char *s, int max_count, Rule3 *out, char *err, size_t n) {
    char dummy[1];
    if (!err || n == 0) { err = dummy; n = sizeof dummy; }
    if (!s || !out || max_count < 1 || max_count > 26) { snprintf(err, n, "argumento inválido"); return -1; }
    const char *p = s;
    Rule3 r;
    if (parse_set3(&p, &r.birth, 'B', max_count, s, err, n)) return -1;
    if (*p != '/') { snprintf(err, n, "regra \"%s\": esperado '/' após a parte B", s); return -1; }
    p++;
    if (parse_set3(&p, &r.survive, 'S', max_count, s, err, n)) return -1;
    if (*p) { snprintf(err, n, "regra \"%s\": caractere inesperado '%c'", s, *p); return -1; }
    *out = r;
    return 0;
}

/** @brief Escreve uma máscara como lista com faixas ("4-5,7"). @param m Máscara. @param buf Destino. @param n Tamanho. */
static void format_set3(uint32_t m, char *buf, size_t n) {
    size_t k = 0;
    buf[0] = '\0';
    for (int a = 0; a <= 26; a++) {
        if (!(m & (1u << a))) continue;
        int b = a;
        while (b + 1 <= 26 && (m & (1u << (b + 1)))) b++;
        int w = b > a ? snprintf(buf + k, n - k, "%s%d-%d", k ? "," : "", a, b) : snprintf(buf + k, n - k, "%s%d", k ? "," : "", a);
        if (w < 0 || (size_t)w >= n - k) return;
        k += (size_t)w;
        a = b;
    }
}

void rule3_format(Rule3 r, char *buf, size_t n) {
    char b[RULE3_STR_MAX], s[RULE3_STR_MAX];
    format_set3(r.birth, b, sizeof b);
    format_set3(r.survive, s, sizeof s);
    snprintf(buf, n, "B%s/S%s", b, s);
}

int volume_init(Volume *v, int w, int h, int d, Boundary b) {
    v->c = NULL;
    if (w < 3 || h < 3 || d < 1 || w > 1024 || h > 1024 || d > 1024 || (long long)w * h * d > (1LL << 26)) return -1;
    v->c = calloc((size_t)w * (size_t)h * (size_t)d, 1);
    if (!v->c) return -1;
    v->w = w;
    v->h = h;
    v->d = d;
    v->bound = b;
    return 0;
}

void volume_free(Volume *v) { free(v->c); v->c = NULL; }

void volume_soup(Volume *v, uint64_t seed, double density, double frac) {
    memset(v->c, 0, (size_t)v->w * (size_t)v->h * (size_t)v->d);
    int fw = (int)(v->w * frac), fh = (int)(v->h * frac), fd = v->d == 1 ? 1 : (int)(v->d * frac);
    int ox = (v->w - fw) / 2, oy = (v->h - fh) / 2, oz = (v->d - fd) / 2;
    uint64_t s = seed;
    for (int z = 0; z < fd; z++)
        for (int y = 0; y < fh; y++)
            for (int x = 0; x < fw; x++) *volume_at(v, ox + x, oy + y, oz + z) = rng_unit(&s) < density;
}

int volume_count(const Volume *v) {
    int n = 0;
    size_t N = (size_t)v->w * (size_t)v->h * (size_t)v->d;
    for (size_t i = 0; i < N; i++) n += v->c[i];
    return n;
}

int volume_diff(const Volume *a, const Volume *b) {
    int n = 0;
    size_t N = (size_t)a->w * (size_t)a->h * (size_t)a->d;
    for (size_t i = 0; i < N; i++) n += a->c[i] != b->c[i];
    return n;
}

uint64_t volume_hash(const Volume *v) {
    uint64_t h = 1469598103934665603ULL;
    size_t N = (size_t)v->w * (size_t)v->h * (size_t)v->d;
    for (size_t i = 0; i < N; i++) { h ^= v->c[i]; h *= 1099511628211ULL; }
    return h;
}

void volume_step(Rule3 r, const Neighborhood *nb, const Volume *src, Volume *dst) {
    for (int z = 0; z < src->d; z++)
        for (int y = 0; y < src->h; y++)
            for (int x = 0; x < src->w; x++) {
                int n = 0;
                for (int k = 0; k < nb->n; k++) {
                    int nx = mc_wrap(x + nb->off[k][0], src->w, src->bound);
                    int ny = mc_wrap(y + nb->off[k][1], src->h, src->bound);
                    int nz = mc_wrap(z + nb->off[k][2], src->d, src->bound);
                    if (nx >= 0 && ny >= 0 && nz >= 0) n += volume_get(src, nx, ny, nz);
                }
                uint32_t set = volume_get(src, x, y, z) ? r.survive : r.birth;
                *volume_at(dst, x, y, z) = (uint8_t)((set >> n) & 1u);
            }
    dst->bound = src->bound;
}

int volume_components(const Volume *v) {
    size_t N = (size_t)v->w * (size_t)v->h * (size_t)v->d;
    uint8_t *seen = calloc(N, 1);
    size_t *queue = malloc(sizeof(size_t) * N);
    if (!seen || !queue) { free(seen); free(queue); return -1; }
    Neighborhood nb;
    neighborhood_make(v->d == 1 ? NB_MOORE2D : NB_MOORE3D, &nb); /* 8-conexo no plano, 26-conexo em 3D */
    int comps = 0;
    for (size_t start = 0; start < N; start++) {
        if (!v->c[start] || seen[start]) continue;
        comps++;
        size_t head = 0, tail = 0;
        queue[tail++] = start;
        seen[start] = 1;
        while (head < tail) {
            size_t idx = queue[head++];
            int x = (int)(idx % (size_t)v->w), y = (int)(idx / (size_t)v->w % (size_t)v->h), z = (int)(idx / ((size_t)v->w * (size_t)v->h));
            for (int k = 0; k < nb.n; k++) {
                int nx = mc_wrap(x + nb.off[k][0], v->w, v->bound), ny = mc_wrap(y + nb.off[k][1], v->h, v->bound);
                int nz = mc_wrap(z + nb.off[k][2], v->d, v->bound);
                if (nx < 0 || ny < 0 || nz < 0) continue;
                size_t ni = ((size_t)nz * (size_t)v->h + (size_t)ny) * (size_t)v->w + (size_t)nx;
                if (v->c[ni] && !seen[ni]) { seen[ni] = 1; queue[tail++] = ni; }
            }
        }
    }
    free(seen);
    free(queue);
    return comps;
}

int volume_com(const Volume *v, double c[3]) {
    const int dims[3] = { v->w, v->h, v->d };
    double sc[3] = { 0 }, ss[3] = { 0 }, sum[3] = { 0 }, cnt = 0;
    for (int z = 0; z < v->d; z++)
        for (int y = 0; y < v->h; y++)
            for (int x = 0; x < v->w; x++) {
                if (!volume_get(v, x, y, z)) continue;
                const double p[3] = { x, y, z };
                for (int a = 0; a < 3; a++) {
                    double ang = 2.0 * 3.14159265358979323846 * p[a] / dims[a];
                    sc[a] += cos(ang);
                    ss[a] += sin(ang);
                    sum[a] += p[a];
                }
                cnt++;
            }
    if (cnt == 0) return 0;
    for (int a = 0; a < 3; a++) {
        if (v->bound != BOUND_TORUS) { c[a] = sum[a] / cnt; continue; }
        double m = atan2(ss[a], sc[a]) * dims[a] / (2.0 * 3.14159265358979323846);
        if (m < 0) m += dims[a];
        if (m >= dims[a]) m -= dims[a];
        c[a] = m;
    }
    return 1;
}

double volume_com_distance(const Volume *v, const double p[3], const double q[3]) {
    const int dims[3] = { v->w, v->h, v->d };
    double s = 0;
    for (int a = 0; a < 3; a++) {
        double d = fabs(q[a] - p[a]);
        if (v->bound == BOUND_TORUS && d > dims[a] - d) d = dims[a] - d;
        s += d * d;
    }
    return sqrt(s);
}

int rollout3(Rule3 r, const Neighborhood *nb, int steps, const Volume *init, Metrics *m, Volume *final_out) {
    Volume a, b;
    memset(m, 0, sizeof *m);
    m->steps = steps;
    m->sensitivity = -1.0;
    if (steps < 1 || volume_init(&a, init->w, init->h, init->d, init->bound)) return -1;
    if (volume_init(&b, init->w, init->h, init->d, init->bound)) { volume_free(&a); return -1; }
    uint64_t *hashes = malloc(sizeof(uint64_t) * ((size_t)steps + 1));
    if (!hashes) { volume_free(&a); volume_free(&b); return -1; }
    size_t bytes = (size_t)init->w * (size_t)init->h * (size_t)init->d;
    memcpy(a.c, init->c, bytes);
    double N = (double)bytes, dens = 0, act = 0;
    int alive = volume_count(&a), persisting = 1, t;
    hashes[0] = volume_hash(&a);
    double c0[3], c1[3];
    int has0 = volume_com(&a, c0);
    for (t = 1; t <= steps; t++) {
        volume_step(r, nb, &a, &b);
        int changed = volume_diff(&a, &b);
        alive = volume_count(&b);
        dens += alive / N;
        act += changed / N;
        if (persisting && alive > 0 && changed > 0) m->persistence++;
        else persisting = 0;
        hashes[t] = volume_hash(&b);
        if (!m->cycle_period)
            for (int s = t - 1; s >= 0; s--)
                if (hashes[s] == hashes[t]) { m->cycle_period = t - s; m->cycle_start = s; break; }
        Volume tmp = a; a = b; b = tmp;
        if (changed == 0) { dens += (double)(steps - t) * alive / N; break; } /* ponto fixo */
    }
    m->steps_simulated = t > steps ? steps : t;
    m->alive_final = alive;
    m->extinct = alive == 0;
    m->density_mean = dens / steps;
    m->activity_mean = act / steps;
    if (has0 && volume_com(&a, c1)) m->com_displacement = volume_com_distance(&a, c0, c1);
    m->components_final = volume_components(&a);
    if (final_out) memcpy(final_out->c, a.c, bytes);
    free(hashes);
    volume_free(&a);
    volume_free(&b);
    return 0;
}

int volume_slice(const Volume *v, int z, World *out) {
    if (z < 0 || z >= v->d || out->w != v->w || out->h != v->h) return -1;
    memcpy(out->c, v->c + (size_t)z * (size_t)v->w * (size_t)v->h, (size_t)v->w * (size_t)v->h);
    return 0;
}

void volume_project(const Volume *v, World *out) {
    for (int y = 0; y < v->h; y++)
        for (int x = 0; x < v->w; x++) {
            uint8_t any = 0;
            for (int z = 0; z < v->d && !any; z++) any = volume_get(v, x, y, z);
            *world_at(out, x, y) = any;
        }
}
