/**
 * @file render.c
 * @brief renderização sem SDL: idade/rastro das células, paleta e arte braille no terminal.
 */
#include "mc.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

/* age = passos consecutivos vivo (recém-nascida = 1, morta = 0).
 * trail = rastro de morte: 255 no passo em que a célula morre, depois decai 7/8 por passo. */
void age_update(const World *w, uint16_t *age, uint8_t *trail) {
    age_update_cells(w->c, (size_t)w->w * (size_t)w->h, age, trail);
}

void age_update_cells(const uint8_t *c, size_t N, uint16_t *age, uint8_t *trail) {
    for (size_t i = 0; i < N; i++) {
        if (c[i] == 1) {
            if (age[i] < UINT16_MAX) age[i]++;
            trail[i] = 0;
        } else {
            trail[i] = age[i] ? 255 : (uint8_t)(trail[i] * 7 / 8);
            age[i] = 0;
        }
    }
}

/** @brief Interpolação linear de cores. @param a Cor inicial. @param b Cor final. @param t Fração em [0,1]. @param out Resultado. */
static void lerp(const uint8_t a[3], const uint8_t b[3], double t, uint8_t out[3]) {
    for (int k = 0; k < 3; k++) out[k] = (uint8_t)lround(a[k] + (b[k] - a[k]) * t);
}

/* Vivas: ciano (recém-nascida) -> amarelo -> laranja -> magenta (idade >= 256, escala log2).
 * Mortas: fundo azul-escuro misturado com violeta proporcional ao rastro. */
void palette_color_p(int pal, uint16_t age, uint8_t trail, uint8_t rgb[3]) {
    static const uint8_t stops[PALETTE_COUNT][4][3] = {
        { { 230, 230, 230 }, { 230, 230, 230 }, { 230, 230, 230 }, { 230, 230, 230 } }, /* mono */
        { { 120, 255, 240 }, { 255, 220, 80 }, { 255, 110, 60 }, { 200, 60, 200 } },    /* idade */
        { { 255, 255, 190 }, { 255, 200, 60 }, { 230, 80, 20 }, { 120, 20, 20 } },      /* fogo */
        { { 210, 255, 255 }, { 60, 200, 255 }, { 40, 100, 220 }, { 40, 40, 130 } },     /* oceano */
    };
    static const double pos[4] = { 0.0, 0.4, 0.7, 1.0 };
    static const uint8_t bg[3] = { 12, 12, 28 }, ghost[3] = { 90, 40, 120 };
    if (pal < 0 || pal >= PALETTE_COUNT) pal = PALETTE_AGE;
    if (age == 0) {
        if (pal == PALETTE_MONO) { rgb[0] = rgb[1] = rgb[2] = 20; return; }
        lerp(bg, ghost, trail / 255.0, rgb);
        return;
    }
    double t = log2((double)age) / 8.0;
    if (t > 1.0) t = 1.0;
    int k = 0;
    while (k < 2 && t > pos[k + 1]) k++;
    lerp(stops[pal][k], stops[pal][k + 1], (t - pos[k]) / (pos[k + 1] - pos[k]), rgb);
}

void palette_color(uint16_t age, uint8_t trail, uint8_t rgb[3]) { palette_color_p(PALETTE_AGE, age, trail, rgb); }

int palette_parse(const char *s, int *pal) {
    static const char *names[PALETTE_COUNT] = { "mono", "idade", "fogo", "oceano" };
    for (int i = 0; i < PALETTE_COUNT; i++)
        if (s && strcmp(s, names[i]) == 0) { *pal = i; return 0; }
    return -1;
}

const char *palette_name(int pal) {
    static const char *names[PALETTE_COUNT] = { "MONO", "IDADE", "FOGO", "OCEANO" };
    return pal >= 0 && pal < PALETTE_COUNT ? names[pal] : "?";
}

/* Bits do caractere braille (U+2800 + bits) para o bloco 2x4 com canto em (x0,y0).
 * Pontos: coluna 0 = 1,2,3,7; coluna 1 = 4,5,6,8. Fora da grade conta como vazio. */
unsigned braille_bits(const World *w, int x0, int y0) {
    static const unsigned dot[4][2] = { { 0x01, 0x08 }, { 0x02, 0x10 }, { 0x04, 0x20 }, { 0x40, 0x80 } };
    unsigned b = 0;
    for (int dy = 0; dy < 4; dy++)
        for (int dx = 0; dx < 2; dx++) {
            int x = x0 + dx, y = y0 + dy;
            if (x < w->w && y < w->h && world_get(w, x, y) == 1) b |= dot[dy][dx];
        }
    return b;
}

void braille_render(FILE *f, const World *w, const uint16_t *age) { braille_render_p(f, w, age, PALETTE_AGE); }

/* Escreve o mundo em braille. Com `age`, cada caractere usa cor ANSI 24 bits da célula mais velha do bloco. */
void braille_render_p(FILE *f, const World *w, const uint16_t *age, int pal) {
    for (int y0 = 0; y0 < w->h; y0 += 4) {
        for (int x0 = 0; x0 < w->w; x0 += 2) {
            unsigned b = braille_bits(w, x0, y0);
            if (!b) { fputc(' ', f); continue; }
            if (age) {
                uint16_t oldest = 0;
                for (int dy = 0; dy < 4 && y0 + dy < w->h; dy++)
                    for (int dx = 0; dx < 2 && x0 + dx < w->w; dx++) {
                        uint16_t a = age[(size_t)(y0 + dy) * (size_t)w->w + (size_t)(x0 + dx)];
                        if (a > oldest) oldest = a;
                    }
                uint8_t c[3];
                palette_color_p(pal, oldest, 0, c);
                fprintf(f, "\x1b[38;2;%u;%u;%um", c[0], c[1], c[2]);
            }
            fputc(0xE2, f);
            fputc((int)(0xA0 | (b >> 6)), f);
            fputc((int)(0x80 | (b & 0x3F)), f);
        }
        if (age) fputs("\x1b[0m\x1b[K", f);
        fputc('\n', f);
    }
}

/* =====================================================================================
 * Canvas, fonte bitmap 3x5, câmera, minimapa e legenda (usados pelo HUD do mcview)
 * ===================================================================================== */

/** @brief Glifo 3x5: 5 linhas de 3 colunas, '#' = pixel aceso. */
typedef struct {
    char c;        /**< Caractere ASCII (maiúsculo). */
    const char *g; /**< 15 caracteres: linha 0 (topo) a 4, 3 colunas cada. */
} Glyph;

static const Glyph FONT[] = { /* uma string por linha do glifo, de cima para baixo */
    { '0', "###" "#.#" "#.#" "#.#" "###" },
    { '1', ".#." "##." ".#." ".#." "###" },
    { '2', "###" "..#" "###" "#.." "###" },
    { '3', "###" "..#" ".##" "..#" "###" },
    { '4', "#.#" "#.#" "###" "..#" "..#" },
    { '5', "###" "#.." "###" "..#" "###" },
    { '6', "###" "#.." "###" "#.#" "###" },
    { '7', "###" "..#" "..#" ".#." ".#." },
    { '8', "###" "#.#" "###" "#.#" "###" },
    { '9', "###" "#.#" "###" "..#" "###" },
    { 'A', ".#." "#.#" "###" "#.#" "#.#" },
    { 'B', "##." "#.#" "##." "#.#" "##." },
    { 'C', ".##" "#.." "#.." "#.." ".##" },
    { 'D', "##." "#.#" "#.#" "#.#" "##." },
    { 'E', "###" "#.." "##." "#.." "###" },
    { 'F', "###" "#.." "##." "#.." "#.." },
    { 'G', ".##" "#.." "#.#" "#.#" ".##" },
    { 'H', "#.#" "#.#" "###" "#.#" "#.#" },
    { 'I', "###" ".#." ".#." ".#." "###" },
    { 'J', "..#" "..#" "..#" "#.#" ".#." },
    { 'K', "#.#" "#.#" "##." "#.#" "#.#" },
    { 'L', "#.." "#.." "#.." "#.." "###" },
    { 'M', "#.#" "###" "###" "#.#" "#.#" },
    { 'N', "##." "#.#" "#.#" "#.#" "#.#" },
    { 'O', ".#." "#.#" "#.#" "#.#" ".#." },
    { 'P', "##." "#.#" "##." "#.." "#.." },
    { 'Q', ".#." "#.#" "#.#" "##." ".##" },
    { 'R', "##." "#.#" "##." "#.#" "#.#" },
    { 'S', ".##" "#.." ".#." "..#" "##." },
    { 'T', "###" ".#." ".#." ".#." ".#." },
    { 'U', "#.#" "#.#" "#.#" "#.#" "###" },
    { 'V', "#.#" "#.#" "#.#" "#.#" ".#." },
    { 'W', "#.#" "#.#" "###" "###" "#.#" },
    { 'X', "#.#" "#.#" ".#." "#.#" "#.#" },
    { 'Y', "#.#" "#.#" ".#." ".#." ".#." },
    { 'Z', "###" "..#" ".#." "#.." "###" },
    { ' ', "..." "..." "..." "..." "..." },
    { ':', "..." ".#." "..." ".#." "..." },
    { '/', "..#" "..#" ".#." "#.." "#.." },
    { '.', "..." "..." "..." "..." ".#." },
    { ',', "..." "..." "..." ".#." "#.." },
    { '-', "..." "..." "###" "..." "..." },
    { '+', "..." ".#." "###" ".#." "..." },
    { '=', "..." "###" "..." "###" "..." },
    { '(', ".#." "#.." "#.." "#.." ".#." },
    { ')', ".#." "..#" "..#" "..#" ".#." },
    { '[', "##." "#.." "#.." "#.." "##." },
    { ']', ".##" "..#" "..#" "..#" ".##" },
    { '<', "..#" ".#." "#.." ".#." "..#" },
    { '>', "#.." ".#." "..#" ".#." "#.." },
    { '?', "###" "..#" ".#." "..." ".#." },
    { '_', "..." "..." "..." "..." "###" },
    { '|', ".#." ".#." ".#." ".#." ".#." },
    { '%', "#.#" "..#" ".#." "#.." "#.#" },
    { '*', "..." "#.#" ".#." "#.#" "..." },
    { '^', ".#." "#.#" "..." "..." "..." },
    { '!', ".#." ".#." ".#." "..." ".#." },
};

int font_glyph(char c, uint16_t *bits) {
    char u = (char)toupper((unsigned char)c);
    int found = 1;
    const Glyph *g = NULL;
    for (size_t i = 0; i < sizeof FONT / sizeof *FONT; i++) if (FONT[i].c == u) g = &FONT[i];
    if (!g) { found = 0; for (size_t i = 0; i < sizeof FONT / sizeof *FONT; i++) if (FONT[i].c == '?') g = &FONT[i]; }
    uint16_t b = 0;
    for (int k = 0; k < 15; k++) if (g->g[k] == '#') b |= (uint16_t)(1u << k);
    *bits = b;
    return found;
}

int ui_auto_scale(int cw, int ch) {
    int a = cw / 480, b = ch / 320, s = a < b ? a : b;
    if (s < 1) s = 1;
    if (s > UI_SCALE_MAX) s = UI_SCALE_MAX;
    return s;
}

int text_width(const char *s, int scale) {
    int n = (int)strlen(s);
    return n ? (4 * n - 1) * scale : 0;
}

int canvas_save_ppm(const Canvas *cv, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", cv->w, cv->h);
    for (size_t i = 0; i < (size_t)cv->w * (size_t)cv->h; i++) {
        unsigned char c[3] = { (unsigned char)(cv->px[i] >> 16), (unsigned char)(cv->px[i] >> 8), (unsigned char)cv->px[i] };
        fwrite(c, 1, 3, f);
    }
    return fclose(f) ? -1 : 0;
}

/** @brief Mistura `argb` (alfa no byte alto) sobre o pixel destino. */
static void blend(uint32_t *dst, uint32_t argb) {
    uint32_t a = argb >> 24;
    if (a == 255) { *dst = argb; return; }
    uint32_t d = *dst, out = 0xFF000000u;
    for (int sh = 0; sh <= 16; sh += 8) {
        uint32_t s = (argb >> sh) & 255, t = (d >> sh) & 255;
        out |= ((s * a + t * (255 - a)) / 255) << sh;
    }
    *dst = out;
}

void canvas_fill_rect(Canvas *cv, int x, int y, int w, int h, uint32_t argb) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > cv->w ? cv->w : x + w, y1 = y + h > cv->h ? cv->h : y + h;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) blend(&cv->px[(size_t)j * (size_t)cv->w + (size_t)i], argb);
}

void canvas_text(Canvas *cv, int x, int y, const char *s, uint32_t argb, int scale) {
    for (; *s; s++, x += 4 * scale) {
        uint16_t b;
        font_glyph(*s, &b);
        for (int k = 0; k < 15; k++)
            if (b & (1u << k)) canvas_fill_rect(cv, x + (k % 3) * scale, y + (k / 3) * scale, scale, scale, argb);
    }
}

int view_cell(const View *v, const World *w, int px, int py, int *cx, int *cy) {
    int x = (int)floor(v->x + px / v->zoom), y = (int)floor(v->y + py / v->zoom);
    if (w->bound == BOUND_TORUS) {
        x = ((x % w->w) + w->w) % w->w;
        y = ((y % w->h) + w->h) % w->h;
    } else if (x < 0 || y < 0 || x >= w->w || y >= w->h) return 0;
    *cx = x;
    *cy = y;
    return 1;
}

void view_zoom_at(View *v, int px, int py, double factor) {
    double z = v->zoom * factor;
    if (z < VIEW_ZOOM_MIN) z = VIEW_ZOOM_MIN;
    if (z > VIEW_ZOOM_MAX) z = VIEW_ZOOM_MAX;
    double wx = v->x + px / v->zoom, wy = v->y + py / v->zoom; /* ponto do mundo sob o cursor fica fixo */
    v->zoom = z;
    v->x = wx - px / z;
    v->y = wy - py / z;
}

void view_fit(View *v, const World *w, int cw, int ch) {
    double zx = (double)cw / w->w, zy = (double)ch / w->h;
    v->zoom = zx < zy ? zx : zy;
    if (v->zoom < VIEW_ZOOM_MIN) v->zoom = VIEW_ZOOM_MIN;
    if (v->zoom > VIEW_ZOOM_MAX) v->zoom = VIEW_ZOOM_MAX;
    v->x = (w->w - cw / v->zoom) / 2.0;
    v->y = (w->h - ch / v->zoom) / 2.0;
}

/** @brief Converte RGB em ARGB opaco. */
static uint32_t argb_of(const uint8_t c[3]) { return 0xFF000000u | ((uint32_t)c[0] << 16) | ((uint32_t)c[1] << 8) | c[2]; }

void render_world(Canvas *cv, const World *w, const uint16_t *age, const uint8_t *trail, const View *v, int color, int grid) {
    int lines = grid && v->zoom >= 6.0;
    for (int py = 0; py < cv->h; py++)
        for (int px = 0; px < cv->w; px++) {
            uint32_t *d = &cv->px[(size_t)py * (size_t)cv->w + (size_t)px];
            int x, y;
            if (!view_cell(v, w, px, py, &x, &y)) { *d = 0xFF060608u; continue; } /* fora do mundo */
            size_t i = (size_t)y * (size_t)w->w + (size_t)x;
            uint8_t c[3];
            if (w->c[i] >= 2) { /* Generations: morrendo — azul que escurece a cada estado */
                int k = w->c[i] - 1;
                c[0] = (uint8_t)(70 / k); c[1] = (uint8_t)(110 / k); c[2] = (uint8_t)(255 / k);
            } else if (color && age) palette_color_p(color, age[i], trail ? trail[i] : 0, c);
            else c[0] = c[1] = c[2] = w->c[i] ? 230 : 20;
            *d = argb_of(c);
            if (lines) {
                double fx = v->x + px / v->zoom, fy = v->y + py / v->zoom;
                if (fx - floor(fx) < 1.0 / v->zoom || fy - floor(fy) < 1.0 / v->zoom) blend(d, 0x50000000u);
            }
        }
}

void render_minimap(Canvas *cv, const World *w, const View *v, int x, int y, int size) {
    int mw = size, mh = size;
    if (w->w >= w->h) mh = size * w->h / w->w; else mw = size * w->w / w->h;
    if (mw < 1) mw = 1;
    if (mh < 1) mh = 1;
    canvas_fill_rect(cv, x - 2, y - 2, mw + 4, mh + 4, 0xFF8080A0u);
    for (int j = 0; j < mh; j++)
        for (int i = 0; i < mw; i++) {
            int cx = i * w->w / mw, cy = j * w->h / mh;
            canvas_fill_rect(cv, x + i, y + j, 1, 1, world_get(w, cx, cy) == 1 ? 0xFFE6E6E6u : 0xFF141428u);
        }
    /* retângulo da área visível (no toro, a origem da câmera é reduzida ao mundo) */
    double vx = v->x, vy = v->y;
    if (w->bound == BOUND_TORUS) { vx = fmod(fmod(vx, w->w) + w->w, w->w); vy = fmod(fmod(vy, w->h) + w->h, w->h); }
    int rx = x + (int)(vx * mw / w->w), ry = y + (int)(vy * mh / w->h);
    int rw = (int)(cv->w / v->zoom * mw / w->w), rh = (int)(cv->h / v->zoom * mh / w->h);
    if (cv->w / v->zoom >= w->w) { rx = x; rw = mw; } /* a tela mostra o mundo inteiro na horizontal */
    if (cv->h / v->zoom >= w->h) { ry = y; rh = mh; }
    if (rw < 2) rw = 2;
    if (rh < 2) rh = 2;
    Canvas mini = *cv; /* recorte simples: limita o retângulo à caixa do minimapa */
    int x1 = rx + rw > x + mw ? x + mw : rx + rw, y1 = ry + rh > y + mh ? y + mh : ry + rh;
    int x0 = rx < x ? x : rx, y0 = ry < y ? y : ry;
    if (x1 > x0 && y1 > y0) {
        canvas_fill_rect(&mini, x0, y0, x1 - x0, 1, 0xFFFFD050u);
        canvas_fill_rect(&mini, x0, y1 - 1, x1 - x0, 1, 0xFFFFD050u);
        canvas_fill_rect(&mini, x0, y0, 1, y1 - y0, 0xFFFFD050u);
        canvas_fill_rect(&mini, x1 - 1, y0, 1, y1 - y0, 0xFFFFD050u);
    }
}

void render_palette_legend(Canvas *cv, int x, int y, int width, int scale, int pal) {
    int bh = 4 * scale;
    for (int i = 0; i < width; i++) {
        uint16_t age = (uint16_t)lround(pow(2.0, 8.0 * i / (width > 1 ? width - 1 : 1))); /* idade 1..256, escala log2 */
        uint8_t c[3];
        palette_color_p(pal, age, 0, c);
        canvas_fill_rect(cv, x + i, y, 1, bh, argb_of(c));
    }
    int ty = y + bh + 2 * scale;
    canvas_text(cv, x, ty, "IDADE 1", 0xFFDDDDDDu, scale);
    canvas_text(cv, x + width / 2 - text_width("16", scale) / 2, ty, "16", 0xFFDDDDDDu, scale);
    canvas_text(cv, x + width - text_width("256+", scale), ty, "256+", 0xFFDDDDDDu, scale);
    uint8_t g[3];
    palette_color_p(pal, 0, 255, g);
    int gx = x + width + 6 * scale;
    canvas_fill_rect(cv, gx, y, bh, bh, argb_of(g));
    canvas_text(cv, gx + bh + 2 * scale, y, "RASTRO", 0xFFDDDDDDu, scale);
}
