/**
 * @file sim.c
 * @brief passo de atualização e rollout com métricas calculadas online.
 */
#include "mc.h"

#include <stdlib.h>
#include <string.h>

void sim_step_gen(Rule r, int states, const World *src, World *dst) {
    int w = src->w, h = src->h;
    Boundary b = src->bound;
    for (int y = 0; y < h; y++) {
        int ys[3] = { mc_wrap(y - 1, h, b), y, mc_wrap(y + 1, h, b) };
        for (int x = 0; x < w; x++) {
            int xs[3] = { mc_wrap(x - 1, w, b), x, mc_wrap(x + 1, w, b) };
            int n = 0;
            for (int j = 0; j < 3; j++) {
                if (ys[j] < 0) continue;
                for (int i = 0; i < 3; i++) {
                    if (xs[i] < 0 || (i == 1 && j == 1)) continue;
                    n += world_get(src, xs[i], ys[j]) == 1; /* só células no estado 1 contam */
                }
            }
            uint8_t cur = world_get(src, x, y), next;
            if (cur == 0) next = (uint8_t)((r.birth >> n) & 1u);
            else if (cur == 1) next = ((r.survive >> n) & 1u) ? 1 : (states > 2 ? 2 : 0);
            else next = cur + 1 >= states ? 0 : (uint8_t)(cur + 1); /* morrendo: avança até voltar a 0 */
            *world_at(dst, x, y) = next;
        }
    }
    dst->bound = b;
}

void sim_step(Rule r, const World *src, World *dst) { sim_step_gen(r, 2, src, dst); }

SimConfig sim_default_config(void) {
    SimConfig c = { 64, 64, BOUND_TORUS, 300, 0.5, 0.5, 1 };
    return c;
}

int sim_config_validate(const SimConfig *c, char *err, size_t n) {
    if (c->w < 3 || c->h < 3 || c->w > 4096 || c->h > 4096) { snprintf(err, n, "grade deve ter lados em 3..4096"); return -1; }
    if (c->steps < 1 || c->steps > 1000000) { snprintf(err, n, "steps deve estar em 1..1000000"); return -1; }
    if (!(c->density >= 0.0 && c->density <= 1.0)) { snprintf(err, n, "density deve estar em [0,1]"); return -1; }
    if (!(c->soup_frac >= 0.0 && c->soup_frac <= 1.0)) { snprintf(err, n, "frac deve estar em [0,1]"); return -1; }
    if ((unsigned)c->bound > BOUND_REFLECT) { snprintf(err, n, "borda inválida"); return -1; }
    return 0;
}

int rollout_gen(Rule r, int states, const SimConfig *cfg, const World *init, Metrics *m, World *final_out) {
    World a, b;
    int T = cfg->steps;
    memset(m, 0, sizeof *m);
    m->steps = T;
    m->sensitivity = -1.0;
    if (world_init(&a, init->w, init->h, init->bound)) return -1;
    if (world_init(&b, init->w, init->h, init->bound)) { world_free(&a); return -1; }
    uint64_t *hashes = malloc(sizeof(uint64_t) * ((size_t)T + 1));
    if (!hashes) { world_free(&a); world_free(&b); return -1; }

    world_copy(&a, init);
    double N = (double)init->w * init->h;
    double cx0 = 0, cy0 = 0;
    int has0 = metric_com(&a, &cx0, &cy0);
    hashes[0] = world_hash(&a);
    double dens_sum = 0, act_sum = 0;
    int alive = world_count(&a), persisting = 1, t;

    for (t = 1; t <= T; t++) {
        sim_step_gen(r, states, &a, &b);
        int changed = world_diff(&a, &b);
        alive = world_count(&b);
        dens_sum += alive / N;
        act_sum += changed / N;
        if (persisting && alive > 0 && changed > 0) m->persistence++;
        else persisting = 0;
        hashes[t] = world_hash(&b);
        if (!m->cycle_period)
            for (int s = t - 1; s >= 0; s--)
                if (hashes[s] == hashes[t]) { m->cycle_period = t - s; m->cycle_start = s; break; }
        World tmp = a; a = b; b = tmp;
        if (changed == 0) { /* ponto fixo: os passos restantes são idênticos */
            dens_sum += (double)(T - t) * alive / N;
            break;
        }
    }
    m->steps_simulated = t > T ? T : t;
    m->alive_final = alive;
    m->extinct = alive == 0;
    m->density_mean = dens_sum / T;
    m->activity_mean = act_sum / T;
    double cx1, cy1;
    if (has0 && metric_com(&a, &cx1, &cy1)) m->com_displacement = metric_com_distance(&a, cx0, cy0, cx1, cy1);
    m->components_final = metric_components(&a);

    int rc = 0;
    if (cfg->perturb) {
        SimConfig c2 = *cfg;
        c2.perturb = 0;
        Metrics mp;
        world_copy(&b, init);
        world_perturb_center(&b);
        World fp;
        if (world_init(&fp, init->w, init->h, init->bound) == 0) {
            if (rollout_gen(r, states, &c2, &b, &mp, &fp) == 0) m->sensitivity = world_diff(&a, &fp) / N;
            else rc = -1;
            world_free(&fp);
        } else rc = -1;
    }
    if (final_out) world_copy(final_out, &a);
    free(hashes);
    world_free(&a);
    world_free(&b);
    return rc;
}

int rollout(Rule r, const SimConfig *cfg, const World *init, Metrics *m, World *final_out) {
    return rollout_gen(r, 2, cfg, init, m, final_out);
}
