/**
 * @file search3d.c
 * @brief Busca de regras 3D (random / evo / map) com estado retomável e checkpoint binário próprio (`MC3D`).
 *
 * Mesmas estratégias e arquivo de diversidade do 2D (search.c), sobre o motor volumétrico (volume.c).
 * O formato `MC3D` é independente do `MCK1` do 2D, que não muda.
 */
#include "mc.h"

#include <stdlib.h>
#include <string.h>

/** @brief Máscara dos bits válidos para contagens 0..maxc. */
static uint32_t mask3(int maxc) { return maxc >= 31 ? 0xFFFFFFFFu : (1u << (maxc + 1)) - 1u; }

Rule3 rule3_random(int maxc, uint64_t *rng) {
    uint64_t x = rng_next(rng);
    Rule3 r = { (uint32_t)x & mask3(maxc), (uint32_t)(x >> 32) & mask3(maxc) };
    return r;
}

Rule3 rule3_mutate(Rule3 r, int maxc, uint64_t *rng) {
    uint32_t bit = rng_below(rng, (uint32_t)(2 * (maxc + 1)));
    if (bit <= (uint32_t)maxc) r.birth ^= 1u << bit;
    else r.survive ^= 1u << (bit - (uint32_t)maxc - 1);
    return r;
}

double rule3_score(Rule3 r, const Metrics *m) {
    if (r.birth & 1u) return 0.0;                      /* B0: o vazio pisca */
    if (m->activity_mean >= ARCH_ACT_MAX) return 0.0;  /* ruído: um quarto das células mudando por passo */
    if (m->density_mean >= SCORE_DENS_MAX) return 0.0; /* ruído: o volume fica coberto */
    return candidate_score(m);
}

SearchConfig3 search3_default_config(void) {
    SearchConfig3 c = { STRAT_MAP, 200, 16, 1, 1, NB_MOORE3D, 16, 16, 16, BOUND_TORUS, 60, 0.35, 0.5 };
    return c;
}

int search3_config_validate(const SearchConfig3 *c, char *err, size_t n) {
    if ((unsigned)c->strat > STRAT_MAP) { snprintf(err, n, "estratégia inválida"); return -1; }
    if ((unsigned)c->nb > NB_VN3D) { snprintf(err, n, "vizinhança inválida"); return -1; }
    if (c->budget < 1 || c->budget > 10000000 || c->pop < 1 || c->pop > 100000) { snprintf(err, n, "budget/pop fora do intervalo"); return -1; }
    if (c->w < 3 || c->h < 3 || c->d < 1 || c->w > 1024 || c->h > 1024 || c->d > 1024 || (long long)c->w * c->h * c->d > (1LL << 26)) {
        snprintf(err, n, "dimensões inválidas (lados 3..1024, d 1..1024, total <= 2^26)");
        return -1;
    }
    if (c->nb != NB_MOORE2D && c->d < 3) { snprintf(err, n, "vizinhança 3D exige d >= 3"); return -1; }
    if ((unsigned)c->bound > BOUND_REFLECT) { snprintf(err, n, "borda inválida"); return -1; }
    if (c->steps < 1 || c->steps > 1000000) { snprintf(err, n, "steps deve estar em 1..1000000"); return -1; }
    if (!(c->density >= 0.0 && c->density <= 1.0) || !(c->soup_frac >= 0.0 && c->soup_frac <= 1.0)) {
        snprintf(err, n, "density e frac devem estar em [0,1]");
        return -1;
    }
    return 0;
}

void search3_state_free(SearchState3 *st) {
    free(st->pop);
    free(st->cand);
    volume_free(&st->init);
    st->pop = NULL;
    st->cand = NULL;
    st->n = 0;
}

int search3_begin(SearchState3 *st, const SearchConfig3 *sc, char *err, size_t n) {
    memset(st, 0, sizeof *st);
    st->sc = *sc;
    st->rng = sc->seed;
    for (int j = 0; j < ARCH_BINS * ARCH_BINS; j++) st->elite[j] = -1;
    if (search3_config_validate(sc, err, n)) return -1;
    neighborhood_make(sc->nb, &st->nbh);
    if (volume_init(&st->init, sc->w, sc->h, sc->d, sc->bound)) { snprintf(err, n, "sem memória"); return -1; }
    volume_soup(&st->init, sc->world_seed, sc->density, sc->soup_frac);
    st->cand = calloc((size_t)sc->budget, sizeof(Candidate3));
    st->pop = malloc(sizeof(int) * (size_t)sc->pop);
    if (!st->cand || !st->pop) { search3_state_free(st); snprintf(err, n, "sem memória"); return -1; }
    return 0;
}

int search3_step(SearchState3 *st, char *err, size_t n) {
    const SearchConfig3 *sc = &st->sc;
    int i = st->n, maxc = st->nbh.n;
    if (i >= sc->budget) return 0;
    Candidate3 *C = st->cand, *c = &C[i];
    int parent = -1;
    if (i >= sc->pop && sc->strat == STRAT_EVO) { /* torneio binário */
        int a = st->pop[rng_below(&st->rng, (uint32_t)st->npop)], b = st->pop[rng_below(&st->rng, (uint32_t)st->npop)];
        parent = C[a].score >= C[b].score ? a : b;
    } else if (i >= sc->pop && sc->strat == STRAT_MAP) { /* elite de célula ocupada uniforme */
        int occ[ARCH_BINS * ARCH_BINS], k = 0;
        for (int j = 0; j < ARCH_BINS * ARCH_BINS; j++) if (st->elite[j] >= 0) occ[k++] = st->elite[j];
        parent = occ[rng_below(&st->rng, (uint32_t)k)];
    }
    c->id = i;
    c->parent = parent;
    c->gen = parent < 0 ? 0 : C[parent].gen + 1;
    c->rule = parent < 0 ? rule3_random(maxc, &st->rng) : rule3_mutate(C[parent].rule, maxc, &st->rng);
    if (rollout3(c->rule, &st->nbh, sc->steps, &st->init, &c->m, NULL)) { snprintf(err, n, "falha no rollout (memória)"); return -1; }
    c->score = rule3_score(c->rule, &c->m);
    c->cell = archive_cell(&c->m);
    int e = st->elite[c->cell];
    if (e < 0 || c->score > C[e].score) st->elite[c->cell] = i;
    if (sc->strat == STRAT_EVO) {
        if (st->npop < sc->pop) st->pop[st->npop++] = i;
        else {
            int w = 0;
            for (int j = 1; j < st->npop; j++) if (C[st->pop[j]].score < C[st->pop[w]].score) w = j;
            if (c->score > C[st->pop[w]].score) st->pop[w] = i;
        }
    }
    st->n = i + 1;
    return 1;
}

int search3_extend(SearchState3 *st, int budget) {
    if (budget <= st->sc.budget) return 0;
    Candidate3 *c = realloc(st->cand, sizeof(Candidate3) * (size_t)budget);
    if (!c) return -1;
    memset(c + st->sc.budget, 0, sizeof(Candidate3) * (size_t)(budget - st->sc.budget));
    st->cand = c;
    st->sc.budget = budget;
    return 0;
}

int search3_ranked(const SearchState3 *st, int *out) {
    int k = 0;
    for (int j = 0; j < ARCH_BINS * ARCH_BINS; j++) if (st->elite[j] >= 0) out[k++] = st->elite[j];
    for (int a = 1; a < k; a++) { /* inserção: k <= 100; score decrescente, empate por menor id */
        int v = out[a], b = a - 1;
        while (b >= 0 && (st->cand[out[b]].score < st->cand[v].score ||
                          (st->cand[out[b]].score == st->cand[v].score && out[b] > v))) { out[b + 1] = out[b]; b--; }
        out[b + 1] = v;
    }
    return k;
}

#define CK3_MAGIC "MC3D"

int search3_save(const SearchState3 *st, const char *path) {
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    uint32_t hdr[2] = { (uint32_t)sizeof(SearchConfig3), (uint32_t)sizeof(Candidate3) };
    int ok = fwrite(CK3_MAGIC, 1, 4, f) == 4 && fwrite(hdr, sizeof hdr, 1, f) == 1 && fwrite(&st->sc, sizeof st->sc, 1, f) == 1 &&
             fwrite(&st->rng, sizeof st->rng, 1, f) == 1 && fwrite(&st->n, sizeof(int), 1, f) == 1 &&
             fwrite(&st->npop, sizeof(int), 1, f) == 1 && fwrite(st->pop, sizeof(int), (size_t)st->sc.pop, f) == (size_t)st->sc.pop &&
             fwrite(st->elite, sizeof st->elite, 1, f) == 1 && fwrite(st->cand, sizeof(Candidate3), (size_t)st->n, f) == (size_t)st->n;
    if (fclose(f) || !ok) { remove(tmp); return -1; }
    return rename(tmp, path);
}

int search3_load(SearchState3 *st, const char *path, char *err, size_t n) {
    memset(st, 0, sizeof *st);
    if (n) err[0] = '\0';
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, n, "não foi possível abrir %s", path); return -1; }
    char magic[4];
    uint32_t hdr[2];
    SearchConfig3 sc;
    uint64_t rng;
    int cnt, npop;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, CK3_MAGIC, 4) || fread(hdr, sizeof hdr, 1, f) != 1 ||
        hdr[0] != sizeof(SearchConfig3) || hdr[1] != sizeof(Candidate3) || fread(&sc, sizeof sc, 1, f) != 1 ||
        fread(&rng, sizeof rng, 1, f) != 1 || fread(&cnt, sizeof cnt, 1, f) != 1 || fread(&npop, sizeof npop, 1, f) != 1) {
        fclose(f);
        snprintf(err, n, "%s: cabeçalho inválido ou não é checkpoint 3D desta versão/arquitetura", path);
        return -1;
    }
    char cerr[160];
    if (search3_config_validate(&sc, cerr, sizeof cerr) || cnt < 0 || cnt > sc.budget || npop < 0 || npop > sc.pop || npop > cnt ||
        search3_begin(st, &sc, cerr, sizeof cerr)) {
        fclose(f);
        snprintf(err, n, "%s: configuração inválida", path);
        search3_state_free(st);
        return -1;
    }
    st->rng = rng;
    st->npop = npop;
    int ok = fread(st->pop, sizeof(int), (size_t)sc.pop, f) == (size_t)sc.pop && fread(st->elite, sizeof st->elite, 1, f) == 1 &&
             fread(st->cand, sizeof(Candidate3), (size_t)cnt, f) == (size_t)cnt && fgetc(f) == EOF;
    fclose(f);
    uint32_t m = mask3(st->nbh.n);
    for (int i = 0; ok && i < npop; i++) ok = st->pop[i] >= 0 && st->pop[i] < cnt;
    for (int j = 0; ok && j < ARCH_BINS * ARCH_BINS; j++)
        ok = st->elite[j] >= -1 && st->elite[j] < cnt && (st->elite[j] < 0 || st->cand[st->elite[j]].cell == j);
    for (int i = 0; ok && i < cnt; i++) {
        const Candidate3 *c = &st->cand[i];
        ok = c->id == i && c->parent >= -1 && c->parent < i && c->gen == (c->parent < 0 ? 0 : st->cand[c->parent].gen + 1) &&
             c->cell >= 0 && c->cell < ARCH_BINS * ARCH_BINS && !(c->rule.birth & ~m) && !(c->rule.survive & ~m);
    }
    if (ok && sc.strat == STRAT_MAP && cnt >= sc.pop) {
        int any = 0;
        for (int j = 0; j < ARCH_BINS * ARCH_BINS; j++) any |= st->elite[j] >= 0;
        ok = any;
    }
    if (ok && sc.strat == STRAT_EVO && cnt >= sc.pop) ok = npop == sc.pop;
    if (!ok) { snprintf(err, n, "%s: conteúdo truncado ou inconsistente", path); search3_state_free(st); return -1; }
    st->n = cnt;
    return 0;
}

int checkpoint3_elite(const char *path, int rank, Rule3 *rule, SearchConfig3 *sc, int *nelites, char *err, size_t n) {
    SearchState3 st;
    if (search3_load(&st, path, err, n)) return -1;
    int rk[ARCH_BINS * ARCH_BINS], k = search3_ranked(&st, rk);
    if (nelites) *nelites = k;
    if (rank < 0 || rank >= k) { snprintf(err, n, "rank %d fora de 0..%d", rank, k - 1); search3_state_free(&st); return -1; }
    *rule = st.cand[rk[rank]].rule;
    *sc = st.sc;
    search3_state_free(&st);
    return 0;
}
