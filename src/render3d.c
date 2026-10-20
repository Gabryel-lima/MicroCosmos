/**
 * @file render3d.c
 * @brief Renderização 3D por software (sem SDL): câmera orbital, projeção perspectiva, z-buffer,
 * caixa do mundo e gizmo de eixos. Desenha no mesmo Canvas do HUD 2D.
 */
#include "mc.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI3 3.14159265358979323846

/** @brief Produto escalar. */
static double dot3(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/** @brief Normaliza no lugar (vetor nulo fica inalterado). */
static void norm3(double v[3]) {
    double l = sqrt(dot3(v, v));
    if (l > 0) for (int i = 0; i < 3; i++) v[i] /= l;
}

/** @brief Produto vetorial out = a × b. */
static void cross3(const double a[3], const double b[3], double out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

void cam3_fit(Camera3 *c, const Volume *v) {
    double diag = sqrt((double)v->w * v->w + (double)v->h * v->h + (double)v->d * v->d);
    c->target[0] = c->target[1] = c->target[2] = 0;
    c->yaw = PI3 / 5;
    c->pitch = PI3 / 7;
    c->fov = PI3 / 3;
    c->dist = diag * 1.1;
}

void cam3_clamp(Camera3 *c) {
    const double lim = 89.0 * PI3 / 180.0;
    if (c->pitch > lim) c->pitch = lim;
    if (c->pitch < -lim) c->pitch = -lim;
    if (c->dist < 2.0) c->dist = 2.0;
    if (c->dist > 5000.0) c->dist = 5000.0;
    c->yaw = fmod(c->yaw, 2 * PI3);
}

void cam3_basis(const Camera3 *c, double eye[3], double right[3], double up[3], double fwd[3]) {
    double dir[3] = { cos(c->pitch) * sin(c->yaw), sin(c->pitch), cos(c->pitch) * cos(c->yaw) };
    for (int i = 0; i < 3; i++) {
        eye[i] = c->target[i] + c->dist * dir[i];
        fwd[i] = -dir[i];
    }
    const double world_up[3] = { 0, 1, 0 };
    cross3(fwd, world_up, right);
    norm3(right);
    cross3(right, fwd, up);
}

int cam3_pick_slice(const Camera3 *c, const Volume *v, int cw, int ch, int px, int py, int z, int *cx, int *cy) {
    double eye[3], r[3], u[3], f[3];
    cam3_basis(c, eye, r, u, f);
    double foc = (ch / 2.0) / tan(c->fov / 2), a = (px + 0.5 - cw / 2.0) / foc, b = -(py + 0.5 - ch / 2.0) / foc;
    double dir[3], plane[3];
    for (int i = 0; i < 3; i++) dir[i] = f[i] + a * r[i] + b * u[i];
    cam3_world(v, 0, 0, z + 0.5, plane); /* plano z da cena passando pelo meio da camada */
    if (fabs(dir[2]) < 1e-12) return 0;  /* raio paralelo ao plano */
    double t = (plane[2] - eye[2]) / dir[2];
    if (t <= 0) return 0;                /* plano atrás da câmera */
    double sx = eye[0] + t * dir[0], sy = eye[1] + t * dir[1];
    int gx = (int)floor(sx + v->w / 2.0), gy = (int)floor(-sy + v->h / 2.0);
    if (gx < 0 || gy < 0 || gx >= v->w || gy >= v->h) return 0;
    *cx = gx;
    *cy = gy;
    return 1;
}

int cam3_project(const Camera3 *c, int cw, int ch, const double p[3], double *sx, double *sy, double *depth) {
    double eye[3], r[3], u[3], f[3], v[3];
    cam3_basis(c, eye, r, u, f);
    for (int i = 0; i < 3; i++) v[i] = p[i] - eye[i];
    double z = dot3(v, f);
    if (z <= CAM3_NEAR) return 0;
    double foc = (ch / 2.0) / tan(c->fov / 2);
    *sx = cw / 2.0 + foc * dot3(v, r) / z;
    *sy = ch / 2.0 - foc * dot3(v, u) / z;
    *depth = z;
    return 1;
}

void cam3_world(const Volume *v, double x, double y, double z, double out[3]) {
    out[0] = x - v->w / 2.0;
    out[1] = -(y - v->h / 2.0); /* y da grade cresce para baixo; na cena, para cima */
    out[2] = z - v->d / 2.0;
}

void canvas_line(Canvas *cv, int x0, int y0, int x1, int y1, uint32_t argb) {
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (int guard = 0; guard < 100000; guard++) { /* Bresenham; guard evita laço longo com coordenadas absurdas */
        canvas_fill_rect(cv, x0, y0, 1, 1, argb);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

/**
 * @brief Linha 3D projetada com teste de profundidade: 1/z é interpolado linearmente na tela (correto em
 * perspectiva); pixels atrás do z-buffer são omitidos. Descartada se algum extremo estiver atrás da câmera.
 * @param cv Canvas. @param zb Z-buffer do canvas. @param c Câmera. @param a,b Extremos na cena. @param argb Cor.
 */
static void line3(Canvas *cv, const float *zb, const Camera3 *c, const double a[3], const double b[3], uint32_t argb) {
    double ax, ay, ad, bx, by, bd;
    if (!cam3_project(c, cv->w, cv->h, a, &ax, &ay, &ad) || !cam3_project(c, cv->w, cv->h, b, &bx, &by, &bd)) return;
    if (fabs(ax) > 1e5 || fabs(ay) > 1e5 || fabs(bx) > 1e5 || fabs(by) > 1e5) return;
    int x0 = (int)lround(ax), y0 = (int)lround(ay), x1 = (int)lround(bx), y1 = (int)lround(by);
    int n = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);
    for (int k = 0; k <= n; k++) {
        double t = n ? (double)k / n : 0.0;
        int x = (int)lround(x0 + (x1 - x0) * t), y = (int)lround(y0 + (y1 - y0) * t);
        if (x < 0 || y < 0 || x >= cv->w || y >= cv->h) continue;
        double z = 1.0 / ((1 - t) / ad + t / bd);
        if (z > zb[(size_t)y * (size_t)cv->w + (size_t)x] + 0.5) continue; /* atrás de um cubo */
        canvas_fill_rect(cv, x, y, 1, 1, argb);
    }
}

/** @brief Projeção de um vértice: posição na tela e 1/z (interpolável linearmente na tela). */
typedef struct {
    double x;   /**< Pixel x. */
    double y;   /**< Pixel y. */
    double iz;  /**< 1 / profundidade. */
} PVert;

/**
 * @brief Rasteriza um triângulo com teste de profundidade (1/z interpolado por baricêntricas).
 * @param cv Canvas. @param zb Z-buffer. @param a,b,c Vértices projetados. @param argb Cor.
 */
static void tri3(Canvas *cv, float *zb, PVert a, PVert b, PVert c, uint32_t argb) {
    double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (fabs(area) < 1e-12) return;
    int x0 = (int)floor(fmin(a.x, fmin(b.x, c.x))), x1 = (int)ceil(fmax(a.x, fmax(b.x, c.x)));
    int y0 = (int)floor(fmin(a.y, fmin(b.y, c.y))), y1 = (int)ceil(fmax(a.y, fmax(b.y, c.y)));
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > cv->w - 1) x1 = cv->w - 1;
    if (y1 > cv->h - 1) y1 = cv->h - 1;
    for (int py = y0; py <= y1; py++)
        for (int px = x0; px <= x1; px++) {
            double qx = px + 0.5, qy = py + 0.5;
            double w0 = ((b.x - qx) * (c.y - qy) - (b.y - qy) * (c.x - qx)) / area;
            double w1 = ((c.x - qx) * (a.y - qy) - (c.y - qy) * (a.x - qx)) / area;
            double w2 = 1.0 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            double z = 1.0 / (w0 * a.iz + w1 * b.iz + w2 * c.iz);
            size_t q = (size_t)py * (size_t)cv->w + (size_t)px;
            if (z >= zb[q]) continue;
            zb[q] = (float)z;
            cv->px[q] = argb;
        }
}

int render_volume(Canvas *cv, const Volume *v, const uint16_t *age, const Camera3 *c, int slice, int color) {
    size_t P = (size_t)cv->w * (size_t)cv->h;
    float *zb = malloc(sizeof(float) * P);
    if (!zb) return -1;
    for (size_t i = 0; i < P; i++) { zb[i] = 1e30f; cv->px[i] = 0xFF0A0A14u; }
    double eye[3], rr[3], uu[3], ff[3];
    cam3_basis(c, eye, rr, uu, ff);
    double near = 1e30, far = 0;
    for (int k = 0; k < 8; k++) { /* faixa de profundidade da caixa, para a névoa */
        double p[3], sx, sy, d;
        cam3_world(v, (k & 1) ? v->w : 0, (k & 2) ? v->h : 0, (k & 4) ? v->d : 0, p);
        if (cam3_project(c, cv->w, cv->h, p, &sx, &sy, &d)) { if (d < near) near = d; if (d > far) far = d; }
    }
    if (far <= near) { near = CAM3_NEAR; far = near + 1; }
    /* 6 faces: direção na GRADE (dx,dy,dz), normal na CENA, 4 cantos (bits x,y,z do canto) e brilho */
    static const struct { int g[3]; double n[3]; int k[4]; double light; } F[6] = {
        { { 1, 0, 0 }, { 1, 0, 0 }, { 1, 3, 7, 5 }, 0.80 },   { { -1, 0, 0 }, { -1, 0, 0 }, { 0, 4, 6, 2 }, 0.80 },
        { { 0, -1, 0 }, { 0, 1, 0 }, { 0, 1, 5, 4 }, 1.00 },  { { 0, 1, 0 }, { 0, -1, 0 }, { 2, 6, 7, 3 }, 0.45 },
        { { 0, 0, 1 }, { 0, 0, 1 }, { 4, 5, 7, 6 }, 0.65 },   { { 0, 0, -1 }, { 0, 0, -1 }, { 0, 2, 3, 1 }, 0.65 },
    };
    for (int z = 0; z < v->d; z++) {
        if (slice >= 0 && z != slice) continue;
        for (int y = 0; y < v->h; y++)
            for (int x = 0; x < v->w; x++) {
                size_t i = ((size_t)z * (size_t)v->h + (size_t)y) * (size_t)v->w + (size_t)x;
                if (!v->c[i]) continue;
                PVert pv[8];
                int ok = 1;
                double dc = 0;
                for (int k = 0; k < 8 && ok; k++) {
                    double p[3], d;
                    cam3_world(v, x + (k & 1), y + ((k >> 1) & 1), z + ((k >> 2) & 1), p);
                    ok = cam3_project(c, cv->w, cv->h, p, &pv[k].x, &pv[k].y, &d);
                    if (ok && (fabs(pv[k].x) > 1e5 || fabs(pv[k].y) > 1e5)) ok = 0;
                    pv[k].iz = ok ? 1.0 / d : 0;
                    dc += d / 8;
                }
                if (!ok) continue; /* cubo cruzando o plano da câmera: omitido */
                uint8_t col[3] = { 230, 230, 230 };
                if (color && age) palette_color_p(color, age[i], 0, col);
                double fog = 1.0 - 0.6 * (dc - near) / (far - near);
                if (fog < 0.3) fog = 0.3;
                if (fog > 1.0) fog = 1.0;
                double ctr[3];
                cam3_world(v, x + 0.5, y + 0.5, z + 0.5, ctr);
                for (int f = 0; f < 6; f++) {
                    int nx = x + F[f].g[0], ny = y + F[f].g[1], nz = z + F[f].g[2];
                    if (slice < 0 && nx >= 0 && ny >= 0 && nz >= 0 && nx < v->w && ny < v->h && nz < v->d &&
                        volume_get(v, nx, ny, nz))
                        continue; /* face interna (encostada em outra célula viva) */
                    double to_eye[3] = { eye[0] - (ctr[0] + 0.5 * F[f].n[0]), eye[1] - (ctr[1] + 0.5 * F[f].n[1]),
                                         eye[2] - (ctr[2] + 0.5 * F[f].n[2]) };
                    if (dot3(to_eye, F[f].n) <= 0) continue; /* face de costas para a câmera */
                    double sh = fog * F[f].light;
                    uint32_t argb = 0xFF000000u | ((uint32_t)(col[0] * sh) << 16) | ((uint32_t)(col[1] * sh) << 8) |
                                    (uint32_t)(col[2] * sh);
                    tri3(cv, zb, pv[F[f].k[0]], pv[F[f].k[1]], pv[F[f].k[2]], argb);
                    tri3(cv, zb, pv[F[f].k[0]], pv[F[f].k[2]], pv[F[f].k[3]], argb);
                }
            }
    }
    /* caixa do mundo (12 arestas) e, no modo fatia, o retângulo do plano z=slice */
    static const int E[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
    double corner[8][3];
    for (int k = 0; k < 8; k++) cam3_world(v, (k & 1) ? v->w : 0, (k & 2) ? v->h : 0, (k & 4) ? v->d : 0, corner[k]);
    for (int e = 0; e < 12; e++) line3(cv, zb, c, corner[E[e][0]], corner[E[e][1]], 0xFF5A5A80u);
    if (slice >= 0) {
        double q[4][3];
        for (int k = 0; k < 4; k++) cam3_world(v, (k & 1) ? v->w : 0, (k & 2) ? v->h : 0, slice + 0.5, q[k]);
        line3(cv, zb, c, q[0], q[1], 0xFFFFD050u); line3(cv, zb, c, q[1], q[3], 0xFFFFD050u);
        line3(cv, zb, c, q[3], q[2], 0xFFFFD050u); line3(cv, zb, c, q[2], q[0], 0xFFFFD050u);
    }
    free(zb);
    return 0;
}

void render_axes_gizmo(Canvas *cv, const Camera3 *c, int cx, int cy, int len, int scale) {
    double eye[3], r[3], u[3], f[3];
    cam3_basis(c, eye, r, u, f);
    /* eixos da GRADE: +X = direita, +Y = para baixo na grade (−y da cena), +Z = profundidade */
    const double ax[3][3] = { { 1, 0, 0 }, { 0, -1, 0 }, { 0, 0, 1 } };
    const uint32_t col[3] = { 0xFFFF5050u, 0xFF50FF50u, 0xFF5090FFu };
    const char *lab[3] = { "X", "Y", "Z" };
    for (int a = 0; a < 3; a++) {
        int ex = cx + (int)lround(len * dot3(ax[a], r)), ey = cy - (int)lround(len * dot3(ax[a], u));
        int th = scale > 1 ? scale / 2 + 1 : 1; /* espessura cresce com a UI */
        for (int o = 0; o < th; o++) {
            canvas_line(cv, cx + o, cy, ex + o, ey, col[a]);
            canvas_line(cv, cx, cy + o, ex, ey + o, col[a]);
        }
        canvas_text(cv, ex + 2 * scale, ey - 2 * scale, lab[a], col[a], scale);
    }
}
