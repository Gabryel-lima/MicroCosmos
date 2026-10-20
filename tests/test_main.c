/**
 * @file test_main.c
 * @brief testes do motor. Valores esperados vêm de padrões conhecidos do Jogo da Vida
 * e de contas feitas à mão, não da própria implementação.
 */
#include "../src/mc.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, eps) CHECK(fabs((a) - (b)) <= (eps))

static Rule R(const char *s) { Rule r; if (rule_parse(s, &r, NULL, 0)) { fprintf(stderr, "regra de teste inválida %s\n", s); exit(1); } return r; }
static void put(World *w, const int (*pts)[2], int n, int ox, int oy) {
    for (int i = 0; i < n; i++) *world_at(w, (ox + pts[i][0]) % w->w, (oy + pts[i][1]) % w->h) = 1;
}
static const int GLIDER[5][2] = { { 1, 0 }, { 2, 1 }, { 0, 2 }, { 1, 2 }, { 2, 2 } };
static const int BLOCK[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
static const int BLINKER[3][2] = { { 0, 1 }, { 1, 1 }, { 2, 1 } };
static SimConfig cfg(int w, int steps, Boundary b) { SimConfig c = sim_default_config(); c.w = c.h = w; c.steps = steps; c.bound = b; c.perturb = 0; return c; }

static void test_rule_parse(void) {
    Rule r;
    char err[128];
    CHECK(rule_parse("B3/S23", &r, err, sizeof err) == 0 && r.birth == 0x8 && r.survive == 0xC);
    CHECK(rule_parse("b36/s23", &r, err, sizeof err) == 0 && r.birth == ((1 << 3) | (1 << 6)));
    CHECK(rule_parse("B/S", &r, err, sizeof err) == 0 && r.birth == 0 && r.survive == 0);
    CHECK(rule_parse("B012345678/S012345678", &r, err, sizeof err) == 0 && r.birth == 0x1FF && r.survive == 0x1FF);
    const char *bad[] = { "", "B3", "B9/S23", "B33/S23", "X3/S23", "B3/S23x", "B3S23", "S23/B3", "B3/S2 3", "B-1/S2", " B3/S23" };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        err[0] = 0;
        CHECK(rule_parse(bad[i], &r, err, sizeof err) == -1);
        CHECK(err[0] != 0); /* mensagem compreensível presente */
    }
    CHECK(rule_parse(NULL, &r, err, sizeof err) == -1);
    char buf[RULE_STR_MAX];
    rule_format(R("b63/s32"), buf, sizeof buf);
    CHECK(strcmp(buf, "B36/S23") == 0);
}

/* Propriedade exaustiva no espaço inteiro de 2^18 regras: format -> parse é identidade. */
static void test_rule_roundtrip_exhaustive(void) {
    int bad = 0;
    for (uint32_t x = 0; x < (1u << 18); x++) {
        Rule r = { (uint16_t)(x & RULE_MASK), (uint16_t)(x >> 9) }, q;
        char buf[RULE_STR_MAX];
        rule_format(r, buf, sizeof buf);
        if (rule_parse(buf, &q, NULL, 0) || !rule_equal(r, q)) bad++;
    }
    CHECK(bad == 0);
}

/* Fuzz simples do parser: sem crash (ASan/UBSan) e toda string aceita sobrevive a parse->format->parse. */
static void test_rule_fuzz(void) {
    const char alpha[] = "BSbs/0123456789x -";
    uint64_t s = 12345;
    int incoherent = 0, accepted = 0;
    for (int it = 0; it < 200000; it++) {
        char buf[32];
        int len = (int)rng_below(&s, 16);
        for (int i = 0; i < len; i++) buf[i] = alpha[rng_below(&s, sizeof alpha - 1)];
        buf[len] = 0;
        Rule r, q;
        char err[64], f[RULE_STR_MAX];
        if (rule_parse(buf, &r, err, sizeof err) == 0) {
            accepted++;
            rule_format(r, f, sizeof f);
            if (rule_parse(f, &q, NULL, 0) || !rule_equal(r, q) || (r.birth | r.survive) & ~RULE_MASK) incoherent++;
        }
    }
    CHECK(incoherent == 0);
    CHECK(accepted > 0); /* o fuzz realmente exercitou o caminho de aceitação */
}

static void test_mutation(void) {
    for (uint64_t seed = 0; seed < 2000; seed++) {
        uint64_t s1 = seed, s2 = seed;
        Rule base = rule_random(&s1);
        (void)rule_random(&s2);
        Rule a = rule_mutate(base, &s1), b = rule_mutate(base, &s2);
        CHECK(rule_equal(a, b)); /* determinística */
        int d = __builtin_popcount((unsigned)((a.birth ^ base.birth) | ((a.survive ^ base.survive) << 9)));
        if (d != 1) { CHECK(d == 1); break; } /* exatamente um bit */
        CHECK(((a.birth | a.survive) & ~RULE_MASK) == 0);
    }
    /* os 18 bits são alcançáveis pela mutação (B e S) */
    uint32_t seen = 0;
    uint64_t s = 99;
    Rule z = { 0, 0 };
    for (int i = 0; i < 5000; i++) { Rule a = rule_mutate(z, &s); seen |= (uint32_t)a.birth | ((uint32_t)a.survive << 9); }
    CHECK(seen == (1u << 18) - 1);
}

static void test_known_patterns(void) {
    Rule life = R("B3/S23");
    World a, b;
    world_init(&a, 16, 16, BOUND_TORUS);
    world_init(&b, 16, 16, BOUND_TORUS);
    /* bloco: estático */
    put(&a, BLOCK, 4, 5, 5);
    sim_step(life, &a, &b);
    CHECK(world_diff(&a, &b) == 0);
    /* blinker: período 2, horizontal -> vertical */
    world_clear(&a);
    put(&a, BLINKER, 3, 5, 5); /* (5,6),(6,6),(7,6) */
    sim_step(life, &a, &b);
    CHECK(world_count(&b) == 3 && world_get(&b, 6, 5) && world_get(&b, 6, 6) && world_get(&b, 6, 7));
    World c;
    world_init(&c, 16, 16, BOUND_TORUS);
    sim_step(life, &b, &c);
    CHECK(world_diff(&a, &c) == 0);
    /* glider: após 4 passos, mesma forma transladada (+1,+1), inclusive atravessando a borda do toro */
    world_clear(&a);
    put(&a, GLIDER, 5, 14, 14);
    World exp;
    world_init(&exp, 16, 16, BOUND_TORUS);
    put(&exp, GLIDER, 5, 15, 15);
    for (int i = 0; i < 4; i++) { sim_step(life, &a, &b); world_copy(&a, &b); }
    CHECK(world_diff(&a, &exp) == 0);
    world_free(&a); world_free(&b); world_free(&c); world_free(&exp);
}

static void test_boundaries(void) {
    Rule life = R("B3/S23");
    World a, b;
    /* Bloco no canto (0,0): com borda fixa permanece; com refletora, (0,0) enxerga a si mesmo 3x e
     * os vizinhos espelhados -> 8 vizinhos -> morre (conta feita à mão). */
    world_init(&a, 8, 8, BOUND_FIXED);
    world_init(&b, 8, 8, BOUND_FIXED);
    put(&a, BLOCK, 4, 0, 0);
    sim_step(life, &a, &b);
    CHECK(world_diff(&a, &b) == 0);
    a.bound = BOUND_REFLECT;
    sim_step(life, &a, &b);
    CHECK(world_get(&b, 0, 0) == 0);
    /* Bloco atravessando a borda: no toro é estável; com borda fixa vira dois pares que morrem. */
    world_clear(&a);
    a.bound = BOUND_TORUS;
    put(&a, BLOCK, 4, 7, 3); /* (7,3),(0,3),(7,4),(0,4) */
    sim_step(life, &a, &b);
    CHECK(world_diff(&a, &b) == 0);
    CHECK(metric_components(&a) == 1);
    a.bound = BOUND_FIXED;
    CHECK(metric_components(&a) == 2);
    sim_step(life, &a, &b);
    CHECK(world_count(&b) == 0);
    world_free(&a); world_free(&b);
    Boundary bd;
    CHECK(boundary_parse("reflect", &bd) == 0 && bd == BOUND_REFLECT);
    CHECK(boundary_parse("toro", &bd) == -1);
}

static void test_metrics_cases(void) {
    World w;
    Metrics m;
    SimConfig c = cfg(64, 50, BOUND_TORUS);
    world_init(&w, 64, 64, BOUND_TORUS);
    /* mundo vazio: extinto, persistência 0, estático */
    rollout(R("B3/S23"), &c, &w, &m, NULL);
    CHECK(m.extinct == 1 && m.persistence == 0 && m.cycle_period == 1 && m.density_mean == 0 && m.steps_simulated == 1);
    /* bloco: estático, persistência 0, densidade 4/4096, 1 componente */
    put(&w, BLOCK, 4, 10, 10);
    rollout(R("B3/S23"), &c, &w, &m, NULL);
    CHECK(m.persistence == 0 && m.cycle_period == 1 && m.components_final == 1 && m.alive_final == 4);
    CHECK(m.steps_simulated == 1); /* para no ponto fixo, não só na extinção */
    NEAR(m.density_mean, 4.0 / 4096, 1e-12);
    NEAR(m.activity_mean, 0.0, 1e-12);
    /* blinker: persistência T, atividade 4/4096, período 2 */
    world_clear(&w);
    put(&w, BLINKER, 3, 30, 30);
    rollout(R("B3/S23"), &c, &w, &m, NULL);
    CHECK(m.persistence == 50 && m.cycle_period == 2 && m.cycle_start == 0 && m.steps_simulated == 50);
    NEAR(m.activity_mean, 4.0 / 4096, 1e-12);
    NEAR(m.density_mean, 3.0 / 4096, 1e-12);
    NEAR(m.com_displacement, 0.0, 1e-9);
    /* glider: 40 passos -> desloca (10,10); 160 passos -> (40,40) que no toro 64 é (-24,-24) */
    world_clear(&w);
    put(&w, GLIDER, 5, 5, 5);
    c.steps = 40;
    rollout(R("B3/S23"), &c, &w, &m, NULL);
    NEAR(m.com_displacement, sqrt(200.0), 1e-6);
    CHECK(m.persistence == 40 && m.components_final == 1);
    c.steps = 160;
    rollout(R("B3/S23"), &c, &w, &m, NULL);
    NEAR(m.com_displacement, sqrt(2.0 * 24 * 24), 1e-6);
    /* glider no toro 64 volta ao estado inicial após 4*64 = 256 passos */
    c.steps = 300;
    rollout(R("B3/S23"), &c, &w, &m, NULL);
    CHECK(m.cycle_period == 256 && m.cycle_start == 0);
    /* sem toro, deslocamento é euclidiano simples: glider 40 passos com borda fixa longe das bordas */
    World f;
    world_init(&f, 64, 64, BOUND_FIXED);
    put(&f, GLIDER, 5, 5, 5);
    SimConfig cf = cfg(64, 40, BOUND_FIXED);
    rollout(R("B3/S23"), &cf, &f, &m, NULL);
    NEAR(m.com_displacement, sqrt(200.0), 1e-6);
    world_free(&f);
    /* regra B/S: tudo morre no passo 1 */
    world_soup(&w, 7, 0.5, 0.5);
    c.steps = 100;
    rollout(R("B/S"), &c, &w, &m, NULL);
    CHECK(m.extinct && m.persistence == 0 && m.steps_simulated == 2);
    /* componentes: dois blocos separados */
    world_clear(&w);
    put(&w, BLOCK, 4, 2, 2);
    put(&w, BLOCK, 4, 20, 20);
    CHECK(metric_components(&w) == 2);
    /* forma em U: a conexão exige visitar vizinho acima (dy=-1) a partir de (1,2) */
    static const int U[5][2] = { { 0, 0 }, { 2, 0 }, { 0, 1 }, { 2, 1 }, { 1, 2 } };
    world_clear(&w);
    put(&w, U, 5, 10, 10);
    CHECK(metric_components(&w) == 1);
    /* centro de massa circular no intervalo [0,n): coluna em x=63 e x=0 -> ~63.5 */
    world_clear(&w);
    *world_at(&w, 63, 5) = 1; *world_at(&w, 0, 5) = 1;
    double cx, cy;
    CHECK(metric_com(&w, &cx, &cy) == 1);
    NEAR(cx, 63.5, 1e-9);
    NEAR(cy, 5.0, 1e-9);
    world_free(&w);
}

static void test_sensitivity(void) {
    World w;
    Metrics m;
    SimConfig c = cfg(32, 20, BOUND_TORUS);
    c.perturb = 1;
    world_init(&w, 32, 32, BOUND_TORUS);
    world_soup(&w, 3, 0.5, 0.5);
    /* B/S012345678: nada nasce, tudo sobrevive -> mundo congelado; a perturbação 3x3 persiste: 9/N */
    rollout(R("B/S012345678"), &c, &w, &m, NULL);
    NEAR(m.sensitivity, 9.0 / 1024, 1e-12);
    /* B/S: tudo morre -> perturbação desaparece */
    rollout(R("B/S"), &c, &w, &m, NULL);
    NEAR(m.sensitivity, 0.0, 1e-12);
    world_free(&w);
}

static void test_determinism(void) {
    SimConfig c = sim_default_config();
    c.steps = 120;
    World w1, w2, f1, f2;
    world_init(&w1, 64, 64, BOUND_TORUS); world_init(&w2, 64, 64, BOUND_TORUS);
    world_init(&f1, 64, 64, BOUND_TORUS); world_init(&f2, 64, 64, BOUND_TORUS);
    world_soup(&w1, 42, 0.5, 0.5);
    world_soup(&w2, 42, 0.5, 0.5);
    CHECK(world_diff(&w1, &w2) == 0);
    Metrics m1, m2;
    rollout(R("B3/S23"), &c, &w1, &m1, &f1);
    rollout(R("B3/S23"), &c, &w2, &m2, &f2);
    CHECK(memcmp(&m1, &m2, sizeof m1) == 0 && world_diff(&f1, &f2) == 0);
    world_soup(&w2, 43, 0.5, 0.5);
    CHECK(world_diff(&w1, &w2) > 0);
    /* sopa só ocupa a fração central */
    int outside = 0;
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) if ((x < 16 || x >= 48 || y < 16 || y >= 48) && world_get(&w1, x, y)) outside++;
    CHECK(outside == 0 && world_count(&w1) > 0);
    world_free(&w1); world_free(&w2); world_free(&f1); world_free(&f2);
}

static void test_archive(void) {
    Metrics m = { 0 };
    m.steps = 100;
    m.persistence = 100; m.activity_mean = 1.0;
    CHECK(archive_cell(&m) == ARCH_BINS * ARCH_BINS - 1); /* limites superiores são clampados */
    m.persistence = 0; m.activity_mean = 0.0;
    CHECK(archive_cell(&m) == 0);
    m.persistence = 35; m.activity_mean = 0.1; /* x=3, y=floor(0.1/0.25*10)=4 */
    CHECK(archive_cell(&m) == 4 * ARCH_BINS + 3);
    m.components_final = 8; /* score = 0.35 * 0.5 */
    NEAR(candidate_score(&m), 0.175, 1e-12);
    m.components_final = 100;
    NEAR(candidate_score(&m), 0.35, 1e-12);
    Rule life = R("B3/S23"), b0 = R("B03/S23");
    m.activity_mean = 0.1;
    NEAR(rule_score(life, &m), candidate_score(&m), 1e-15);
    CHECK(rule_score(b0, &m) == 0.0);
    m.activity_mean = ARCH_ACT_MAX; /* limite: já é ruído */
    CHECK(rule_score(life, &m) == 0.0 && candidate_score(&m) > 0);
    m.activity_mean = 0.1;
    m.density_mean = SCORE_DENS_MAX;
    CHECK(rule_score(life, &m) == 0.0);
    m.density_mean = 0.05;
    CHECK(rule_score(life, &m) > 0.0);
    Candidate c[3] = { 0 };
    c[0].cell = c[1].cell = c[2].cell = 5;
    c[0].score = 0.5; c[1].score = 0.4; c[2].score = 0.6;
    Archive a;
    archive_clear(&a);
    CHECK(archive_offer(&a, c, 0) == 1 && a.elite[5] == 0);
    CHECK(archive_offer(&a, c, 1) == 0 && a.elite[5] == 0);
    CHECK(archive_offer(&a, c, 2) == 1 && a.elite[5] == 2);
    c[1].score = 0.6;
    CHECK(archive_offer(&a, c, 1) == 0); /* empate não substitui */
}

static void test_search(void) {
    SearchConfig sc = { STRAT_MAP, 40, 8, 7, 1, cfg(24, 60, BOUND_TORUS) };
    char err[128];
    for (int s = 0; s < 3; s++) {
        sc.strat = (Strategy)s;
        SearchResult r1, r2;
        CHECK(search_run(&sc, &r1, err, sizeof err) == 0);
        CHECK(search_run(&sc, &r2, err, sizeof err) == 0);
        CHECK(r1.n == 40 && r2.n == 40);
        int same = 1, parents_ok = 1, mut_ok = 1, b0 = 0;
        for (int i = 0; i < r1.n; i++) {
            if (!rule_equal(r1.cand[i].rule, r2.cand[i].rule) || r1.cand[i].score != r2.cand[i].score) same = 0;
            int p = r1.cand[i].parent;
            if (s == STRAT_RANDOM && p != -1) parents_ok = 0;
            if (p >= i) parents_ok = 0;
            if (p >= 0) {
                Rule a = r1.cand[i].rule, b = r1.cand[p].rule;
                if (__builtin_popcount((unsigned)((a.birth ^ b.birth) | ((a.survive ^ b.survive) << 9))) != 1) mut_ok = 0;
                if (r1.cand[i].gen != r1.cand[p].gen + 1) mut_ok = 0;
            }
            if (r1.cand[i].rule.birth & 1u) { b0++; if (r1.cand[i].score != 0.0) mut_ok = 0; }
            if (r1.cand[i].m.activity_mean >= ARCH_ACT_MAX && r1.cand[i].score != 0.0) mut_ok = 0;
            {
            }
        }
        CHECK(same && parents_ok && mut_ok);
        CHECK(b0 > 0); /* o caso B0 foi realmente exercitado */
        int cov = 0;
        for (int j = 0; j < ARCH_BINS * ARCH_BINS; j++) cov += r1.arch.elite[j] >= 0;
        CHECK(cov == r1.coverage && cov >= 1);
        if (s != STRAT_RANDOM) {
            int muts = 0;
            for (int i = 0; i < r1.n; i++) muts += r1.cand[i].parent >= 0;
            CHECK(muts == 40 - 8);
        }
        search_free(&r1);
        search_free(&r2);
    }
    sc.budget = 0;
    SearchResult r;
    CHECK(search_run(&sc, &r, err, sizeof err) == -1);
    sc.budget = 10; sc.sim.density = 1.5;
    CHECK(search_run(&sc, &r, err, sizeof err) == -1);
    double v[] = { 3, 1, 2, 10 };
    NEAR(median_of(v, 4), 2.5, 1e-12);
}

/** @brief 1 se dois resultados têm candidatos e arquivo idênticos (teste diferencial). */
static int same_cands(const SearchResult *a, const SearchResult *b) {
    if (a->n != b->n || memcmp(&a->arch, &b->arch, sizeof a->arch)) return 0;
    for (int i = 0; i < a->n; i++) {
        const Candidate *x = &a->cand[i], *y = &b->cand[i];
        if (!rule_equal(x->rule, y->rule) || x->score != y->score || x->parent != y->parent || x->gen != y->gen ||
            x->cell != y->cell || memcmp(&x->m, &y->m, sizeof x->m))
            return 0;
    }
    return 1;
}

/* Teste diferencial: busca interrompida + checkpoint + retomada == busca sem interrupção. */
static void test_checkpoint(void) {
    const char *path = "bin/test_ck.bin";
    char err[256];
    for (int s = 0; s < 3; s++) {
        SearchConfig sc = { (Strategy)s, 40, 8, 11, 2, cfg(20, 50, BOUND_TORUS) };
        SearchResult full;
        CHECK(search_run(&sc, &full, err, sizeof err) == 0);
        SearchState st, ld;
        /* corte antes da população inicial completar (5) e depois (17) */
        for (int cut = 5; cut <= 17; cut += 12) {
            CHECK(search_begin(&st, &sc, err, sizeof err) == 0);
            for (int i = 0; i < cut; i++) CHECK(search_step(&st, err, sizeof err) == 1);
            CHECK(search_save(&st, path) == 0);
            search_state_free(&st);
            CHECK(search_load(&ld, path, err, sizeof err) == 0);
            CHECK(ld.res.n == cut);
            while (search_step(&ld, err, sizeof err) == 1) {}
            search_stats(&ld.res);
            CHECK(same_cands(&full, &ld.res) && ld.res.qd_score == full.qd_score);
            search_state_free(&ld);
        }
        /* orçamento estendido ao retomar: 20 + extensão até 40 == 40 direto */
        SearchConfig s20 = sc;
        s20.budget = 20;
        CHECK(search_begin(&st, &s20, err, sizeof err) == 0);
        while (search_step(&st, err, sizeof err) == 1) {}
        CHECK(search_step(&st, err, sizeof err) == 0); /* orçamento esgotado */
        CHECK(search_save(&st, path) == 0);
        search_state_free(&st);
        CHECK(search_load(&ld, path, err, sizeof err) == 0 && search_extend(&ld, 40) == 0);
        while (search_step(&ld, err, sizeof err) == 1) {}
        CHECK(same_cands(&full, &ld.res));
        int rk[ARCH_BINS * ARCH_BINS], k = archive_ranked(&ld.res, rk), sorted = k == full.coverage;
        for (int i = 1; i < k; i++) sorted &= ld.res.cand[rk[i - 1]].score >= ld.res.cand[rk[i]].score;
        CHECK(sorted && k >= 1);
        search_state_free(&ld);
        search_free(&full);
    }
    /* Fuzz do leitor: bytes invertidos e truncamentos não podem travar nem passar inconsistência (ASan). */
    SearchConfig sc = { STRAT_MAP, 30, 8, 3, 1, cfg(16, 30, BOUND_TORUS) };
    SearchState st, ld;
    search_begin(&st, &sc, err, sizeof err);
    while (search_step(&st, err, sizeof err) == 1) {}
    search_save(&st, path);
    search_state_free(&st);
    FILE *f = fopen(path, "rb");
    static unsigned char good[200000], buf[200000];
    size_t len = fread(good, 1, sizeof good, f);
    fclose(f);
    CHECK(len > 100 && len < sizeof good);
    uint64_t r = 77;
    int accepted_corrupt = 0, rejected = 0;
    for (int it = 0; it < 400; it++) {
        memcpy(buf, good, len);
        size_t L = len;
        if (it % 4 == 0) L = rng_below(&r, (uint32_t)len); /* truncamento */
        else for (int k = 0; k < 1 + it % 5; k++) buf[rng_below(&r, (uint32_t)len)] ^= (unsigned char)(1u << rng_below(&r, 8));
        f = fopen(path, "wb");
        fwrite(buf, 1, L, f);
        fclose(f);
        if (search_load(&ld, path, err, sizeof err) == 0) {
            /* aceito: o estado precisa continuar utilizável até o fim do orçamento sem violar memória */
            search_extend(&ld, ld.sc.budget < 60 ? 60 : ld.sc.budget);
            int steps = 0;
            while (steps < 5 && search_step(&ld, err, sizeof err) == 1) steps++;
            accepted_corrupt++;
            search_state_free(&ld);
        } else { rejected++; CHECK(err[0] != 0); }
    }
    CHECK(rejected > 0);
    printf("fuzz checkpoint: 400 arquivos corrompidos, %d rejeitados, %d aceitos (bits em campos sem invariante)\n", rejected,
           accepted_corrupt);
    /* bytes extras no fim = arquivo não confiável (ex.: concatenado) -> rejeitado */
    f = fopen(path, "wb");
    fwrite(good, 1, len, f);
    fputc(0, f);
    fclose(f);
    CHECK(search_load(&ld, path, err, sizeof err) == -1);
    f = fopen(path, "wb");
    fwrite(good, 1, len, f);
    fclose(f);
    CHECK(search_load(&ld, path, err, sizeof err) == 0); /* controle: o original é aceito */
    search_state_free(&ld);
    CHECK(search_load(&ld, "bin/nao_existe.bin", err, sizeof err) == -1);
    remove(path);
}

static void test_render(void) {
    World w;
    world_init(&w, 4, 8, BOUND_TORUS);
    CHECK(braille_bits(&w, 0, 0) == 0);
    *world_at(&w, 0, 0) = 1;
    CHECK(braille_bits(&w, 0, 0) == 0x01); /* ponto 1 */
    *world_at(&w, 1, 3) = 1;
    CHECK(braille_bits(&w, 0, 0) == 0x81); /* + ponto 8 */
    *world_at(&w, 0, 3) = 1; *world_at(&w, 1, 0) = 1;
    CHECK(braille_bits(&w, 0, 0) == 0xC9); /* pontos 1,4,7,8 */
    CHECK(braille_bits(&w, 2, 0) == 0 && braille_bits(&w, 0, 4) == 0);
    for (int y = 4; y < 8; y++) for (int x = 2; x < 4; x++) *world_at(&w, x, y) = 1;
    CHECK(braille_bits(&w, 2, 4) == 0xFF);
    CHECK(braille_bits(&w, 3, 6) == 0x03); /* bloco parcialmente fora: (3,6),(3,7) na coluna 0 = pontos 1,2 */
    /* saída UTF-8: célula (0,0) sozinha -> U+2801 = E2 A0 81 */
    World s;
    world_init(&s, 3, 4, BOUND_TORUS);
    *world_at(&s, 0, 0) = 1;
    char buf[64] = { 0 };
    FILE *f = fmemopen(buf, sizeof buf - 1, "w");
    braille_render(f, &s, NULL);
    fclose(f);
    CHECK((unsigned char)buf[0] == 0xE2 && (unsigned char)buf[1] == 0xA0 && (unsigned char)buf[2] == 0x81);
    CHECK(buf[3] == ' '); /* segunda coluna braille (x=2) vazia */
    CHECK(buf[4] == '\n'); /* saída monocromática sem códigos ANSI */
    /* bloco cheio -> U+28FF = E2 A3 BF */
    for (int y = 0; y < 4; y++) for (int x = 0; x < 2; x++) *world_at(&s, x, y) = 1;
    memset(buf, 0, sizeof buf);
    f = fmemopen(buf, sizeof buf - 1, "w");
    braille_render(f, &s, NULL);
    fclose(f);
    CHECK((unsigned char)buf[0] == 0xE2 && (unsigned char)buf[1] == 0xA3 && (unsigned char)buf[2] == 0xBF);
    world_free(&s);
    /* idade e rastro: nasce=1, cresce, morre -> 0 com rastro 255, depois decai 7/8 */
    uint16_t age[32] = { 0 };
    uint8_t trail[32] = { 0 };
    world_clear(&w);
    *world_at(&w, 1, 1) = 1;
    age_update(&w, age, trail);
    CHECK(age[5] == 1 && age[0] == 0 && trail[0] == 0);
    age_update(&w, age, trail);
    CHECK(age[5] == 2);
    world_clear(&w);
    age_update(&w, age, trail);
    CHECK(age[5] == 0 && trail[5] == 255);
    age_update(&w, age, trail);
    CHECK(trail[5] == 223); /* 255*7/8 */
    age[0] = UINT16_MAX; *world_at(&w, 0, 0) = 1;
    age_update(&w, age, trail);
    CHECK(age[0] == UINT16_MAX); /* sem overflow */
    /* paleta: extremos batem com as paradas declaradas */
    uint8_t c[3];
    palette_color(1, 0, c);
    CHECK(c[0] == 120 && c[1] == 255 && c[2] == 240);
    palette_color(256, 0, c);
    CHECK(c[0] == 200 && c[1] == 60 && c[2] == 200);
    palette_color(0, 0, c);
    CHECK(c[0] == 12 && c[1] == 12 && c[2] == 28);
    palette_color(0, 255, c);
    CHECK(c[0] == 90 && c[1] == 40 && c[2] == 120);
    world_free(&w);
}

/** @brief Câmera, fonte e canvas do HUD (oráculos calculados à mão). */
static void test_hud(void) {
    World w;
    world_init(&w, 10, 6, BOUND_TORUS);
    View v = { 0, 0, 8 };
    int cx = -1, cy = -1;
    CHECK(view_cell(&v, &w, 17, 9, &cx, &cy) == 1 && cx == 2 && cy == 1); /* 17/8 = 2.1, 9/8 = 1.1 */
    v.x = -1.5; /* toro: célula -2 -> 8 */
    CHECK(view_cell(&v, &w, 0, 0, &cx, &cy) == 1 && cx == 8 && cy == 0);
    w.bound = BOUND_FIXED;
    CHECK(view_cell(&v, &w, 0, 0, &cx, &cy) == 0);
    CHECK(view_cell(&v, &w, 12, 0, &cx, &cy) == 1 && cx == 0); /* -1.5 + 1.5 = 0 */
    /* zoom ancorado: o ponto do mundo sob o cursor não se move (propriedade, entradas aleatórias) */
    uint64_t s = 5;
    int moved = 0;
    for (int it = 0; it < 1000; it++) {
        View u = { rng_unit(&s) * 40 - 20, rng_unit(&s) * 40 - 20, 0.5 + rng_unit(&s) * 30 };
        int px = (int)rng_below(&s, 800), py = (int)rng_below(&s, 600);
        double f = 0.5 + rng_unit(&s) * 1.5, wx = u.x + px / u.zoom, wy = u.y + py / u.zoom;
        view_zoom_at(&u, px, py, f);
        if (fabs(u.x + px / u.zoom - wx) > 1e-9 || fabs(u.y + py / u.zoom - wy) > 1e-9) moved++;
        if (u.zoom < VIEW_ZOOM_MIN || u.zoom > VIEW_ZOOM_MAX) moved++;
    }
    CHECK(moved == 0);
    View z = { 0, 0, 60 };
    view_zoom_at(&z, 0, 0, 10);
    CHECK(z.zoom == VIEW_ZOOM_MAX);
    view_fit(&z, &w, 200, 60); /* 10x6 em 200x60: zoom = min(20, 10) = 10; x centrado = (10 - 20)/2 = -5 */
    NEAR(z.zoom, 10.0, 1e-12);
    NEAR(z.x, -5.0, 1e-12);
    NEAR(z.y, 0.0, 1e-12);
    /* fonte: todos os caracteres usados existem e têm 15 pontos (ASan pega string curta) */
    const char *set = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcz :/.,-+=()[]<>?_|%*^!";
    int missing = 0;
    uint16_t b;
    for (const char *p = set; *p; p++) missing += !font_glyph(*p, &b);
    CHECK(missing == 0);
    CHECK(font_glyph('1', &b) == 1 && b == ((1 << 1) | (1 << 3) | (1 << 4) | (1 << 7) | (1 << 10) | (1 << 12) | (1 << 13) | (1 << 14)));
    /* padrões desenhados à mão (A, H, M saíram errados na 1a versão; visto no snapshot) */
    CHECK(font_glyph('A', &b) == 1 && b == 23530); /* .#. #.# ### #.# #.# */
    CHECK(font_glyph('H', &b) == 1 && b == 23533); /* #.# #.# ### #.# #.# */
    CHECK(font_glyph('M', &b) == 1 && b == 23549); /* #.# ### ### #.# #.# */
    uint16_t q;
    font_glyph('?', &q);
    CHECK(font_glyph('~', &b) == 0 && b == q); /* desconhecido -> '?' */
    CHECK(text_width("AB", 2) == 14 && text_width("", 3) == 0);
    /* escala automática da UI (valores documentados no header) */
    CHECK(ui_auto_scale(640, 400) == 1 && ui_auto_scale(1000, 700) == 2 && ui_auto_scale(1920, 1080) == 3 &&
          ui_auto_scale(2560, 1440) == 4 && ui_auto_scale(900, 2000) == 1 /* retrato: largura limita */ && ui_auto_scale(320, 240) == 1 && ui_auto_scale(20000, 20000) == UI_SCALE_MAX);
    /* canvas: "1" em (0,0), escala 1 */
    uint32_t px[8 * 8] = { 0 };
    Canvas cv = { px, 8, 8 };
    canvas_text(&cv, 0, 0, "1", 0xFFFFFFFFu, 1);
    CHECK(px[0] == 0 && px[1] == 0xFFFFFFFFu && px[8] == 0xFFFFFFFFu && px[2] == 0 && px[4 * 8 + 2] == 0xFFFFFFFFu);
    canvas_text(&cv, -5, -3, "XYZ", 0xFFFFFFFFu, 3); /* recorte: não pode escrever fora (ASan) */
    canvas_fill_rect(&cv, 6, 6, 100, 100, 0xFF00FF00u);
    CHECK(px[7 * 8 + 7] == 0xFF00FF00u);
    px[0] = 0xFF000000u;
    canvas_fill_rect(&cv, 0, 0, 1, 1, 0x80FFFFFFu); /* branco com alfa 128 sobre preto -> 128 */
    CHECK(px[0] == 0xFF808080u);
    /* render_world: célula viva no zoom 1 aparece; fora do mundo (borda fixa) é quase preto */
    world_clear(&w);
    *world_at(&w, 1, 0) = 1;
    View one = { 0, 0, 1 };
    uint32_t big[12 * 6];
    Canvas cb = { big, 12, 6 };
    render_world(&cb, &w, NULL, NULL, &one, 0, 0);
    CHECK(big[1] == 0xFFE6E6E6u && big[0] == 0xFF141414u && big[11] == 0xFF060608u);
    render_minimap(&cb, &w, &one, 1, 1, 8); /* só não pode violar memória */
    /* PPM do canvas: cabeçalho + RGB do pixel (0,0) */
    Canvas c2 = { px, 2, 1 };
    px[0] = 0xFF102030u;
    CHECK(canvas_save_ppm(&c2, "bin/test_canvas.ppm") == 0);
    FILE *pf = fopen("bin/test_canvas.ppm", "rb");
    unsigned char ppm[32] = { 0 };
    size_t pl = pf ? fread(ppm, 1, sizeof ppm, pf) : 0;
    if (pf) fclose(pf);
    CHECK(pl == 11 + 6 && memcmp(ppm, "P6\n2 1\n255\n", 11) == 0 && ppm[11] == 0x10 && ppm[12] == 0x20 && ppm[13] == 0x30);
    remove("bin/test_canvas.ppm");
    CHECK(canvas_save_ppm(&c2, "/diretorio/inexistente/x.ppm") == -1);
    render_palette_legend(&cb, 0, 0, 40, 1, PALETTE_FIRE);
    world_free(&w);
}

/** @brief Motor volumétrico: diferencial contra o 2D, oráculos 3D à mão, parser de faixas. */
static void test_volume(void) {
    char err[160], buf[RULE3_STR_MAX];
    Rule3 r3;
    Neighborhood nb;
    /* vizinhanças: tamanhos conhecidos */
    CHECK(neighborhood_make(NB_MOORE2D, &nb) == 0 && nb.n == 8);
    CHECK(neighborhood_make(NB_MOORE3D, &nb) == 0 && nb.n == 26);
    CHECK(neighborhood_make(NB_VN3D, &nb) == 0 && nb.n == 6);
    /* parser */
    CHECK(rule3_parse("B5-7/S4-5,9", 26, &r3, err, sizeof err) == 0 && r3.birth == ((1u << 5) | (1u << 6) | (1u << 7)) &&
          r3.survive == ((1u << 4) | (1u << 5) | (1u << 9)));
    CHECK(rule3_parse("B3/S23", 8, &r3, err, sizeof err) == 0 && r3.birth == 8 && r3.survive == 12); /* forma compacta */
    CHECK(rule3_parse("B13/S26", 26, &r3, err, sizeof err) == 0 && r3.birth == (1u << 13) && r3.survive == (1u << 26));
    const char *bad[] = { "B27/S1", "B7-5/S1", "B5-/S1", "B5,,6/S1", "B4,4/S1", "B5", "B5/S1x", "B9/S1", "", "B1-3/S2-3,2", "B5-27/S1" };
    int maxc[] = { 26, 26, 26, 26, 26, 26, 26, 8, 26, 26, 26 };
    for (int i = 0; i < 11; i++) { err[0] = 0; CHECK(rule3_parse(bad[i], maxc[i], &r3, err, sizeof err) == -1 && err[0]); }
    rule3_format((Rule3){ (1u << 5) | (1u << 6) | (1u << 7) | (1u << 26), 1u }, buf, sizeof buf);
    CHECK(strcmp(buf, "B5-7,26/S0") == 0);
    /* round-trip format -> parse em máscaras aleatórias de 27 bits */
    uint64_t s = 31;
    int rt = 0;
    for (int i = 0; i < 20000; i++) {
        Rule3 a = { (uint32_t)(rng_next(&s) & 0x7FFFFFFu), (uint32_t)(rng_next(&s) & 0x7FFFFFFu) }, q;
        rule3_format(a, buf, sizeof buf);
        if (rule3_parse(buf, 26, &q, NULL, 0) || q.birth != a.birth || q.survive != a.survive) rt++;
    }
    CHECK(rt == 0);
    /* Diferencial: d=1 + Moore 2D == motor 2D, para regras/sopas/bordas aleatórias, 30 passos */
    neighborhood_make(NB_MOORE2D, &nb);
    int divergent = 0;
    for (int it = 0; it < 60; it++) {
        Boundary bd = (Boundary)(it % 3);
        Rule r2 = rule_random(&s);
        Rule3 r = { r2.birth, r2.survive };
        World w, w2;
        Volume v, v2;
        world_init(&w, 13, 9, bd); world_init(&w2, 13, 9, bd);
        volume_init(&v, 13, 9, 1, bd); volume_init(&v2, 13, 9, 1, bd);
        world_soup(&w, (uint64_t)it, 0.4, 0.8);
        volume_soup(&v, (uint64_t)it, 0.4, 0.8);
        if (memcmp(w.c, v.c, 13 * 9)) divergent++;
        for (int t = 0; t < 30; t++) {
            sim_step(r2, &w, &w2);
            volume_step(r, &nb, &v, &v2);
            if (memcmp(w2.c, v2.c, 13 * 9)) { divergent++; break; }
            memcpy(w.c, w2.c, 13 * 9);
            memcpy(v.c, v2.c, 13 * 9);
        }
        world_free(&w); world_free(&w2); volume_free(&v); volume_free(&v2);
    }
    CHECK(divergent == 0);
    /* Oráculos 3D à mão */
    Volume a, b;
    volume_init(&a, 5, 5, 5, BOUND_TORUS);
    volume_init(&b, 5, 5, 5, BOUND_TORUS);
    *volume_at(&a, 2, 2, 2) = 1;
    neighborhood_make(NB_MOORE3D, &nb);
    rule3_parse("B1/S", 26, &r3, NULL, 0); /* cada vizinho do único vivo tem 1 vizinho -> nasce; o centro morre */
    volume_step(r3, &nb, &a, &b);
    CHECK(volume_count(&b) == 26 && volume_get(&b, 2, 2, 2) == 0 && volume_get(&b, 1, 1, 1) == 1);
    neighborhood_make(NB_VN3D, &nb);
    volume_step(r3, &nb, &a, &b);
    CHECK(volume_count(&b) == 6 && volume_get(&b, 2, 2, 3) == 1 && volume_get(&b, 1, 1, 2) == 0);
    /* cubo 2x2x2 com borda fixa: cada célula tem 7 vizinhos vivos; B/S7 = estático */
    memset(a.c, 0, 125);
    a.bound = BOUND_FIXED;
    for (int z = 0; z < 2; z++) for (int y = 0; y < 2; y++) for (int x = 0; x < 2; x++) *volume_at(&a, x, y, z) = 1;
    neighborhood_make(NB_MOORE3D, &nb);
    rule3_parse("B/S7", 26, &r3, NULL, 0);
    Metrics m;
    CHECK(rollout3(r3, &nb, 50, &a, &m, NULL) == 0 && m.persistence == 0 && m.cycle_period == 1 && m.alive_final == 8 &&
          m.steps_simulated == 1 && m.components_final == 1);
    rule3_parse("B/S6", 26, &r3, NULL, 0); /* 7 vizinhos não sobrevive -> extinção no passo 1 */
    rollout3(r3, &nb, 50, &a, &m, NULL);
    CHECK(m.extinct && m.alive_final == 0 && m.persistence == 0);
    /* projeção: coluna com qualquer viva aparece */
    World pw;
    world_init(&pw, 5, 5, BOUND_FIXED);
    memset(a.c, 0, 125);
    *volume_at(&a, 3, 1, 4) = 1;
    volume_project(&a, &pw);
    CHECK(world_count(&pw) == 1 && world_get(&pw, 3, 1) == 1);
    CHECK(volume_slice(&a, 4, &pw) == 0 && world_count(&pw) == 1 && world_get(&pw, 3, 1) == 1);
    CHECK(volume_slice(&a, 3, &pw) == 0 && world_count(&pw) == 0);
    CHECK(volume_slice(&a, 5, &pw) == -1 && volume_slice(&a, -1, &pw) == -1);
    Volume inv;
    CHECK(volume_init(&inv, 2, 5, 5, BOUND_TORUS) == -1 && inv.c == NULL && volume_init(&inv, 5, 5, 0, BOUND_TORUS) == -1);
    world_free(&pw);
    /* componentes 3D: dois cubos separados = 2; par atravessando a borda z: toro = 1, fixa = 2 */
    memset(a.c, 0, 125);
    a.bound = BOUND_TORUS;
    *volume_at(&a, 0, 0, 0) = 1;
    *volume_at(&a, 3, 3, 2) = 1;
    CHECK(volume_components(&a) == 2);
    memset(a.c, 0, 125);
    *volume_at(&a, 2, 2, 0) = 1;
    *volume_at(&a, 3, 3, 4) = 1; /* diagonal 3D através da borda z: vizinhos 26-conexos só no toro */
    CHECK(volume_components(&a) == 1);
    a.bound = BOUND_FIXED;
    CHECK(volume_components(&a) == 2);
    neighborhood_make(NB_VN3D, &nb); /* nb não afeta componentes (sempre 26-conexos) */
    /* centro de massa: translação no toro desloca o centro exatamente pela imagem mínima (propriedade) */
    Volume big, sh;
    volume_init(&big, 12, 10, 8, BOUND_TORUS);
    volume_init(&sh, 12, 10, 8, BOUND_TORUS);
    int bad_com = 0;
    for (int it = 0; it < 40; it++) {
        memset(big.c, 0, 960);
        memset(sh.c, 0, 960);
        int dx = (int)rng_below(&s, 12), dy = (int)rng_below(&s, 10), dz = (int)rng_below(&s, 8);
        for (int k = 0; k < 5; k++) { /* aglomerado compacto (resultante circular bem definida) */
            int x = 2 + (int)rng_below(&s, 3), y = 2 + (int)rng_below(&s, 3), z = 2 + (int)rng_below(&s, 3);
            *volume_at(&big, x, y, z) = 1;
            *volume_at(&sh, (x + dx) % 12, (y + dy) % 10, (z + dz) % 8) = 1;
        }
        double p[3], q[3];
        volume_com(&big, p);
        volume_com(&sh, q);
        double ex = dx > 6 ? 12 - dx : dx, ey = dy > 5 ? 10 - dy : dy, ez = dz > 4 ? 8 - dz : dz;
        if (fabs(volume_com_distance(&big, p, q) - sqrt(ex * ex + ey * ey + ez * ez)) > 1e-6) bad_com++;
    }
    CHECK(bad_com == 0);
    /* diferencial d=1: componentes e centro de massa iguais aos do 2D */
    int bad2d = 0;
    for (int it = 0; it < 30; it++) {
        World w;
        Volume v1;
        Boundary bd = (Boundary)(it % 3);
        world_init(&w, 16, 12, bd);
        volume_init(&v1, 16, 12, 1, bd);
        world_soup(&w, (uint64_t)it + 100, 0.3, 0.9);
        volume_soup(&v1, (uint64_t)it + 100, 0.3, 0.9);
        double cx, cy, c3[3];
        if (metric_components(&w) != volume_components(&v1)) bad2d++;
        if (metric_com(&w, &cx, &cy) != volume_com(&v1, c3) || fabs(cx - c3[0]) > 1e-9 || fabs(cy - c3[1]) > 1e-9) bad2d++;
        world_free(&w);
        volume_free(&v1);
    }
    CHECK(bad2d == 0);
    memset(big.c, 0, 960);
    double z3[3];
    CHECK(volume_com(&big, z3) == 0);
    volume_free(&big);
    volume_free(&sh);
    volume_free(&a);
    volume_free(&b);
}

/** @brief Câmera 3D, projeção, linhas e z-buffer (oráculos à mão e relações metamórficas). */
static void test_render3d(void) {
    Camera3 c = { 0, 0, 10, { 0, 0, 0 }, 3.14159265358979323846 / 2 }; /* olho em (0,0,10) olhando -z, fov 90° */
    double sx, sy, d, p[3] = { 1, 0, 0 };
    CHECK(cam3_project(&c, 200, 200, p, &sx, &sy, &d) == 1);
    NEAR(sx, 110.0, 1e-9); /* 100 + (200/2 / tan 45°) * 1/10 */
    NEAR(sy, 100.0, 1e-9);
    NEAR(d, 10.0, 1e-9);
    double q[3] = { 0, 2, 0 };
    cam3_project(&c, 200, 200, q, &sx, &sy, &d);
    NEAR(sy, 80.0, 1e-9); /* y da cena para cima = sy menor */
    double behind[3] = { 0, 0, 20 };
    CHECK(cam3_project(&c, 200, 200, behind, &sx, &sy, &d) == 0);
    /* base ortonormal e alvo no centro da tela (propriedade, ângulos aleatórios) */
    uint64_t s = 9;
    int bad = 0;
    for (int it = 0; it < 500; it++) {
        Camera3 k = { rng_unit(&s) * 20 - 10, rng_unit(&s) * 4 - 2, 2 + rng_unit(&s) * 100, { rng_unit(&s), rng_unit(&s), rng_unit(&s) }, 1.0 };
        cam3_clamp(&k);
        double e[3], r[3], u[3], f[3];
        cam3_basis(&k, e, r, u, f);
        double rr = r[0] * r[0] + r[1] * r[1] + r[2] * r[2], uu = u[0] * u[0] + u[1] * u[1] + u[2] * u[2];
        double ru = r[0] * u[0] + r[1] * u[1] + r[2] * u[2], rf = r[0] * f[0] + r[1] * f[1] + r[2] * f[2];
        if (fabs(rr - 1) > 1e-9 || fabs(uu - 1) > 1e-9 || fabs(ru) > 1e-9 || fabs(rf) > 1e-9) bad++;
        if (!cam3_project(&k, 640, 480, k.target, &sx, &sy, &d) || fabs(sx - 320) > 1e-6 || fabs(sy - 240) > 1e-6) bad++;
        if (fabs(k.pitch) > 89.0 * 3.14159265358979323846 / 180 + 1e-12) bad++;
    }
    CHECK(bad == 0);
    /* linhas */
    uint32_t px[16 * 16] = { 0 };
    Canvas cv = { px, 16, 16 };
    canvas_line(&cv, 1, 1, 4, 1, 0xFFFFFFFFu);
    canvas_line(&cv, 0, 5, 3, 8, 0xFFFFFFFFu);
    int lit = 0;
    for (int i = 0; i < 256; i++) lit += px[i] != 0;
    CHECK(lit == 8 && px[1 * 16 + 4] && px[8 * 16 + 3] && px[6 * 16 + 1]);
    canvas_line(&cv, -500, -300, 900, 700, 0xFFFFFFFFu); /* recorte (ASan) */
    /* render_volume: metamórfico — célula fora da fatia não altera a imagem; dentro, altera */
    Volume v;
    volume_init(&v, 6, 6, 6, BOUND_TORUS);
    static uint32_t A[120 * 90], B[120 * 90];
    Canvas ca = { A, 120, 90 }, cb = { B, 120, 90 };
    Camera3 cam;
    cam3_fit(&cam, &v);
    CHECK(render_volume(&ca, &v, NULL, &cam, 4, 0) == 0);
    *volume_at(&v, 2, 3, 1) = 1;
    render_volume(&cb, &v, NULL, &cam, 4, 0);
    CHECK(memcmp(A, B, sizeof A) == 0);
    render_volume(&cb, &v, NULL, &cam, 1, 0);
    render_volume(&ca, &v, NULL, &cam, -1, 0);
    int diff = 0, not_outline = 0;
    for (int i = 0; i < 120 * 90; i++) if (A[i] != B[i]) { diff++; not_outline += B[i] != 0xFFFFD050u; }
    CHECK(diff > 0 && not_outline == 0); /* fatia z=1 contém a célula: só o contorno amarelo difere do volume inteiro */
    /* z-buffer: duas células na mesma linha de visão; a mais próxima (z maior na cena) vence no centro */
    memset(v.c, 0, 216);
    Camera3 front = { 0, 0, 20, { 0.5, -0.5, 0 }, 1.0 }; /* centro da célula (3,3,z) na cena = (0.5,-0.5,·) */
    *volume_at(&v, 3, 3, 1) = 1;
    *volume_at(&v, 3, 3, 4) = 1;
    uint16_t age[216] = { 0 };
    age[(4 * 6 + 3) * 6 + 3] = 1;   /* perto: ciano (G > R) */
    age[(1 * 6 + 3) * 6 + 3] = 256; /* longe: magenta (G < R) */
    render_volume(&ca, &v, age, &front, -1, 1);
    uint32_t mid = A[45 * 120 + 60];
    CHECK(((mid >> 8) & 255) > ((mid >> 16) & 255));
    age[(4 * 6 + 3) * 6 + 3] = 256;
    age[(1 * 6 + 3) * 6 + 3] = 1;
    render_volume(&ca, &v, age, &front, -1, 1);
    mid = A[45 * 120 + 60];
    CHECK(((mid >> 8) & 255) < ((mid >> 16) & 255));
    /* câmera do outro lado (yaw = 180°): agora a célula próxima é z=1, desenhada ANTES da distante —
       só o z-buffer impede que a distante a sobrescreva */
    front.yaw = 3.14159265358979323846;
    render_volume(&ca, &v, age, &front, -1, 1); /* z=1 tem idade 1 (ciano) */
    mid = A[45 * 120 + 60];
    CHECK(((mid >> 8) & 255) > ((mid >> 16) & 255));
    /* cubo único visto de frente: centro aceso com a face +Z (brilho 0.65 × névoa); longe do cubo = fundo */
    memset(v.c, 0, 216);
    *volume_at(&v, 3, 3, 3) = 1;
    Camera3 head = { 0, 0, 20, { 0.5, -0.5, 0.5 }, 1.0 };
    render_volume(&ca, &v, NULL, &head, -1, 0);
    CHECK(A[45 * 120 + 60] != 0xFF0A0A14u && (A[45 * 120 + 60] & 0xFF) < 230 && A[2 * 120 + 2] == 0xFF0A0A14u);
    /* picking: projetar o centro de uma célula da camada e escolher de volta dá a mesma célula (propriedade) */
    Volume pk;
    volume_init(&pk, 20, 14, 10, BOUND_TORUS);
    int pick_bad = 0, tried = 0;
    for (int it = 0; it < 400; it++) {
        Camera3 k = { rng_unit(&s) * 6.28, rng_unit(&s) * 2.4 - 1.2, 15 + rng_unit(&s) * 40, { 0, 0, 0 }, 1.0 };
        cam3_clamp(&k);
        int gx = (int)rng_below(&s, 20), gy = (int)rng_below(&s, 14), gz = (int)rng_below(&s, 10);
        double pc3[3], ppx, ppy, dd;
        cam3_world(&pk, gx + 0.5, gy + 0.5, gz + 0.5, pc3);
        if (!cam3_project(&k, 800, 600, pc3, &ppx, &ppy, &dd) || ppx < 0 || ppy < 0 || ppx >= 800 || ppy >= 600) continue;
        double e3[3], r3[3], u3[3], f3[3];
        cam3_basis(&k, e3, r3, u3, f3);
        if (fabs(f3[2]) < 0.3) continue; /* plano quase de perfil: 1 pixel cobre várias células */
        tried++;
        int ox, oy;
        if (!cam3_pick_slice(&k, &pk, 800, 600, (int)ppx, (int)ppy, gz, &ox, &oy) || ox != gx || oy != gy) pick_bad++;
    }
    CHECK(tried > 100 && pick_bad == 0);
    volume_free(&pk);
    render_axes_gizmo(&ca, &cam, 100, 70, 30, 3);
    volume_free(&v);
}

/** @brief Lê um arquivo inteiro (até cap-1 bytes) como texto. @return Bytes lidos; -1 se não abriu. */
static long slurp(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = 0;
    return (long)n;
}

/** @brief Exportação (JSON/JSONL/CSV/PPM) e braille colorido. */
static void test_export(void) {
    char out[256] = { 0 };
    FILE *f = fmemopen(out, sizeof out - 1, "w");
    json_str(f, "a\"b\\c\nd");
    fclose(f);
    CHECK(strcmp(out, "\"a\\\"b\\\\c\\u000ad\"") == 0);
    CHECK(mkdir_p("bin/t_exp/sub/dir") == 0 && mkdir_p("bin/t_exp/sub/dir") == 0); /* idempotente */
    SearchConfig sc = { STRAT_MAP, 25, 5, 4, 1, cfg(16, 40, BOUND_TORUS) };
    SearchResult res;
    char err[128];
    CHECK(search_run(&sc, &res, err, sizeof err) == 0);
    CHECK(export_search("bin/t_exp/run", &sc, &res, "teste \"aspas\"") == 0);
    static char buf[200000];
    CHECK(slurp("bin/t_exp/run/run.json", buf, sizeof buf) > 0 && strstr(buf, "\"engine_version\":\"" MC_ENGINE_VERSION "\"") &&
          strstr(buf, "\"strategy\":\"map\"") && strstr(buf, "\"budget\":25") && strstr(buf, "teste \\\"aspas\\\""));
    long n = slurp("bin/t_exp/run/candidates.jsonl", buf, sizeof buf);
    int lines = 0;
    for (long i = 0; i < n; i++) lines += buf[i] == '\n';
    CHECK(lines == 25 && strncmp(buf, "{\"id\":0,", 8) == 0 && strstr(buf, "\"view_cmd\":\"./bin/mcview --rule "));
    CHECK(slurp("bin/t_exp/run/summary.csv", buf, sizeof buf) > 0 &&
          strncmp(buf, "strategy,seed,budget,evaluated,coverage,", 40) == 0 && strstr(buf, "\nmap,4,25,25,"));
    char expect[64];
    snprintf(expect, sizeof expect, "{\"coverage\":%d,", res.coverage);
    CHECK(slurp("bin/t_exp/run/archive.json", buf, sizeof buf) > 0 && strncmp(buf, expect, strlen(expect)) == 0);
    CHECK(slurp("bin/t_exp/run/archive_map.ppm", buf, 16) > 0 && strncmp(buf, "P6\n160 160\n255\n", 15) == 0);
    int rk[ARCH_BINS * ARCH_BINS];
    archive_ranked(&res, rk);
    char ep[128];
    snprintf(ep, sizeof ep, "bin/t_exp/run/elite_00_cand%d.ppm", rk[0]);
    CHECK(slurp(ep, buf, 16) > 0 && strncmp(buf, "P6\n64 64\n255\n", 13) == 0); /* 16 células × escala 4 */
    CHECK(export_search("/proc/nao/pode", &sc, &res, "x") == -1);
    search_free(&res);
    /* export_ppm: 3x3 com escala 2 -> 6x6 pixels, célula viva = (230,230,230) */
    World w;
    world_init(&w, 3, 3, BOUND_TORUS);
    *world_at(&w, 0, 0) = 1;
    CHECK(export_ppm("bin/t_exp/w.ppm", &w, 2) == 0);
    n = slurp("bin/t_exp/w.ppm", buf, sizeof buf);
    CHECK(n == 11 + 6 * 6 * 3 && (unsigned char)buf[11] == 230 && (unsigned char)buf[11 + 2 * 3] == 20);
    /* braille colorido: célula de idade 1 -> cor ciano (120,255,240) em ANSI 24 bits e reset no fim da linha */
    uint16_t age[9] = { 1 };
    memset(out, 0, sizeof out);
    f = fmemopen(out, sizeof out - 1, "w");
    braille_render(f, &w, age);
    fclose(f);
    CHECK(strncmp(out, "\x1b[38;2;120;255;240m\xE2\xA0\x83", 22) == 0 || strncmp(out, "\x1b[38;2;120;255;240m", 19) == 0);
    CHECK(strstr(out, "\x1b[0m\x1b[K\n") != NULL);
    world_free(&w);
}

/** @brief Fase 5: oráculo de propagação à velocidade da luz, propriedade do cone de luz e controles. */
static void test_probe(void) {
    char err[128];
    ProbeConfig pc = { cfg(64, 40, BOUND_FIXED), 20, 3, 1 };
    int sx, sy, rx, ry, gap;
    CHECK(probe_geometry(&pc, &sx, &sy, &rx, &ry, &gap) == 0 && sx == 22 && rx == 42 && sy == 32 && gap == 17);
    pc.sim.bound = BOUND_TORUS;
    pc.dist = 40; /* no toro de 64, o caminho pela volta é 24 */
    CHECK(probe_geometry(&pc, &sx, &sy, &rx, &ry, &gap) == 0 && gap == 21);
    pc.dist = 3;
    ProbeResult pr;
    CHECK(probe_run(R("B3/S23"), &pc, &pr, NULL, err, sizeof err) == -1 && err[0]);
    /* Oráculo: mundo vazio + B12345678/S012345678 — o bloco do sinal cresce exatamente 1 célula por passo;
       o controle fica vazio. Chegada = gap exatamente, em todas as sementes. */
    pc = (ProbeConfig){ cfg(64, 40, BOUND_FIXED), 20, 3, 1 };
    pc.sim.density = 0.0;
    int arr[3];
    CHECK(probe_run(R("B12345678/S012345678"), &pc, &pr, arr, err, sizeof err) == 0);
    CHECK(pr.detected == 3 && pr.min_arrival == 17 && arr[0] == 17 && arr[2] == 17 && pr.light_cone_violations == 0);
    NEAR(pr.median_arrival, 17.0, 1e-12);
    /* orçamento menor que o cone de luz: impossível detectar */
    pc.sim.steps = 16;
    probe_run(R("B12345678/S012345678"), &pc, &pr, arr, err, sizeof err);
    CHECK(pr.detected == 0 && arr[1] == -1 && pr.min_arrival == -1 && pr.median_arrival == -1);
    /* regra que congela tudo: o sinal nunca se propaga */
    pc.sim.steps = 40;
    pc.sim.density = 0.5;
    probe_run(R("B/S012345678"), &pc, &pr, NULL, err, sizeof err);
    CHECK(pr.detected == 0);
    /* Propriedade (controle negativo): nenhuma regra/semente chega antes do cone de luz */
    uint64_t s = 77;
    int viol = 0, det = 0;
    for (int it = 0; it < 40; it++) {
        ProbeConfig q = { cfg(48, 30, (Boundary)(it % 3)), 8 + (int)rng_below(&s, 20), 2, (uint64_t)it };
        q.sim.soup_frac = 0.9;
        probe_run(rule_random(&s), &q, &pr, NULL, err, sizeof err);
        viol += pr.light_cone_violations;
        det += pr.detected;
    }
    CHECK(viol == 0 && det > 0); /* det > 0: o teste exercitou chegadas reais */
    /* agregação com chegadas sintéticas (inclui uma impossível, abaixo do cone de luz) */
    int syn[4] = { 5, -1, 20, 12 };
    probe_tally(syn, 4, 10, &pr);
    CHECK(pr.detected == 3 && pr.light_cone_violations == 1 && pr.min_arrival == 5 && pr.reps == 4 && pr.gap == 10);
    NEAR(pr.median_arrival, 12.0, 1e-12);
    int none[2] = { -1, -1 };
    probe_tally(none, 2, 10, &pr);
    CHECK(pr.detected == 0 && pr.min_arrival == -1 && pr.median_arrival == -1);
    /* região 5x5: cantos contam, fora não */
    World a, b;
    world_init(&a, 16, 16, BOUND_FIXED);
    world_init(&b, 16, 16, BOUND_FIXED);
    CHECK(probe_region_differs(&a, &b, 8, 8) == 0);
    *world_at(&b, 10, 6) = 1; /* canto (rx+2, ry-2) */
    CHECK(probe_region_differs(&a, &b, 8, 8) == 1);
    *world_at(&b, 10, 6) = 0;
    *world_at(&b, 6, 10) = 1; /* canto oposto */
    CHECK(probe_region_differs(&a, &b, 8, 8) == 1);
    *world_at(&b, 6, 10) = 0;
    *world_at(&b, 11, 8) = 1; /* fora (rx+3) */
    CHECK(probe_region_differs(&a, &b, 8, 8) == 0);
    world_free(&a);
    world_free(&b);
}

/** @brief Generations (oráculo à mão do Brian's Brain), diferencial binário e paletas. */
static void test_generations(void) {
    Rule r;
    int st;
    char err[128], buf[40];
    CHECK(rule_parse_gen("B2/S/C3", &r, &st, err, sizeof err) == 0 && st == 3 && r.birth == 4 && r.survive == 0);
    CHECK(rule_parse_gen("B3/S23", &r, &st, err, sizeof err) == 0 && st == 2);
    const char *bad[] = { "B2/S/C1", "B2/S/C256", "B2/S/C", "B2/S/Cx", "B2/S/C3x", "B9/S/C3" };
    for (int i = 0; i < 6; i++) { err[0] = 0; CHECK(rule_parse_gen(bad[i], &r, &st, err, sizeof err) == -1 && err[0]); }
    rule_format_gen(R("B2/S"), 3, buf, sizeof buf);
    CHECK(strcmp(buf, "B2/S/C3") == 0);
    rule_format_gen(R("B3/S23"), 2, buf, sizeof buf);
    CHECK(strcmp(buf, "B3/S23") == 0);
    /* Brian's Brain: (0,0),(1,0) disparando -> viram 2 (morrendo); nascem (0,1),(1,1),(0,4),(1,4) (2 vizinhos);
       no passo seguinte o estado 2 vira 0 (3 estados). Contas à mão. */
    World a, b;
    world_init(&a, 5, 5, BOUND_TORUS);
    world_init(&b, 5, 5, BOUND_TORUS);
    *world_at(&a, 0, 0) = 1;
    *world_at(&a, 1, 0) = 1;
    sim_step_gen(R("B2/S"), 3, &a, &b);
    CHECK(world_get(&b, 0, 0) == 2 && world_get(&b, 1, 0) == 2 && world_get(&b, 0, 1) == 1 && world_get(&b, 1, 1) == 1 &&
          world_get(&b, 0, 4) == 1 && world_get(&b, 1, 4) == 1 && world_count(&b) == 4);
    sim_step_gen(R("B2/S"), 3, &b, &a);
    CHECK(world_get(&a, 0, 0) == 0 && world_get(&a, 1, 0) == 0);
    /* estado de morte não conta como vizinho vivo: célula cercada só por estados 2 não nasce com B1 */
    world_clear(&a);
    *world_at(&a, 2, 2) = 2;
    sim_step_gen(R("B1/S"), 4, &a, &b);
    CHECK(world_get(&b, 2, 1) == 0 && world_get(&b, 2, 2) == 3);
    /* diferencial: states = 2 é idêntico ao binário (passos e rollout) */
    uint64_t s = 5;
    int diff = 0;
    for (int it = 0; it < 30; it++) {
        Rule q = rule_random(&s);
        World w0, w1, w2;
        world_init(&w0, 20, 15, (Boundary)(it % 3)); world_init(&w1, 20, 15, (Boundary)(it % 3)); world_init(&w2, 20, 15, (Boundary)(it % 3));
        world_soup(&w0, (uint64_t)it, 0.5, 0.8);
        Volume v1, v2;
        volume_init(&v1, 20, 15, 1, (Boundary)(it % 3)); volume_init(&v2, 20, 15, 1, (Boundary)(it % 3));
        memcpy(v1.c, w0.c, 300);
        Neighborhood nb;
        neighborhood_make(NB_MOORE2D, &nb);
        for (int t = 0; t < 20; t++) {
            sim_step_gen(q, 2, &w0, &w1);
            volume_step((Rule3){ q.birth, q.survive }, &nb, &v1, &v2);
            if (memcmp(w1.c, v2.c, 300)) { diff++; break; }
            memcpy(w0.c, w1.c, 300);
            memcpy(v1.c, v2.c, 300);
        }
        SimConfig c2 = cfg(20, 30, BOUND_TORUS);
        Metrics m1, m2;
        world_soup(&w0, (uint64_t)it, 0.5, 0.8);
        rollout(q, &c2, &w0, &m1, NULL);
        rollout_gen(q, 2, &c2, &w0, &m2, NULL);
        if (memcmp(&m1, &m2, sizeof m1)) diff++;
        world_free(&w0); world_free(&w1); world_free(&w2); volume_free(&v1); volume_free(&v2);
    }
    CHECK(diff == 0);
    /* Brian's Brain numa sopa: nunca estático, vivas = só estado 1 */
    Metrics m;
    SimConfig c3 = cfg(32, 60, BOUND_TORUS);
    world_soup(&a, 1, 0.5, 0.5);
    World big;
    world_init(&big, 32, 32, BOUND_TORUS);
    world_soup(&big, 3, 0.5, 0.5);
    CHECK(rollout_gen(R("B2/S"), 3, &c3, &big, &m, NULL) == 0 && m.persistence > 0 && m.alive_final <= 32 * 32);
    world_free(&big);
    /* paletas: paradas declaradas */
    uint8_t c[3];
    palette_color_p(PALETTE_FIRE, 1, 0, c);
    CHECK(c[0] == 255 && c[1] == 255 && c[2] == 190);
    palette_color_p(PALETTE_OCEAN, 256, 0, c);
    CHECK(c[0] == 40 && c[1] == 40 && c[2] == 130);
    palette_color_p(PALETTE_MONO, 5, 0, c);
    CHECK(c[0] == 230 && c[1] == 230);
    palette_color_p(99, 1, 0, c); /* inválida -> idade */
    CHECK(c[0] == 120 && c[1] == 255 && c[2] == 240);
    int p;
    CHECK(palette_parse("fogo", &p) == 0 && p == PALETTE_FIRE && palette_parse("azul", &p) == -1);
    CHECK(strcmp(palette_name(PALETTE_OCEAN), "OCEANO") == 0);
    /* render_world: estado 2 aparece em azul (B > R) */
    world_clear(&a);
    *world_at(&a, 0, 0) = 2;
    uint32_t px[4];
    Canvas cv = { px, 2, 2 };
    View one = { 0, 0, 1 };
    render_world(&cv, &a, NULL, NULL, &one, PALETTE_AGE, 0);
    CHECK((px[0] & 255) > ((px[0] >> 16) & 255));
    world_free(&a);
    world_free(&b);
}

/** @brief 1 se dois estados 3D têm candidatos e arquivo idênticos. */
static int same3(const SearchState3 *a, const SearchState3 *b) {
    if (a->n != b->n || memcmp(a->elite, b->elite, sizeof a->elite)) return 0;
    for (int i = 0; i < a->n; i++) {
        const Candidate3 *x = &a->cand[i], *y = &b->cand[i];
        if (x->rule.birth != y->rule.birth || x->rule.survive != y->rule.survive || x->score != y->score ||
            x->parent != y->parent || x->gen != y->gen || x->cell != y->cell || memcmp(&x->m, &y->m, sizeof x->m))
            return 0;
    }
    return 1;
}

/** @brief Busca 3D: retomada idêntica, extensão, mutação, filtros de score e fuzz do checkpoint MC3D. */
static void test_search3d(void) {
    char err[256];
    const char *path = "bin/test_ck3.bin";
    /* mutação: exatamente 1 bit, dentro de 0..maxc, determinística */
    uint64_t s = 3;
    int bad = 0;
    for (int it = 0; it < 3000; it++) {
        int maxc = it % 3 == 0 ? 26 : it % 3 == 1 ? 6 : 8;
        uint64_t s1 = s, s2 = s;
        Rule3 base = rule3_random(maxc, &s1), a = rule3_mutate(base, maxc, &s1);
        (void)rule3_random(maxc, &s2);
        Rule3 b = rule3_mutate(base, maxc, &s2);
        uint32_t m = (1u << (maxc + 1)) - 1;
        int d = __builtin_popcount(a.birth ^ base.birth) + __builtin_popcount(a.survive ^ base.survive);
        if (d != 1 || a.birth != b.birth || a.survive != b.survive || (a.birth & ~m) || (a.survive & ~m) || (base.birth & ~m)) bad++;
        s = s1;
    }
    CHECK(bad == 0);
    /* score 3D: mesmos filtros de ruído */
    Metrics m = { 0 };
    m.steps = 10; m.persistence = 10; m.components_final = 16; m.activity_mean = 0.05; m.density_mean = 0.05;
    NEAR(rule3_score((Rule3){ 0x10, 0x30 }, &m), 1.0, 1e-12);
    CHECK(rule3_score((Rule3){ 0x11, 0x30 }, &m) == 0.0);
    m.activity_mean = 0.3;
    CHECK(rule3_score((Rule3){ 0x10, 0x30 }, &m) == 0.0);
    m.activity_mean = 0.05; m.density_mean = 0.3;
    CHECK(rule3_score((Rule3){ 0x10, 0x30 }, &m) == 0.0);
    /* validação */
    SearchConfig3 bc = search3_default_config();
    bc.d = 2;
    CHECK(search3_config_validate(&bc, err, sizeof err) == -1 && err[0]);
    bc = search3_default_config();
    bc.density = 2;
    CHECK(search3_config_validate(&bc, err, sizeof err) == -1);
    /* retomada idêntica (3 estratégias, dois cortes) e extensão de orçamento */
    for (int st = 0; st < 3; st++) {
        SearchConfig3 sc = search3_default_config();
        sc.strat = (Strategy)st; sc.budget = 30; sc.pop = 6; sc.w = sc.h = sc.d = 8; sc.steps = 20; sc.seed = 9;
        SearchState3 full, part, ld;
        CHECK(search3_begin(&full, &sc, err, sizeof err) == 0);
        while (search3_step(&full, err, sizeof err) == 1) {}
        CHECK(full.n == 30 && search3_step(&full, err, sizeof err) == 0);
        int parents_ok = 1;
        for (int i = 0; i < full.n; i++) {
            int p = full.cand[i].parent;
            if ((st == STRAT_RANDOM && p != -1) || p >= i || (i >= 6 && st != STRAT_RANDOM && p < 0)) parents_ok = 0;
        }
        CHECK(parents_ok);
        /* propriedade do arquivo: a elite de cada célula tem o maior score dela (empate: menor id) */
        int elite_ok = 1;
        for (int j = 0; j < ARCH_BINS * ARCH_BINS; j++) {
            int best = -1;
            for (int i = 0; i < full.n; i++)
                if (full.cand[i].cell == j && (best < 0 || full.cand[i].score > full.cand[best].score)) best = i;
            if (full.elite[j] != best) elite_ok = 0;
        }
        CHECK(elite_ok);
        for (int cut = 4; cut <= 16; cut += 12) {
            search3_begin(&part, &sc, err, sizeof err);
            for (int i = 0; i < cut; i++) search3_step(&part, err, sizeof err);
            CHECK(search3_save(&part, path) == 0);
            search3_state_free(&part);
            CHECK(search3_load(&ld, path, err, sizeof err) == 0 && ld.n == cut);
            while (search3_step(&ld, err, sizeof err) == 1) {}
            CHECK(same3(&full, &ld));
            search3_state_free(&ld);
        }
        SearchConfig3 s15 = sc;
        s15.budget = 15;
        search3_begin(&part, &s15, err, sizeof err);
        while (search3_step(&part, err, sizeof err) == 1) {}
        search3_save(&part, path);
        search3_state_free(&part);
        CHECK(search3_load(&ld, path, err, sizeof err) == 0 && search3_extend(&ld, 30) == 0);
        while (search3_step(&ld, err, sizeof err) == 1) {}
        CHECK(same3(&full, &ld));
        int rk[ARCH_BINS * ARCH_BINS], k = search3_ranked(&ld, rk), sorted = k >= 1;
        for (int i = 1; i < k; i++) sorted &= ld.cand[rk[i - 1]].score >= ld.cand[rk[i]].score;
        CHECK(sorted);
        Rule3 er;
        SearchConfig3 esc;
        int ne;
        search3_save(&ld, path);
        CHECK(checkpoint3_elite(path, 0, &er, &esc, &ne, err, sizeof err) == 0 && ne == k && er.birth == ld.cand[rk[0]].rule.birth &&
              esc.w == 8 && esc.budget == 30);
        CHECK(checkpoint3_elite(path, k, &er, &esc, NULL, err, sizeof err) == -1);
        search3_state_free(&ld);
        search3_state_free(&full);
    }
    /* fuzz do leitor MC3D */
    FILE *f = fopen(path, "rb");
    static unsigned char good[200000], buf[200000];
    size_t len = fread(good, 1, sizeof good, f);
    fclose(f);
    uint64_t r = 99;
    int rejected = 0, accepted = 0;
    for (int it = 0; it < 300; it++) {
        memcpy(buf, good, len);
        size_t L = len;
        if (it % 4 == 0) L = rng_below(&r, (uint32_t)len);
        else for (int k = 0; k < 1 + it % 4; k++) buf[rng_below(&r, (uint32_t)len)] ^= (unsigned char)(1u << rng_below(&r, 8));
        f = fopen(path, "wb");
        fwrite(buf, 1, L, f);
        fclose(f);
        SearchState3 ld;
        if (search3_load(&ld, path, err, sizeof err) == 0) {
            accepted++;
            int steps = 0;
            if (ld.sc.steps <= 200 && (long long)ld.sc.w * ld.sc.h * ld.sc.d <= 4096) /* não rodar configs gigantes corrompidas */
                while (steps < 3 && search3_step(&ld, err, sizeof err) == 1) steps++;
            search3_state_free(&ld);
        } else { rejected++; CHECK(err[0] != 0); }
    }
    CHECK(rejected > 0);
    f = fopen(path, "wb");
    fwrite(good, 1, len, f);
    fputc(0, f);
    fclose(f);
    SearchState3 ld;
    CHECK(search3_load(&ld, path, err, sizeof err) == -1); /* byte extra */
    CHECK(search3_load(&ld, "bin/nao_existe3.bin", err, sizeof err) == -1);
    /* regra com bit fora de 0..26 (Moore 3D) num checkpoint construído à mão: rejeitado */
    f = fopen(path, "wb");
    fwrite(good, 1, len, f);
    fclose(f);
    CHECK(search3_load(&ld, path, err, sizeof err) == 0);
    ld.cand[ld.n - 1].rule.survive |= 1u << 30;
    CHECK(search3_save(&ld, path) == 0);
    search3_state_free(&ld);
    CHECK(search3_load(&ld, path, err, sizeof err) == -1);
    printf("fuzz checkpoint 3D: 300 arquivos corrompidos, %d rejeitados, %d aceitos\n", rejected, accepted);
    remove(path);
}

int main(void) {
    test_search3d();
    test_generations();
    test_probe();
    test_export();
    test_render3d();
    test_volume();
    test_hud();
    test_render();
    test_checkpoint();
    test_rule_parse();
    test_rule_roundtrip_exhaustive();
    test_rule_fuzz();
    test_mutation();
    test_known_patterns();
    test_boundaries();
    test_metrics_cases();
    test_sensitivity();
    test_determinism();
    test_archive();
    test_search();
    printf("%d verificações, %d falhas\n", checks, fails);
    return fails ? 1 : 0;
}
