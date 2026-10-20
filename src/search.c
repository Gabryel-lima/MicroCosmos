/**
 * @file search.c
 * @brief baseline aleatório, busca evolutiva (estado estacionário) e MAP-Elites mínimo.
 */
#include "mc.h"

#include <stdlib.h>
#include <string.h>

/* Score declarado (heurística, não medida de "interesse"):
 * persistência normalizada × min(1, componentes_finais/16).
 * Favorece mundos que não morrem/congelam e terminam com vários objetos separados. */
double candidate_score(const Metrics *m) {
    double p = m->steps ? (double)m->persistence / m->steps : 0.0;
    double c = m->components_final / 16.0;
    if (c > 1.0) c = 1.0;
    if (c < 0.0) c = 0.0;
    return p * c;
}

/** @brief Bin de um valor normalizado. @param v Valor (idealmente em [0,1]). @return Bin clampado em [0, ARCH_BINS). */
double rule_score(Rule r, const Metrics *m) {
    if (r.birth & 1u) return 0.0;                      /* B0: o vazio pisca (ruído observado) */
    if (m->activity_mean >= ARCH_ACT_MAX) return 0.0;  /* >= 25% das células mudando por passo: ruído */
    if (m->density_mean >= SCORE_DENS_MAX) return 0.0; /* enche o mundo (sopa inicial ~12,5%): ruído observado */
    return candidate_score(m);
}

/** @brief Bin de um valor normalizado. @param v Valor (idealmente em [0,1]). @return Bin clampado em [0, ARCH_BINS). */
static int bin(double v) {
    int b = (int)(v * ARCH_BINS);
    if (b < 0) b = 0;
    if (b >= ARCH_BINS) b = ARCH_BINS - 1;
    return b;
}

/* Eixo X: persistência normalizada; eixo Y: atividade média / ARCH_ACT_MAX. */
int archive_cell(const Metrics *m) {
    double p = m->steps ? (double)m->persistence / m->steps : 0.0;
    return bin(m->activity_mean / ARCH_ACT_MAX) * ARCH_BINS + bin(p);
}

void archive_clear(Archive *a) {
    for (int i = 0; i < ARCH_BINS * ARCH_BINS; i++) a->elite[i] = -1;
}

int archive_offer(Archive *a, const Candidate *all, int idx) {
    int c = all[idx].cell, e = a->elite[c];
    if (e < 0 || all[idx].score > all[e].score) { a->elite[c] = idx; return 1; }
    return 0;
}

/** @brief Comparador crescente de double para qsort. */
static int cmp_double(const void *x, const void *y) {
    double a = *(const double *)x, b = *(const double *)y;
    return (a > b) - (a < b);
}

double median_of(double *v, int n) {
    if (n <= 0) return 0.0;
    qsort(v, (size_t)n, sizeof *v, cmp_double);
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

int search_begin(SearchState *st, const SearchConfig *sc, char *err, size_t n) {
    memset(st, 0, sizeof *st);
    st->sc = *sc;
    st->rng = sc->seed;
    archive_clear(&st->res.arch);
    if (sc->budget < 1 || sc->pop < 1) { snprintf(err, n, "budget e pop devem ser >= 1"); return -1; }
    if (sim_config_validate(&sc->sim, err, n)) return -1;
    if (world_init(&st->init, sc->sim.w, sc->sim.h, sc->sim.bound)) { snprintf(err, n, "sem memória"); return -1; }
    world_soup(&st->init, sc->world_seed, sc->sim.density, sc->sim.soup_frac);
    st->res.cand = calloc((size_t)sc->budget, sizeof(Candidate));
    st->pop = malloc(sizeof(int) * (size_t)sc->pop);
    if (!st->res.cand || !st->pop) { search_state_free(st); snprintf(err, n, "sem memória"); return -1; }
    return 0;
}

/* Avalia o próximo candidato. 1 = avaliou, 0 = orçamento esgotado, -1 = erro. */
int search_step(SearchState *st, char *err, size_t n) {
    const SearchConfig *sc = &st->sc;
    int i = st->res.n;
    if (i >= sc->budget) return 0;
    Candidate *C = st->res.cand, *c = &C[i];
    int parent = -1;
    if (i >= sc->pop && sc->strat == STRAT_EVO) { /* torneio binário na população */
        int a = st->pop[rng_below(&st->rng, (uint32_t)st->npop)], b = st->pop[rng_below(&st->rng, (uint32_t)st->npop)];
        parent = C[a].score >= C[b].score ? a : b;
    } else if (i >= sc->pop && sc->strat == STRAT_MAP) { /* elite de célula ocupada uniforme */
        int occ[ARCH_BINS * ARCH_BINS], k = 0;
        for (int j = 0; j < ARCH_BINS * ARCH_BINS; j++) if (st->res.arch.elite[j] >= 0) occ[k++] = st->res.arch.elite[j];
        parent = occ[rng_below(&st->rng, (uint32_t)k)];
    }
    c->id = i;
    c->parent = parent;
    c->gen = parent < 0 ? 0 : C[parent].gen + 1;
    c->rule = parent < 0 ? rule_random(&st->rng) : rule_mutate(C[parent].rule, &st->rng);
    c->world_seed = sc->world_seed;
    if (rollout(c->rule, &sc->sim, &st->init, &c->m, NULL)) { snprintf(err, n, "falha no rollout (memória)"); return -1; }
    c->score = rule_score(c->rule, &c->m); /* ruído (B0 ou atividade alta) = 0, mas segue no arquivo */
    c->cell = archive_cell(&c->m);
    archive_offer(&st->res.arch, C, i);
    if (sc->strat == STRAT_EVO) {
        if (st->npop < sc->pop) st->pop[st->npop++] = i;
        else {
            int w = 0;
            for (int j = 1; j < st->npop; j++) if (C[st->pop[j]].score < C[st->pop[w]].score) w = j;
            if (c->score > C[st->pop[w]].score) st->pop[w] = i;
        }
    }
    st->res.n = i + 1;
    return 1;
}

void search_stats(SearchResult *res) {
    const Candidate *C = res->cand;
    double *scores = malloc(sizeof(double) * (size_t)(res->n ? res->n : 1));
    int ext = 0;
    res->best_score = 0;
    res->coverage = 0;
    res->qd_score = 0;
    for (int i = 0; i < res->n; i++) {
        if (scores) scores[i] = C[i].score;
        if (C[i].score > res->best_score) res->best_score = C[i].score;
        ext += C[i].m.extinct;
    }
    res->median_score = scores ? median_of(scores, res->n) : 0;
    free(scores);
    res->extinct_frac = res->n ? (double)ext / res->n : 0;
    for (int j = 0; j < ARCH_BINS * ARCH_BINS; j++)
        if (res->arch.elite[j] >= 0) { res->coverage++; res->qd_score += C[res->arch.elite[j]].score; }
}

int search_extend(SearchState *st, int budget) {
    if (budget <= st->sc.budget) return 0;
    Candidate *c = realloc(st->res.cand, sizeof(Candidate) * (size_t)budget);
    if (!c) return -1;
    memset(c + st->sc.budget, 0, sizeof(Candidate) * (size_t)(budget - st->sc.budget));
    st->res.cand = c;
    st->sc.budget = budget;
    return 0;
}

void search_state_free(SearchState *st) {
    free(st->pop);
    st->pop = NULL;
    world_free(&st->init);
    search_free(&st->res);
}

int search_run(const SearchConfig *sc, SearchResult *res, char *err, size_t n) {
    SearchState st;
    if (search_begin(&st, sc, err, n)) { memset(res, 0, sizeof *res); return -1; }
    int r;
    while ((r = search_step(&st, err, n)) == 1) {}
    if (r < 0) { search_state_free(&st); memset(res, 0, sizeof *res); return -1; }
    search_stats(&st.res);
    *res = st.res;
    st.res.cand = NULL;
    search_state_free(&st);
    return 0;
}

/* ---------- Checkpoint binário (mesma arquitetura/build; versão no cabeçalho) ---------- */
#define CK_MAGIC "MCK1"

int search_save(const SearchState *st, const char *path) {
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    uint32_t hdr[3] = { (uint32_t)sizeof(SearchConfig), (uint32_t)sizeof(Candidate), (uint32_t)sizeof(Archive) };
    int ok = fwrite(CK_MAGIC, 1, 4, f) == 4 && fwrite(hdr, sizeof hdr, 1, f) == 1 && fwrite(&st->sc, sizeof st->sc, 1, f) == 1 &&
             fwrite(&st->rng, sizeof st->rng, 1, f) == 1 && fwrite(&st->res.n, sizeof(int), 1, f) == 1 &&
             fwrite(&st->npop, sizeof(int), 1, f) == 1 && fwrite(st->pop, sizeof(int), (size_t)st->sc.pop, f) == (size_t)st->sc.pop &&
             fwrite(&st->res.arch, sizeof(Archive), 1, f) == 1 &&
             fwrite(st->res.cand, sizeof(Candidate), (size_t)st->res.n, f) == (size_t)st->res.n;
    if (fclose(f) || !ok) { remove(tmp); return -1; }
    return rename(tmp, path); /* escrita atômica: um checkpoint interrompido não corrompe o anterior */
}

int search_load(SearchState *st, const char *path, char *err, size_t n) {
    memset(st, 0, sizeof *st);
    if (n) err[0] = '\0';
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, n, "não foi possível abrir %s", path); return -1; }
    char magic[4];
    uint32_t hdr[3];
    SearchConfig sc;
    uint64_t rng;
    int cnt, npop;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, CK_MAGIC, 4) || fread(hdr, sizeof hdr, 1, f) != 1 ||
        hdr[0] != sizeof(SearchConfig) || hdr[1] != sizeof(Candidate) || hdr[2] != sizeof(Archive) ||
        fread(&sc, sizeof sc, 1, f) != 1 || fread(&rng, sizeof rng, 1, f) != 1 || fread(&cnt, sizeof cnt, 1, f) != 1 ||
        fread(&npop, sizeof npop, 1, f) != 1) {
        fclose(f);
        snprintf(err, n, "%s: cabeçalho inválido ou de outra versão/arquitetura", path);
        return -1;
    }
    if ((unsigned)sc.strat > STRAT_MAP || sc.pop < 1 || sc.pop > 100000 || sc.budget < 1 || cnt < 0 || cnt > sc.budget ||
        npop < 0 || npop > sc.pop || npop > cnt || search_begin(st, &sc, err, n)) {
        fclose(f);
        if (!err[0]) snprintf(err, n, "%s: configuração inválida", path);
        search_state_free(st);
        return -1;
    }
    st->rng = rng;
    st->npop = npop;
    int ok = fread(st->pop, sizeof(int), (size_t)sc.pop, f) == (size_t)sc.pop && fread(&st->res.arch, sizeof(Archive), 1, f) == 1 &&
             fread(st->res.cand, sizeof(Candidate), (size_t)cnt, f) == (size_t)cnt && fgetc(f) == EOF;
    fclose(f);
    for (int i = 0; ok && i < npop; i++) ok = st->pop[i] >= 0 && st->pop[i] < cnt;
    for (int j = 0; ok && j < ARCH_BINS * ARCH_BINS; j++) ok = st->res.arch.elite[j] >= -1 && st->res.arch.elite[j] < cnt;
    for (int i = 0; ok && i < cnt; i++) {
        const Candidate *c = &st->res.cand[i];
        ok = c->id == i && c->parent >= -1 && c->parent < i && c->gen == (c->parent < 0 ? 0 : st->res.cand[c->parent].gen + 1) &&
             c->cell >= 0 && c->cell < ARCH_BINS * ARCH_BINS &&
             !((c->rule.birth | c->rule.survive) & ~RULE_MASK);
    }
    if (ok && sc.strat == STRAT_MAP && cnt >= sc.pop) { /* MAP precisa de ao menos uma elite para escolher pai */
        int any = 0;
        for (int j = 0; j < ARCH_BINS * ARCH_BINS; j++) any |= st->res.arch.elite[j] >= 0;
        ok = any;
    }
    if (ok && sc.strat == STRAT_EVO && cnt >= sc.pop) ok = npop == sc.pop;
    if (!ok) { snprintf(err, n, "%s: conteúdo truncado ou inconsistente", path); search_state_free(st); return -1; }
    st->res.n = cnt;
    return 0;
}

/* Carrega um checkpoint e devolve a regra da elite de posição `rank` (0 = melhor) e a configuração usada. */
int checkpoint_elite(const char *path, int rank, Rule *rule, SearchConfig *sc, int *nelites, char *err, size_t n) {
    SearchState st;
    if (search_load(&st, path, err, n)) return -1;
    int rk[ARCH_BINS * ARCH_BINS], k = archive_ranked(&st.res, rk);
    if (nelites) *nelites = k;
    if (rank < 0 || rank >= k) { snprintf(err, n, "rank %d fora de 0..%d", rank, k - 1); search_state_free(&st); return -1; }
    *rule = st.res.cand[rk[rank]].rule;
    *sc = st.sc;
    search_state_free(&st);
    return 0;
}

/* Índices das elites do arquivo em ordem decrescente de score (empate: menor id). Retorna quantas. */
int archive_ranked(const SearchResult *res, int *out) {
    int k = 0;
    for (int j = 0; j < ARCH_BINS * ARCH_BINS; j++) if (res->arch.elite[j] >= 0) out[k++] = res->arch.elite[j];
    for (int a = 1; a < k; a++) { /* inserção: k <= 100 */
        int v = out[a], b = a - 1;
        while (b >= 0 && (res->cand[out[b]].score < res->cand[v].score ||
                          (res->cand[out[b]].score == res->cand[v].score && out[b] > v))) { out[b + 1] = out[b]; b--; }
        out[b + 1] = v;
    }
    return k;
}

void search_free(SearchResult *res) { free(res->cand); res->cand = NULL; res->n = 0; }

static const char *snames[] = { "random", "evo", "map" };

int strategy_parse(const char *s, Strategy *out) {
    for (int i = 0; i < 3; i++)
        if (s && strcmp(s, snames[i]) == 0) { *out = (Strategy)i; return 0; }
    return -1;
}

const char *strategy_name(Strategy s) { return (unsigned)s < 3 ? snames[s] : "?"; }
