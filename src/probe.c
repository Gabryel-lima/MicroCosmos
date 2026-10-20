/**
 * @file probe.c
 * @brief Fase 5: experimento de intervenção com controle (sinal em uma região, efeito medido em outra).
 *
 * Para cada semente: rollout de controle e rollout com o bloco 3x3 da fonte invertido em t=0.
 * Mede o primeiro passo em que a região-alvo 5x5 difere entre os dois. Controle negativo: o cone de luz
 * (vizinhança de raio 1 ⇒ no máximo 1 célula por passo) torna impossível chegar antes de `gap` passos;
 * qualquer chegada anterior é contada como violação (indicaria defeito no medidor ou no motor).
 * "Detectado" significa apenas "a região-alvo mudou dentro do orçamento" — não é chamado de comunicação.
 */
#include "mc.h"

#include <stdlib.h>
#include <string.h>

int probe_geometry(const ProbeConfig *pc, int *sx, int *sy, int *rx, int *ry, int *gap) {
    const SimConfig *c = &pc->sim;
    int cx = c->w / 2, cy = c->h / 2;
    *sx = cx - pc->dist / 2;
    *rx = *sx + pc->dist;
    *sy = *ry = cy;
    if (pc->dist < 4 || *sx - 1 < 0 || *rx + 2 >= c->w || cy - 2 < 0 || cy + 2 >= c->h) return -1;
    int d = pc->dist;
    if (c->bound == BOUND_TORUS && c->w - d < d) d = c->w - d; /* no toro o caminho pode dar a volta */
    *gap = d - 3; /* distância de Chebyshev entre as células mais próximas da fonte (raio 1) e do alvo (raio 2) */
    return 0;
}

int probe_region_differs(const World *a, const World *b, int rx, int ry) {
    for (int y = ry - 2; y <= ry + 2; y++)
        for (int x = rx - 2; x <= rx + 2; x++)
            if (world_get(a, x, y) != world_get(b, x, y)) return 1;
    return 0;
}

void probe_tally(const int *arrivals, int reps, int gap, ProbeResult *res) {
    memset(res, 0, sizeof *res);
    res->reps = reps;
    res->gap = gap;
    res->min_arrival = -1;
    res->median_arrival = -1;
    double *arr = malloc(sizeof(double) * (size_t)(reps > 0 ? reps : 1));
    int nd = 0;
    for (int k = 0; k < reps; k++) {
        int a = arrivals[k];
        if (a < 0) continue;
        if (arr) arr[nd] = a;
        nd++;
        if (a < gap) res->light_cone_violations++;
        if (res->min_arrival < 0 || a < res->min_arrival) res->min_arrival = a;
    }
    res->detected = nd;
    if (nd && arr) res->median_arrival = median_of(arr, nd);
    free(arr);
}

int probe_run(Rule r, const ProbeConfig *pc, ProbeResult *res, int *arrivals, char *err, size_t n) {
    memset(res, 0, sizeof *res);
    int sx, sy, rx, ry, gap;
    if (sim_config_validate(&pc->sim, err, n)) return -1;
    if (pc->reps < 1) { snprintf(err, n, "reps deve ser >= 1"); return -1; }
    if (probe_geometry(pc, &sx, &sy, &rx, &ry, &gap)) {
        snprintf(err, n, "dist deve ser >= 4 e caber na grade (fonte e alvo inteiros dentro dela)");
        return -1;
    }
    const SimConfig *c = &pc->sim;
    World ctl, sig, t1, t2;
    if (world_init(&ctl, c->w, c->h, c->bound) || world_init(&sig, c->w, c->h, c->bound) ||
        world_init(&t1, c->w, c->h, c->bound) || world_init(&t2, c->w, c->h, c->bound)) {
        world_free(&ctl); world_free(&sig); world_free(&t1);
        snprintf(err, n, "sem memória");
        return -1;
    }
    int *own = arrivals ? NULL : malloc(sizeof(int) * (size_t)pc->reps), *arv = arrivals ? arrivals : own;
    if (!arv) { world_free(&ctl); world_free(&sig); world_free(&t1); world_free(&t2); snprintf(err, n, "sem memória"); return -1; }
    for (int k = 0; k < pc->reps; k++) {
        world_soup(&ctl, pc->seed0 + (uint64_t)k, c->density, c->soup_frac);
        world_copy(&sig, &ctl);
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) { uint8_t *p = world_at(&sig, sx + dx, sy + dy); *p = !*p; }
        int arrival = -1;
        for (int t = 1; t <= c->steps && arrival < 0; t++) {
            sim_step(r, &ctl, &t1);
            sim_step(r, &sig, &t2);
            world_copy(&ctl, &t1);
            world_copy(&sig, &t2);
            if (probe_region_differs(&ctl, &sig, rx, ry)) arrival = t;
        }
        arv[k] = arrival;
    }
    probe_tally(arv, pc->reps, gap, res);
    free(own);
    world_free(&ctl); world_free(&sig); world_free(&t1); world_free(&t2);
    return 0;
}
