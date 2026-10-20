/**
 * @file main.c
 * @brief CLI do Microcosmos: run | search | compare | smoke.
 */
#include "mc.h"
#include "opts.h"

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

/** @brief 1 após Ctrl+C: a busca salva checkpoint e sai; o tui encerra. */
static volatile sig_atomic_t g_stop = 0;
/** @brief Handler de SIGINT: só marca g_stop. @param s Sinal (ignorado). */
static void on_sigint(int s) { (void)s; g_stop = 1; }

/** @brief Imprime a mensagem de uso em stderr. */
static void usage(void) {
    fprintf(stderr,
            "uso:\n"
            "  microcosmos run --rule B3/S23[/C<n>] [--ppm arquivo.ppm] [--braille] [opções de simulação]   (/C<n> = Generations)\n"
            "  microcosmos search --strategy random|evo|map [--budget N] [--pop N] [--seed N] [--out DIR] [opções]\n"
            "  microcosmos compare [--budget N] [--reps N] [--out DIR] [opções]\n"
            "  microcosmos search ... [--checkpoint-every N] [--checkpoint-dir DIR] [--save ARQ.bin] [--resume ARQ.bin [--budget N]]\n"
            "  microcosmos elites --checkpoint ARQ.bin            (lista elites, sem janela)\n"
            "  microcosmos tui --rule B3/S23[/C<n>] | --checkpoint ARQ.bin [--rank K] [--fps N] [--steps N] [--mono] [--palette idade|fogo|oceano|mono] [opções]\n"
            "  microcosmos run3d --rule B5-7/S4-6 [--nb moore|vn|moore2d] [--d N] [--braille [--slice K]] [opções]  (motor 3D)\n"
            "  microcosmos probe --rule R [--dist D] [--reps N] [opções]  (fase 5: sinal na fonte, efeito no alvo, controle de cone de luz)\n"
            "  microcosmos search3d [--strategy random|evo|map] [--budget N] [--nb moore|vn] [--d N] [--checkpoint-every N]\n"
            "                       [--save ARQ.bin] [--resume ARQ.bin [--budget N]] [--out DIR] [opções]  (padrão 16³, 60 passos)\n"
            "  microcosmos elites3d --checkpoint ARQ.bin        (lista elites 3D e o comando mcview3d)\n"
            "  microcosmos smoke\n"
            "opções de simulação:\n" SIM_OPTS_HELP);
}

/** @brief Junta argv com espaços (registrado em run.json). @param buf Destino. @param n Tamanho. @param argc,argv Argumentos. */
static void join_args(char *buf, size_t n, int argc, char **argv) {
    buf[0] = '\0';
    for (int i = 0; i < argc; i++) {
        strncat(buf, argv[i], n - strlen(buf) - 1);
        if (i + 1 < argc) strncat(buf, " ", n - strlen(buf) - 1);
    }
}

/** @brief Opções da linha de comando. */
typedef struct {
    SearchConfig sc;
    const char *rule;       /**< --rule: regra B/S. */
    const char *out;        /**< --out: diretório de resultados. */
    const char *ppm;        /**< --ppm: imagem do estado final (run). */
    const char *checkpoint; /**< --checkpoint: checkpoint a ler (elites, tui). */
    const char *resume;     /**< --resume: checkpoint para continuar a busca. */
    const char *save;       /**< --save: caminho do checkpoint final/Ctrl+C. */
    const char *ckdir;      /**< --checkpoint-dir: diretório dos checkpoints periódicos. */
    int reps;               /**< --reps: sementes por estratégia (compare). */
    int fps;                /**< --fps: quadros por segundo (tui). */
    int mono;               /**< --mono: tui sem cores. */
    int braille;            /**< --braille: run imprime o estado final em braille. */
    int every;              /**< --checkpoint-every: avaliações entre checkpoints; 0 = desligado. */
    int rank;               /**< --rank: posição da elite (0 = melhor). */
    int budget_set;         /**< 1 se --budget foi passado (para estender ao retomar). */
    int depth;              /**< --d: profundidade do volume (run3d). */
    const char *nb;         /**< --nb: vizinhança do run3d (moore, vn, moore2d). */
    int slice;              /**< --slice: plano z mostrado em braille no run3d; -1 = projeção. */
    int dist;               /**< --dist: distância fonte-alvo (probe). */
    int palette;            /**< --palette: paleta do tui (PALETTE_*). */
} Args;

/**
 * @brief Lê as opções a partir de argv[2].
 * @param argc,argv Argumentos. @param A Opções (pré-preenchidas com padrões).
 * @return 0 ok; -1 erro (mensagem já impressa).
 */
static int parse_args(int argc, char **argv, Args *A) {
    for (int i = 2; i < argc; i++) {
        int r = parse_sim_opt(argc, argv, &i, &A->sc.sim, &A->sc.world_seed);
        if (r < 0) return -1;
        if (r) continue;
        const char *a = argv[i];
        if (!strcmp(a, "--mono")) { A->mono = 1; continue; }
        if (!strcmp(a, "--braille")) { A->braille = 1; continue; }
        if (i + 1 >= argc) { fprintf(stderr, "erro: opção desconhecida ou sem valor: %s\n", a); return -1; }
        const char *v = argv[++i];
        long n;
        if (!strcmp(a, "--rule")) A->rule = v;
        else if (!strcmp(a, "--out")) A->out = v;
        else if (!strcmp(a, "--ppm")) A->ppm = v;
        else if (!strcmp(a, "--strategy")) { if (strategy_parse(v, &A->sc.strat)) goto bad; }
        else if (!strcmp(a, "--seed")) { if (parse_u64(v, &A->sc.seed)) goto bad; }
        else if (!strcmp(a, "--budget")) { if (parse_int(v, 1, 10000000, &n)) goto bad; A->sc.budget = (int)n; A->budget_set = 1; }
        else if (!strcmp(a, "--pop")) { if (parse_int(v, 1, 100000, &n)) goto bad; A->sc.pop = (int)n; }
        else if (!strcmp(a, "--checkpoint")) A->checkpoint = v;
        else if (!strcmp(a, "--resume")) A->resume = v;
        else if (!strcmp(a, "--save")) A->save = v;
        else if (!strcmp(a, "--checkpoint-dir")) A->ckdir = v;
        else if (!strcmp(a, "--checkpoint-every")) { if (parse_int(v, 1, 10000000, &n)) goto bad; A->every = (int)n; }
        else if (!strcmp(a, "--rank")) { if (parse_int(v, 0, ARCH_BINS * ARCH_BINS - 1, &n)) goto bad; A->rank = (int)n; }
        else if (!strcmp(a, "--d")) { if (parse_int(v, 1, 1024, &n)) goto bad; A->depth = (int)n; }
        else if (!strcmp(a, "--nb")) A->nb = v;
        else if (!strcmp(a, "--palette")) { if (palette_parse(v, &A->palette)) goto bad; }
        else if (!strcmp(a, "--dist")) { if (parse_int(v, 4, 4096, &n)) goto bad; A->dist = (int)n; }
        else if (!strcmp(a, "--slice")) { if (parse_int(v, 0, 1023, &n)) goto bad; A->slice = (int)n; }
        else if (!strcmp(a, "--fps")) { if (parse_int(v, 1, 240, &n)) goto bad; A->fps = (int)n; }
        else if (!strcmp(a, "--reps")) { if (parse_int(v, 1, 1000, &n)) goto bad; A->reps = (int)n; }
        else { fprintf(stderr, "erro: opção desconhecida: %s\n", a); return -1; }
        continue;
    bad:
        fprintf(stderr, "erro: valor inválido para %s: \"%s\"\n", a, v);
        return -1;
    }
    char err[256];
    if (sim_config_validate(&A->sc.sim, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return -1; }
    return 0;
}

/** @brief Comando `run`: um rollout, métricas em JSON, opcionalmente PPM/braille. @return Código de saída. */
static int cmd_run(Args *A) {
    Rule r;
    char err[256];
    if (!A->rule) { fprintf(stderr, "erro: --rule é obrigatório\n"); return 2; }
    int states;
    if (rule_parse_gen(A->rule, &r, &states, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return 2; }
    World init, fin;
    SimConfig *c = &A->sc.sim;
    if (world_init(&init, c->w, c->h, c->bound) || world_init(&fin, c->w, c->h, c->bound)) return 1;
    world_soup(&init, A->sc.world_seed, c->density, c->soup_frac);
    Metrics m;
    if (rollout_gen(r, states, c, &init, &m, &fin)) { fprintf(stderr, "erro: rollout falhou\n"); return 1; }
    char rs[RULE_STR_MAX + 8];
    rule_format_gen(r, states, rs, sizeof rs);
    printf("{\"engine_version\":\"%s\",\"rule\":\"%s\",\"world_seed\":%llu,\"sim\":", MC_ENGINE_VERSION, rs,
           (unsigned long long)A->sc.world_seed);
    json_simconfig(stdout, c);
    printf(",\"score\":%.6f,\"cell\":%d,\"metrics\":", rule_score(r, &m), archive_cell(&m));
    json_metrics(stdout, &m);
    printf("}\n");
    if (A->braille) braille_render(stdout, &fin, NULL);
    if (A->ppm && export_ppm(A->ppm, &fin, 4)) { fprintf(stderr, "erro: não foi possível escrever %s\n", A->ppm); return 1; }
    world_free(&init);
    world_free(&fin);
    return 0;
}

/** @brief Comando `search`: busca nova ou retomada (--resume), checkpoints e exportação. @return Código de saída. */
static int cmd_search(Args *A, const char *cmdline) {
    SearchState st;
    char err[256], dir[512], ck[700];
    if (A->resume) {
        if (search_load(&st, A->resume, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return 1; }
        if (A->budget_set && search_extend(&st, A->sc.budget)) { fprintf(stderr, "erro: sem memória\n"); return 1; }
        printf("retomando %s: %d/%d avaliados\n", A->resume, st.res.n, st.sc.budget);
    } else if (search_begin(&st, &A->sc, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return 1; }
    if (A->out) snprintf(dir, sizeof dir, "%s", A->out);
    else snprintf(dir, sizeof dir, "runs/%s_s%llu_b%d", strategy_name(st.sc.strat), (unsigned long long)st.sc.seed, st.sc.budget);
    char ckd[600];
    snprintf(ckd, sizeof ckd, "%s", A->ckdir ? A->ckdir : dir);
    if (!A->ckdir) strncat(ckd, "/checkpoints", sizeof ckd - strlen(ckd) - 1);
    snprintf(ck, sizeof ck, "%s", ckd);
    if (A->save) { /* cria o diretório pai do caminho pedido */
        snprintf(ck, sizeof ck, "%s", A->save);
        char *slash = strrchr(ck, '/');
        if (slash) *slash = '\0'; else snprintf(ck, sizeof ck, ".");
        if (mkdir_p(ck)) { fprintf(stderr, "erro: não foi possível criar %s\n", ck); search_state_free(&st); return 1; }
        snprintf(ck, sizeof ck, "%s", ckd);
    }
    if (mkdir_p(ck)) { fprintf(stderr, "erro: não foi possível criar %s\n", ck); search_state_free(&st); return 1; }
    clock_t t0 = clock();
    int r;
    signal(SIGINT, on_sigint); /* Ctrl+C: salva checkpoint e sai; retome com --resume */
    while (!g_stop && (r = search_step(&st, err, sizeof err)) == 1) {
        if (A->every && st.res.n % A->every == 0) {
            snprintf(ck, sizeof ck, "%s/ck_%07d.bin", ckd, st.res.n);
            search_stats(&st.res);
            printf("[checkpoint] %d/%d cobertura=%d qd=%.4f melhor=%.4f -> %s%s\n", st.res.n, st.sc.budget, st.res.coverage,
                   st.res.qd_score, st.res.best_score, ck, search_save(&st, ck) ? " (FALHA)" : "");
            fflush(stdout);
        }
    }
    if (!g_stop && r < 0) { fprintf(stderr, "erro: %s\n", err); search_state_free(&st); return 1; }
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    search_stats(&st.res);
    if (A->save) snprintf(ck, sizeof ck, "%s", A->save);
    else snprintf(ck, sizeof ck, "%s/checkpoint.bin", dir);
    int rc = search_save(&st, ck);
    printf("checkpoint salvo em %s%s\n", ck, rc ? " (FALHA)" : "");
    if (!g_stop) rc |= export_search(dir, &st.sc, &st.res, cmdline);
    printf("%s: avaliados=%d/%d cobertura=%d/%d qd=%.4f melhor=%.4f mediana=%.4f extintos=%.2f cpu=%.2fs -> %s%s%s\n",
           strategy_name(st.sc.strat), st.res.n, st.sc.budget, st.res.coverage, ARCH_BINS * ARCH_BINS, st.res.qd_score,
           st.res.best_score, st.res.median_score, st.res.extinct_frac, secs, dir, g_stop ? " (INTERROMPIDO; use --resume)" : "",
           rc ? " (FALHA AO SALVAR)" : "");
    search_state_free(&st);
    return rc || g_stop ? 1 : 0;
}

/** @brief Comando `elites`: lista as elites de um checkpoint. @return Código de saída. */
static int cmd_elites(Args *A) {
    SearchState st;
    char err[256];
    if (!A->checkpoint) { fprintf(stderr, "erro: --checkpoint é obrigatório\n"); return 2; }
    if (search_load(&st, A->checkpoint, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return 1; }
    search_stats(&st.res);
    int rk[ARCH_BINS * ARCH_BINS], k = archive_ranked(&st.res, rk);
    printf("%s: estratégia=%s avaliados=%d/%d cobertura=%d qd=%.4f\n", A->checkpoint, strategy_name(st.sc.strat), st.res.n,
           st.sc.budget, st.res.coverage, st.res.qd_score);
    printf("rank  id     regra                   score  cel(x,y) persist ativ.   gen\n");
    for (int i = 0; i < k; i++) {
        const Candidate *c = &st.res.cand[rk[i]];
        char rs[RULE_STR_MAX];
        rule_format(c->rule, rs, sizeof rs);
        printf("%4d  %-6d %-22s %6.3f  (%d,%d)    %5d  %.4f  %d\n", i, c->id, rs, c->score, c->cell % ARCH_BINS,
               c->cell / ARCH_BINS, c->m.persistence, c->m.activity_mean, c->gen);
    }
    search_state_free(&st);
    return 0;
}

/* Mesmo orçamento, mesma condição inicial, `reps` sementes por estratégia; reporta mediana e extremos. */
/** @brief Comando `compare`: random/evo/map com orçamento igual. @return Código de saída. */
static int cmd_compare(Args *A, const char *cmdline) {
    const char *dir = A->out ? A->out : "runs/compare";
    if (mkdir_p(dir)) { fprintf(stderr, "erro: não foi possível criar %s\n", dir); return 1; }
    char p[600];
    snprintf(p, sizeof p, "%s/summary.csv", dir);
    FILE *f = fopen(p, "w");
    snprintf(p, sizeof p, "%s/run.json", dir);
    FILE *fr = fopen(p, "w");
    if (!f || !fr) { fprintf(stderr, "erro: escrita em %s\n", dir); return 1; }
    fprintf(fr, "{\"engine_version\":\"%s\",\"command\":", MC_ENGINE_VERSION);
    json_str(fr, cmdline);
    fprintf(fr, ",\"budget\":%d,\"pop\":%d,\"reps\":%d,\"seeds\":\"1..reps\",\"world_seed\":%llu,\"sim\":", A->sc.budget,
            A->sc.pop, A->reps, (unsigned long long)A->sc.world_seed);
    json_simconfig(fr, &A->sc.sim);
    fprintf(fr, "}\n");
    fclose(fr);
    fprintf(f, "strategy,seed,budget,evaluated,coverage,qd_score,best_score,median_score,extinct_frac\n");
    printf("%-7s %-26s %-26s\n", "estrat.", "cobertura med [min,max]", "qd_score med [min,max]");
    for (int s = 0; s < 3; s++) {
        double *cov = malloc(sizeof(double) * (size_t)A->reps), *qd = malloc(sizeof(double) * (size_t)A->reps);
        if (!cov || !qd) return 1;
        for (int k = 0; k < A->reps; k++) {
            SearchConfig sc = A->sc;
            sc.strat = (Strategy)s;
            sc.seed = (uint64_t)k + 1;
            SearchResult res;
            char err[256];
            if (search_run(&sc, &res, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return 1; }
            fprintf(f, "%s,%llu,%d,%d,%d,%.6f,%.6f,%.6f,%.4f\n", strategy_name(sc.strat), (unsigned long long)sc.seed,
                    sc.budget, res.n, res.coverage, res.qd_score, res.best_score, res.median_score, res.extinct_frac);
            cov[k] = res.coverage;
            qd[k] = res.qd_score;
            search_free(&res);
        }
        double mc = median_of(cov, A->reps), mq = median_of(qd, A->reps); /* median_of ordena */
        printf("%-7s %6.1f [%3.0f,%3.0f]          %7.3f [%.3f,%.3f]\n", strategy_name((Strategy)s), mc, cov[0],
               cov[A->reps - 1], mq, qd[0], qd[A->reps - 1]);
        free(cov);
        free(qd);
    }
    fclose(f);
    printf("-> %s/summary.csv\n", dir);
    return 0;
}

/** @brief Comando `tui`: animação braille de uma regra ou de uma elite de checkpoint. @return Código de saída. */
/* Animação no terminal: cada caractere braille mostra 2x4 células; cor = idade da célula mais velha. */
static int cmd_tui(Args *A) {
    Rule r;
    char err[256];
    int states = 2;
    if (A->checkpoint) { /* reproduz uma elite com a configuração exata em que foi avaliada */
        if (checkpoint_elite(A->checkpoint, A->rank, &r, &A->sc, NULL, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return 1; }
    } else if (rule_parse_gen(A->rule ? A->rule : "B3/S23", &r, &states, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return 2; }
    SimConfig *c = &A->sc.sim;
    World a, b;
    if (world_init(&a, c->w, c->h, c->bound) || world_init(&b, c->w, c->h, c->bound)) return 1;
    size_t N = (size_t)c->w * (size_t)c->h;
    uint16_t *age = calloc(N, sizeof *age);
    uint8_t *trail = calloc(N, 1);
    if (!age || !trail) return 1;
    world_soup(&a, A->sc.world_seed, c->density, c->soup_frac);
    age_update(&a, age, trail);
    signal(SIGINT, on_sigint);
    char rs[RULE_STR_MAX + 8];
    rule_format_gen(r, states, rs, sizeof rs);
    printf("\x1b[2J\x1b[?25l");
    struct timespec ts = { 0, 1000000000L / A->fps };
    for (int t = 0; t <= c->steps && !g_stop; t++) {
        printf("\x1b[H%s  t=%d  vivos=%d  (Ctrl+C sai)\x1b[K\n", rs, t, world_count(&a));
        braille_render_p(stdout, &a, A->mono ? NULL : age, A->palette);
        fflush(stdout);
        nanosleep(&ts, NULL);
        sim_step_gen(r, states, &a, &b);
        World tmp = a; a = b; b = tmp;
        age_update(&a, age, trail);
    }
    printf("\x1b[0m\x1b[?25h");
    free(age);
    free(trail);
    world_free(&a);
    world_free(&b);
    return 0;
}

/** @brief Comando `run3d`: rollout volumétrico, métricas em JSON e projeção braille opcional. @return Código de saída. */
static int cmd_run3d(Args *A) {
    char err[256], rs[RULE3_STR_MAX];
    NeighborhoodKind k = NB_MOORE3D;
    Neighborhood nb;
    Rule3 r;
    if (A->nb && neighborhood_parse(A->nb, &k)) { fprintf(stderr, "erro: --nb deve ser moore, vn ou moore2d\n"); return 2; }
    neighborhood_make(k, &nb);
    int d = A->depth ? A->depth : (k == NB_MOORE2D ? 1 : A->sc.sim.w);
    if (k != NB_MOORE2D && d < 3) { fprintf(stderr, "erro: vizinhança 3D exige --d >= 3\n"); return 2; }
    if (!A->rule) { fprintf(stderr, "erro: --rule é obrigatório\n"); return 2; }
    if (A->slice >= d) { fprintf(stderr, "erro: --slice fora de 0..%d\n", d - 1); return 2; }
    if (rule3_parse(A->rule, nb.n, &r, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return 2; }
    SimConfig *c = &A->sc.sim;
    Volume init, fin;
    if (volume_init(&init, c->w, c->h, d, c->bound) || volume_init(&fin, c->w, c->h, d, c->bound)) {
        fprintf(stderr, "erro: dimensões inválidas (lados 3..1024, d 1..1024, total <= 2^26) ou sem memória\n");
        return 2;
    }
    volume_soup(&init, A->sc.world_seed, c->density, c->soup_frac);
    Metrics m;
    if (rollout3(r, &nb, c->steps, &init, &m, &fin)) { fprintf(stderr, "erro: rollout falhou\n"); return 1; }
    rule3_format(r, rs, sizeof rs);
    printf("{\"engine_version\":\"%s\",\"rule\":\"%s\",\"neighborhood\":\"%s\",\"w\":%d,\"h\":%d,\"d\":%d,"
           "\"boundary\":\"%s\",\"world_seed\":%llu,\"density\":%.6f,\"soup_frac\":%.6f,\"metrics\":",
           MC_ENGINE_VERSION, rs, neighborhood_name(k), c->w, c->h, d, boundary_name(c->bound),
           (unsigned long long)A->sc.world_seed, c->density, c->soup_frac);
    json_metrics(stdout, &m);
    printf("}\n");
    if (A->braille) {
        World p;
        if (world_init(&p, c->w, c->h, BOUND_FIXED) == 0) {
            if (A->slice >= 0 && volume_slice(&fin, A->slice, &p)) fprintf(stderr, "erro: --slice fora de 0..%d\n", d - 1);
            else {
                if (A->slice < 0) volume_project(&fin, &p);
                printf("%s\n", A->slice >= 0 ? "fatia z selecionada:" : "projeção ao longo de z (qualquer viva na coluna):");
                braille_render(stdout, &p, NULL);
            }
            world_free(&p);
        }
    }
    volume_free(&init);
    volume_free(&fin);
    return 0;
}

/** @brief Comando `probe`: experimento de intervenção (fase 5). @return Código de saída. */
static int cmd_probe(Args *A) {
    Rule r;
    char err[256], rs[RULE_STR_MAX];
    if (!A->rule) { fprintf(stderr, "erro: --rule é obrigatório\n"); return 2; }
    if (rule_parse(A->rule, &r, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return 2; }
    ProbeConfig pc = { A->sc.sim, A->dist, A->reps, A->sc.world_seed };
    ProbeResult pr;
    int *arr = malloc(sizeof(int) * (size_t)pc.reps);
    if (!arr) return 1;
    if (probe_run(r, &pc, &pr, arr, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); free(arr); return 2; }
    rule_format(r, rs, sizeof rs);
    printf("{\"rule\":\"%s\",\"dist\":%d,\"reps\":%d,\"seed0\":%llu,\"sim\":", rs, pc.dist, pc.reps,
           (unsigned long long)pc.seed0);
    json_simconfig(stdout, &pc.sim);
    printf(",\"detected\":%d,\"gap\":%d,\"light_cone_violations\":%d,\"min_arrival\":%d,\"median_arrival\":%.1f,\"arrivals\":[",
           pr.detected, pr.gap, pr.light_cone_violations, pr.min_arrival, pr.median_arrival);
    for (int k = 0; k < pc.reps; k++) printf("%s%d", k ? "," : "", arr[k]);
    printf("],\"criterio\":\"detectado = alvo 5x5 difere do controle dentro de steps; não implica comunicação\"}\n");
    free(arr);
    return pr.light_cone_violations ? 1 : 0;
}

/** @brief Monta a SearchConfig3 a partir das opções (defaults 3D aplicados antes do parse em main). */
static int args_to_sc3(const Args *A, SearchConfig3 *sc, char *err, size_t n) {
    *sc = search3_default_config();
    sc->strat = A->sc.strat;
    sc->budget = A->sc.budget;
    sc->pop = A->sc.pop;
    sc->seed = A->sc.seed;
    sc->world_seed = A->sc.world_seed;
    if (A->nb && neighborhood_parse(A->nb, &sc->nb)) { snprintf(err, n, "--nb deve ser moore, vn ou moore2d"); return -1; }
    sc->w = A->sc.sim.w;
    sc->h = A->sc.sim.h;
    sc->d = A->depth ? A->depth : (sc->nb == NB_MOORE2D ? 1 : 16);
    sc->bound = A->sc.sim.bound;
    sc->steps = A->sc.sim.steps;
    sc->density = A->sc.sim.density;
    sc->soup_frac = A->sc.sim.soup_frac;
    return search3_config_validate(sc, err, n);
}

/** @brief Comando `search3d`: busca 3D com checkpoints (MC3D), retomada e exportação de candidatos. @return Código de saída. */
static int cmd_search3d(Args *A, const char *cmdline) {
    SearchState3 st;
    SearchConfig3 sc;
    char err[256], dir[512], ck[700];
    if (A->resume) {
        if (search3_load(&st, A->resume, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return 1; }
        if (A->budget_set && search3_extend(&st, A->sc.budget)) { fprintf(stderr, "erro: sem memória\n"); return 1; }
        printf("retomando %s: %d/%d avaliados\n", A->resume, st.n, st.sc.budget);
    } else {
        if (args_to_sc3(A, &sc, err, sizeof err) || search3_begin(&st, &sc, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return 2; }
    }
    if (A->out) snprintf(dir, sizeof dir, "%s", A->out);
    else snprintf(dir, sizeof dir, "runs/3d_%s_s%llu_b%d", strategy_name(st.sc.strat), (unsigned long long)st.sc.seed, st.sc.budget);
    char ckd[600];
    snprintf(ckd, sizeof ckd, "%s", A->ckdir ? A->ckdir : dir);
    if (!A->ckdir) strncat(ckd, "/checkpoints", sizeof ckd - strlen(ckd) - 1);
    if (mkdir_p(ckd) || mkdir_p(dir)) { fprintf(stderr, "erro: não foi possível criar %s\n", ckd); search3_state_free(&st); return 1; }
    if (A->save) {
        snprintf(ck, sizeof ck, "%s", A->save);
        char *slash = strrchr(ck, '/');
        if (slash) { *slash = '\0'; if (mkdir_p(ck)) { fprintf(stderr, "erro: não foi possível criar %s\n", ck); search3_state_free(&st); return 1; } }
    }
    signal(SIGINT, on_sigint);
    int r = 1;
    clock_t t0 = clock();
    while (!g_stop && (r = search3_step(&st, err, sizeof err)) == 1) {
        if (A->every && st.n % A->every == 0) {
            snprintf(ck, sizeof ck, "%s/ck3d_%07d.bin", ckd, st.n);
            printf("[checkpoint] %d/%d -> %s%s\n", st.n, st.sc.budget, ck, search3_save(&st, ck) ? " (FALHA)" : "");
            fflush(stdout);
        }
    }
    if (!g_stop && r < 0) { fprintf(stderr, "erro: %s\n", err); search3_state_free(&st); return 1; }
    if (A->save) snprintf(ck, sizeof ck, "%s", A->save);
    else snprintf(ck, sizeof ck, "%s/checkpoint3d.bin", dir);
    int rc = search3_save(&st, ck);
    char p[700];
    snprintf(p, sizeof p, "%s/candidates3d.jsonl", dir);
    FILE *f = g_stop ? NULL : fopen(p, "w");
    if (f) {
        for (int i = 0; i < st.n; i++) {
            const Candidate3 *c = &st.cand[i];
            char rs[RULE3_STR_MAX];
            rule3_format(c->rule, rs, sizeof rs);
            fprintf(f, "{\"id\":%d,\"parent\":%d,\"gen\":%d,\"rule\":\"%s\",\"score\":%.6f,\"cell\":%d,\"metrics\":", c->id,
                    c->parent, c->gen, rs, c->score, c->cell);
            json_metrics(f, &c->m);
            fprintf(f, "}\n");
        }
        fclose(f);
        snprintf(p, sizeof p, "%s/run3d.json", dir);
        if ((f = fopen(p, "w"))) {
            fprintf(f, "{\"engine_version\":\"%s\",\"command\":", MC_ENGINE_VERSION);
            json_str(f, cmdline);
            fprintf(f, ",\"strategy\":\"%s\",\"budget\":%d,\"pop\":%d,\"seed\":%llu,\"world_seed\":%llu,\"neighborhood\":\"%s\","
                       "\"w\":%d,\"h\":%d,\"d\":%d,\"boundary\":\"%s\",\"steps\":%d,\"density\":%.6f,\"soup_frac\":%.6f}\n",
                    strategy_name(st.sc.strat), st.sc.budget, st.sc.pop, (unsigned long long)st.sc.seed,
                    (unsigned long long)st.sc.world_seed, neighborhood_name(st.sc.nb), st.sc.w, st.sc.h, st.sc.d,
                    boundary_name(st.sc.bound), st.sc.steps, st.sc.density, st.sc.soup_frac);
            fclose(f);
        }
    }
    int cov = 0;
    double qd = 0;
    for (int j = 0; j < ARCH_BINS * ARCH_BINS; j++) if (st.elite[j] >= 0) { cov++; qd += st.cand[st.elite[j]].score; }
    printf("3d %s: avaliados=%d/%d cobertura=%d/%d qd=%.4f cpu=%.2fs checkpoint=%s%s%s\n", strategy_name(st.sc.strat), st.n,
           st.sc.budget, cov, ARCH_BINS * ARCH_BINS, qd, (double)(clock() - t0) / CLOCKS_PER_SEC, ck,
           g_stop ? " (INTERROMPIDO; use --resume)" : "", rc ? " (FALHA AO SALVAR)" : "");
    search3_state_free(&st);
    return rc || g_stop ? 1 : 0;
}

/** @brief Comando `elites3d`: lista as elites de um checkpoint MC3D e o comando para abri-las. @return Código de saída. */
static int cmd_elites3d(Args *A) {
    SearchState3 st;
    char err[256];
    if (!A->checkpoint) { fprintf(stderr, "erro: --checkpoint é obrigatório\n"); return 2; }
    if (search3_load(&st, A->checkpoint, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return 1; }
    int rk[ARCH_BINS * ARCH_BINS], k = search3_ranked(&st, rk);
    printf("%s: estratégia=%s avaliados=%d/%d volume=%dx%dx%d vizinhança=%s elites=%d\n", A->checkpoint,
           strategy_name(st.sc.strat), st.n, st.sc.budget, st.sc.w, st.sc.h, st.sc.d, neighborhood_name(st.sc.nb), k);
    printf("rank  id     regra                        score  cel(x,y) persist ativ.   gen\n");
    for (int i = 0; i < k; i++) {
        const Candidate3 *c = &st.cand[rk[i]];
        char rs[RULE3_STR_MAX];
        rule3_format(c->rule, rs, sizeof rs);
        printf("%4d  %-6d %-28s %6.3f  (%d,%d)    %5d  %.4f  %d\n", i, c->id, rs, c->score, c->cell % ARCH_BINS,
               c->cell / ARCH_BINS, c->m.persistence, c->m.activity_mean, c->gen);
    }
    printf("abrir: ./bin/mcview3d --checkpoint %s --rank K\n", A->checkpoint);
    search3_state_free(&st);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 2; }
    Args A = { .sc = { STRAT_MAP, 200, 16, 1, 1, sim_default_config() }, .reps = 5, .fps = 15, .slice = -1, .dist = 20, .palette = PALETTE_AGE };
    char cmdline[2048];
    join_args(cmdline, sizeof cmdline, argc, argv);
    const char *cmd = argv[1];
    if (!strcmp(cmd, "smoke")) { /* caso curto e previsível */
        A.sc.budget = 30;
        A.sc.sim.steps = 100;
        A.sc.sim.w = A.sc.sim.h = 32;
        A.out = "runs/smoke";
        return cmd_search(&A, cmdline);
    }
    if (!strcmp(cmd, "run3d")) A.sc.sim.w = A.sc.sim.h = 32; /* padrão 3D: 32^3; --w/--h sobrepõem */
    if (!strcmp(cmd, "search3d")) { /* padrões 3D; --w/--h/--steps/--density sobrepõem */
        A.sc.sim.w = A.sc.sim.h = 16;
        A.sc.sim.steps = 60;
        A.sc.sim.density = 0.35;
    }
    if (parse_args(argc, argv, &A)) { usage(); return 2; }
    if (!strcmp(cmd, "run")) return cmd_run(&A);
    if (!strcmp(cmd, "search")) return cmd_search(&A, cmdline);
    if (!strcmp(cmd, "run3d")) return cmd_run3d(&A);
    if (!strcmp(cmd, "probe")) return cmd_probe(&A);
    if (!strcmp(cmd, "search3d")) return cmd_search3d(&A, cmdline);
    if (!strcmp(cmd, "elites3d")) return cmd_elites3d(&A);
    if (!strcmp(cmd, "elites")) return cmd_elites(&A);
    if (!strcmp(cmd, "tui")) return cmd_tui(&A);
    if (!strcmp(cmd, "compare")) return cmd_compare(&A, cmdline);
    usage();
    return 2;
}
