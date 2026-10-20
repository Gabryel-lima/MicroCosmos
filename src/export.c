/**
 * @file export.c
 * @brief JSON/JSONL/CSV/PPM de resultados.
 */
#include "mc.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

int mkdir_p(const char *path) {
    char buf[1024];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof buf) return -1;
    memcpy(buf, path, len + 1);
    for (size_t i = 1; i <= len; i++) {
        if (buf[i] == '/' || buf[i] == '\0') {
            char c = buf[i];
            buf[i] = '\0';
            if (mkdir(buf, 0755) && errno != EEXIST) return -1;
            buf[i] = c;
        }
    }
    return 0;
}

void json_str(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') fprintf(f, "\\%c", c);
        else if (c < 0x20) fprintf(f, "\\u%04x", c);
        else fputc(c, f);
    }
    fputc('"', f);
}

void json_metrics(FILE *f, const Metrics *m) {
    fprintf(f,
            "{\"steps\":%d,\"steps_simulated\":%d,\"alive_final\":%d,\"extinct\":%d,\"persistence\":%d,"
            "\"density_mean\":%.6f,\"activity_mean\":%.6f,\"cycle_period\":%d,\"cycle_start\":%d,"
            "\"com_displacement\":%.4f,\"components_final\":%d,\"sensitivity\":%.6f}",
            m->steps, m->steps_simulated, m->alive_final, m->extinct, m->persistence, m->density_mean,
            m->activity_mean, m->cycle_period, m->cycle_start, m->com_displacement, m->components_final,
            m->sensitivity);
}

void json_simconfig(FILE *f, const SimConfig *c) {
    fprintf(f, "{\"w\":%d,\"h\":%d,\"boundary\":\"%s\",\"steps\":%d,\"density\":%.6f,\"soup_frac\":%.6f,\"perturb\":%d}",
            c->w, c->h, boundary_name(c->bound), c->steps, c->density, c->soup_frac, c->perturb);
}

int export_ppm(const char *path, const World *w, int scale) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", w->w * scale, w->h * scale);
    for (int y = 0; y < w->h * scale; y++)
        for (int x = 0; x < w->w * scale; x++) {
            unsigned char v = world_get(w, x / scale, y / scale) == 1 ? 230 : 20;
            unsigned char px[3] = { v, v, (unsigned char)(v ? v : 40) };
            fwrite(px, 1, 3, f);
        }
    return fclose(f);
}

/** @brief Abre `dir/name` para escrita. @return Arquivo ou NULL. */
static FILE *open_in(const char *dir, const char *name) {
    char p[1200];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    return fopen(p, "w");
}

/** @brief Monta o comando mcview que reabre o candidato. @param buf Destino. @param n Tamanho. @param c Candidato. @param s Simulação. */
static void view_cmd(char *buf, size_t n, const Candidate *c, const SimConfig *s) {
    char r[RULE_STR_MAX];
    rule_format(c->rule, r, sizeof r);
    snprintf(buf, n, "./bin/mcview --rule %s --world-seed %llu --w %d --h %d --boundary %s --density %g --frac %g",
             r, (unsigned long long)c->world_seed, s->w, s->h, boundary_name(s->bound), s->density, s->soup_frac);
}

/** @brief Escreve um candidato como objeto JSON (com métricas e view_cmd). @param f Arquivo. @param c Candidato. @param s Simulação. */
static void cand_json(FILE *f, const Candidate *c, const SimConfig *s) {
    char r[RULE_STR_MAX], vc[512];
    rule_format(c->rule, r, sizeof r);
    view_cmd(vc, sizeof vc, c, s);
    fprintf(f, "{\"id\":%d,\"parent\":%d,\"gen\":%d,\"rule\":\"%s\",\"world_seed\":%llu,\"score\":%.6f,\"cell\":%d,\"metrics\":",
            c->id, c->parent, c->gen, r, (unsigned long long)c->world_seed, c->score, c->cell);
    json_metrics(f, &c->m);
    fprintf(f, ",\"view_cmd\":");
    json_str(f, vc);
    fputc('}', f);
}

int export_search(const char *dir, const SearchConfig *sc, const SearchResult *res, const char *cmdline) {
    if (mkdir_p(dir)) return -1;
    FILE *f = open_in(dir, "run.json");
    if (!f) return -1;
    char date[64];
    time_t now = time(NULL);
    strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%S%z", localtime(&now));
    fprintf(f, "{\"date\":\"%s\",\"engine_version\":\"%s\",\"code_version\":\"unknown (sem git)\",\"command\":", date,
            MC_ENGINE_VERSION);
    json_str(f, cmdline);
    fprintf(f, ",\"strategy\":\"%s\",\"budget\":%d,\"pop\":%d,\"seed\":%llu,\"world_seed\":%llu,\"sim\":",
            strategy_name(sc->strat), sc->budget, sc->pop, (unsigned long long)sc->seed,
            (unsigned long long)sc->world_seed);
    json_simconfig(f, &sc->sim);
    fprintf(f, ",\"archive\":{\"bins\":%d,\"x\":\"persistence/steps\",\"y\":\"activity_mean/%.3f\"},"
               "\"score\":\"0 se B0, activity_mean>=%.2f ou density_mean>=%.2f; senão persistence/steps * min(1, components_final/16)\"}\n",
            ARCH_BINS, ARCH_ACT_MAX, ARCH_ACT_MAX, SCORE_DENS_MAX);
    fclose(f);

    if (!(f = open_in(dir, "candidates.jsonl"))) return -1;
    for (int i = 0; i < res->n; i++) { cand_json(f, &res->cand[i], &sc->sim); fputc('\n', f); }
    fclose(f);

    int elites[ARCH_BINS * ARCH_BINS], ne = archive_ranked(res, elites);

    if (!(f = open_in(dir, "archive.json"))) return -1;
    fprintf(f, "{\"coverage\":%d,\"qd_score\":%.6f,\"elites\":[\n", res->coverage, res->qd_score);
    for (int k = 0; k < ne; k++) {
        const Candidate *c = &res->cand[elites[k]];
        fprintf(f, "  {\"cell_x\":%d,\"cell_y\":%d,\"candidate\":", c->cell % ARCH_BINS, c->cell / ARCH_BINS);
        cand_json(f, c, &sc->sim);
        fprintf(f, "}%s\n", k + 1 < ne ? "," : "");
    }
    fprintf(f, "]}\n");
    fclose(f);

    if (!(f = open_in(dir, "summary.csv"))) return -1;
    fprintf(f, "strategy,seed,budget,evaluated,coverage,qd_score,best_score,median_score,extinct_frac\n");
    fprintf(f, "%s,%llu,%d,%d,%d,%.6f,%.6f,%.6f,%.4f\n", strategy_name(sc->strat), (unsigned long long)sc->seed,
            sc->budget, res->n, res->coverage, res->qd_score, res->best_score, res->median_score, res->extinct_frac);
    fclose(f);

    /* Mapa do arquivo (y cresce para cima) e estados finais das 8 melhores elites. */
    {
        char p[1200];
        snprintf(p, sizeof p, "%s/archive_map.ppm", dir);
        if ((f = fopen(p, "wb"))) {
            int s = 16;
            fprintf(f, "P6\n%d %d\n255\n", ARCH_BINS * s, ARCH_BINS * s);
            for (int y = ARCH_BINS * s - 1; y >= 0; y--)
                for (int x = 0; x < ARCH_BINS * s; x++) {
                    int e = res->arch.elite[(y / s) * ARCH_BINS + x / s];
                    unsigned char px[3] = { 15, 15, 30 };
                    if (e >= 0) {
                        double q = res->cand[e].score;
                        px[0] = (unsigned char)(60 + 195 * q);
                        px[1] = (unsigned char)(90 + 100 * q);
                        px[2] = (unsigned char)(140 - 100 * q);
                    }
                    fwrite(px, 1, 3, f);
                }
            fclose(f);
        }
    }
    World init, fin;
    if (world_init(&init, sc->sim.w, sc->sim.h, sc->sim.bound) == 0 &&
        world_init(&fin, sc->sim.w, sc->sim.h, sc->sim.bound) == 0) {
        SimConfig c2 = sc->sim;
        c2.perturb = 0;
        for (int k = 0; k < ne && k < 8; k++) {
            const Candidate *c = &res->cand[elites[k]];
            Metrics m;
            world_soup(&init, c->world_seed, sc->sim.density, sc->sim.soup_frac);
            if (rollout(c->rule, &c2, &init, &m, &fin)) continue;
            char p[1200];
            snprintf(p, sizeof p, "%s/elite_%02d_cand%d.ppm", dir, k, c->id);
            export_ppm(p, &fin, 4);
        }
        world_free(&fin);
    }
    world_free(&init);
    return 0;
}
