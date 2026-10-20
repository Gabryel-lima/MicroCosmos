/**
 * @file metrics.c
 * @brief descritores puros sobre um estado. São indicadores, não provas de função.
 */
#include "mc.h"

#include <math.h>
#include <stdlib.h>

#define PI 3.14159265358979323846

int metric_components(const World *w) {
    size_t N = (size_t)w->w * (size_t)w->h;
    uint8_t *seen = calloc(N, 1);
    int *queue = malloc(sizeof(int) * N);
    if (!seen || !queue) { free(seen); free(queue); return -1; }
    int comps = 0;
    for (size_t start = 0; start < N; start++) {
        if (w->c[start] != 1 || seen[start]) continue;
        comps++;
        size_t head = 0, tail = 0;
        queue[tail++] = (int)start;
        seen[start] = 1;
        while (head < tail) {
            int idx = queue[head++], x = idx % w->w, y = idx / w->w;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = mc_wrap(x + dx, w->w, w->bound), ny = mc_wrap(y + dy, w->h, w->bound);
                    if (nx < 0 || ny < 0) continue;
                    size_t ni = (size_t)ny * (size_t)w->w + (size_t)nx;
                    if (w->c[ni] == 1 && !seen[ni]) { seen[ni] = 1; queue[tail++] = (int)ni; }
                }
        }
    }
    free(seen);
    free(queue);
    return comps;
}

/**
 * @brief Média de uma coordenada das células vivas.
 * @param w Mundo (não vazio). @param axis 0 = x, 1 = y. @param n Tamanho do eixo.
 * @return Média; no toro, média circular em [0, n).
 */
/* No toro usa média circular por eixo (invariante a translação módulo o tamanho);
 * para padrões com resultante ~0 (ex.: uniformes) a posição é mal definida. */
static double axis_mean(const World *w, int axis, int n) {
    double sc = 0, ss = 0, sum = 0, cnt = 0;
    for (int y = 0; y < w->h; y++)
        for (int x = 0; x < w->w; x++) {
            if (world_get(w, x, y) != 1) continue;
            double v = axis ? y : x;
            double a = 2.0 * PI * v / n;
            sc += cos(a);
            ss += sin(a);
            sum += v;
            cnt++;
        }
    if (w->bound != BOUND_TORUS) return sum / cnt;
    double m = atan2(ss, sc) * n / (2.0 * PI);
    if (m < 0) m += n;
    if (m >= n) m -= n;
    return m;
}

int metric_com(const World *w, double *cx, double *cy) {
    if (world_count(w) == 0) return 0;
    *cx = axis_mean(w, 0, w->w);
    *cy = axis_mean(w, 1, w->h);
    return 1;
}

double metric_com_distance(const World *w, double x0, double y0, double x1, double y1) {
    double dx = fabs(x1 - x0), dy = fabs(y1 - y0);
    if (w->bound == BOUND_TORUS) {
        if (dx > w->w - dx) dx = w->w - dx;
        if (dy > w->h - dy) dy = w->h - dy;
    }
    return sqrt(dx * dx + dy * dy);
}
