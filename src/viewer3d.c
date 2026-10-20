/**
 * @file viewer3d.c
 * @brief Visualizador 3D interativo (SDL2): câmera orbital, HUD, gizmo de eixos, modo fatia e ajuda na tela.
 *
 * Todo o desenho é feito por render3d.c/render.c num Canvas; SDL só cuida de janela e eventos.
 * `--snapshot` gera a imagem sem abrir janela.
 */
#include "mc.h"
#include "opts.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** @brief Linhas do painel de ajuda (fonte 3x5: só ASCII). */
static const char *HELP3[] = {
    "CONTROLES 3D",
    "BOTAO ESQUERDO + ARRASTAR   GIRA A CAMERA",
    "SETAS                       GIRA A CAMERA",
    "RODA / Z X                  APROXIMA / AFASTA",
    "WASD                        MOVE O ALVO NO PLANO DA TELA",
    "Q E                         MOVE O ALVO PARA CIMA / BAIXO",
    "F                           ENQUADRA O VOLUME",
    "V                           MODO FATIA LIGA / DESLIGA",
    "PGUP PGDN  OU  K J          MUDA A FATIA Z",
    "BOTAO DIREITO (NA FATIA)    ALTERNA CELULA",
    "ESPACO  N  R                PAUSA, 1 PASSO, REINICIA",
    "+ -                         VELOCIDADE",
    "C  I                        PALETA (MONO/IDADE/FOGO/OCEANO), HUD",
    "F2 F3  /  F4                DIMINUI / AUMENTA A INTERFACE  /  AUTO",
    "[ ]                         ELITE ANTERIOR / PROXIMA (--CHECKPOINT)",
    "H                           ESTA AJUDA    ESC  SAIR",
};

/** @brief Estado do visualizador 3D. */
typedef struct {
    Rule3 rule;                /**< Regra. */
    char rs[RULE3_STR_MAX];    /**< Regra em texto. */
    Neighborhood nb;           /**< Vizinhança. */
    NeighborhoodKind nbk;      /**< Tipo de vizinhança. */
    SimConfig cfg;             /**< w, h, borda, steps, densidade e fração da sopa. */
    int d;                     /**< Profundidade. */
    uint64_t wseed;            /**< Semente da sopa. */
    Volume a, b;               /**< Estado atual e buffer. */
    uint16_t *age;             /**< Idade por célula. */
    uint8_t *trail;            /**< Rastro (exigido por age_update_cells). */
    long t;                    /**< Passo atual. */
    Camera3 cam;               /**< Câmera orbital. */
    int paused, delay;         /**< Pausa (1) e ms entre passos. */
    int slice;                 /**< -1 = volume; k = plano z=k. */
    int last_slice;            /**< Última fatia usada (para religar o modo). */
    int color, hud, help;      /**< Opções de exibição (color = paleta). */
    int ui;                    /**< Escala do HUD (1..UI_SCALE_MAX); 0 = automática pela janela. */
    int cw, ch;                /**< Tamanho atual da janela (para F2/F3 partirem da escala automática). */
    const char *ckpath;        /**< Checkpoint MC3D de onde vêm as elites; NULL se --rule. */
    long rank;                 /**< Elite atual (0 = melhor). */
    int nelites;               /**< Número de elites no checkpoint. */
} V3;

/** @brief Reinicia em t=0 com a sopa da semente. @param v Estado. */
static void reset3(V3 *v) {
    size_t N = (size_t)v->a.w * (size_t)v->a.h * (size_t)v->a.d;
    volume_soup(&v->a, v->wseed, v->cfg.density, v->cfg.soup_frac);
    memset(v->age, 0, N * sizeof *v->age);
    memset(v->trail, 0, N);
    age_update_cells(v->a.c, N, v->age, v->trail);
    v->t = 0;
}

/** @brief Avança um passo; pausa ao atingir steps. @param v Estado. */
static void step3(V3 *v) {
    volume_step(v->rule, &v->nb, &v->a, &v->b);
    Volume tmp = v->a;
    v->a = v->b;
    v->b = tmp;
    age_update_cells(v->a.c, (size_t)v->a.w * (size_t)v->a.h * (size_t)v->a.d, v->age, v->trail);
    if (++v->t == v->cfg.steps) v->paused = 1;
}

/** @brief Troca para outra elite do checkpoint (mesma configuração) e reinicia. @return 0 ok; -1 fora do intervalo. */
static int load_elite3(V3 *v, long nr) {
    char err[256];
    SearchConfig3 sc;
    if (!v->ckpath || nr < 0 || nr >= v->nelites) return -1;
    if (checkpoint3_elite(v->ckpath, (int)nr, &v->rule, &sc, NULL, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return -1; }
    v->rank = nr;
    rule3_format(v->rule, v->rs, sizeof v->rs);
    reset3(v);
    v->paused = 0;
    return 0;
}

/** @brief Desenha cena, HUD, gizmo e ajuda. @param v Estado. @param cv Canvas. */
static void draw3(const V3 *v, Canvas *cv) {
    render_volume(cv, &v->a, v->age, &v->cam, v->slice, v->color);
    const int S = v->ui ? v->ui : ui_auto_scale(cv->w, cv->h), LH = 7 * S, P = 4 * S;
    const uint32_t TXT = 0xFFE8E8F0u, DIM = 0xFFA0A0B8u, PANEL = 0xB0101018u;
    if (v->hud) {
        char l[5][256];
        const char *st = v->t >= v->cfg.steps && v->paused ? "FIM" : v->paused ? "PAUSADO" : "RODANDO";
        snprintf(l[0], sizeof l[0], "%.120s   VIZINHANCA %s (%d)", v->rs, neighborhood_name(v->nbk), v->nb.n);
        if (v->ckpath) snprintf(l[0] + strlen(l[0]), sizeof l[0] - strlen(l[0]), "   ELITE %ld/%d", v->rank, v->nelites - 1);
        snprintf(l[1], sizeof l[1], "T %ld/%d   VIVAS %d   %s", v->t, v->cfg.steps, volume_count(&v->a), st);
        snprintf(l[2], sizeof l[2], "VOLUME %dX%dX%d   BORDA %s   SEED %llu", v->a.w, v->a.h, v->a.d,
                 boundary_name(v->a.bound), (unsigned long long)v->wseed);
        snprintf(l[3], sizeof l[3], "CAMERA YAW %.0f PITCH %.0f DIST %.1f  VEL %dMS  PALETA %s  UI %d%s",
                 v->cam.yaw * 180 / 3.14159265, v->cam.pitch * 180 / 3.14159265, v->cam.dist, v->delay,
                 palette_name(v->color), S, v->ui ? "" : " AUTO");
        if (v->slice >= 0) snprintf(l[4], sizeof l[4], "MODO FATIA Z=%d/%d (PGUP/PGDN)", v->slice, v->a.d - 1);
        else snprintf(l[4], sizeof l[4], "MODO VOLUME (V = FATIA)");
        int w = 0;
        for (int i = 0; i < 5; i++) if (text_width(l[i], S) > w) w = text_width(l[i], S);
        canvas_fill_rect(cv, P, P, w + 2 * P, 5 * LH + 2 * P, PANEL);
        for (int i = 0; i < 5; i++) canvas_text(cv, 2 * P, 2 * P + i * LH, l[i], i ? DIM : TXT, S);
        const char *keys = "H AJUDA   F2/F3 TAMANHO DA UI";
        int ly = cv->h - 4 * LH - P, bw = text_width(keys, S);
        if (120 * S > bw) bw = 120 * S; /* barra (80) + amostra e "RASTRO" */
        canvas_fill_rect(cv, P, ly - P, bw + 2 * P, 4 * LH + P, PANEL);
        if (v->color) render_palette_legend(cv, 2 * P, ly, 80 * S, S, v->color);
        canvas_text(cv, 2 * P, ly + 3 * LH, keys, TXT, S);
        int g = 62 * S; /* caixa do gizmo cresce com a UI */
        if (g > cv->h / 3) g = cv->h / 3;
        canvas_fill_rect(cv, cv->w - g - P, cv->h - g - P, g, g, PANEL);
        canvas_text(cv, cv->w - g, cv->h - g, "EIXOS", DIM, S);
        render_axes_gizmo(cv, &v->cam, cv->w - g / 2 - P, cv->h - g / 2 - P + LH / 2, g / 3, S);
    }
    if (v->help) {
        int n = (int)(sizeof HELP3 / sizeof *HELP3), w = 0;
        for (int i = 0; i < n; i++) if (text_width(HELP3[i], S) > w) w = text_width(HELP3[i], S);
        int x = (cv->w - w) / 2, y = (cv->h - n * LH) / 2;
        canvas_fill_rect(cv, x - 2 * P, y - 2 * P, w + 4 * P, n * LH + 3 * P, 0xE0080810u);
        for (int i = 0; i < n; i++) canvas_text(cv, x, y + i * LH, HELP3[i], i ? TXT : 0xFFFFD050u, S);
    }
}

/** @brief Move o alvo da câmera no plano da tela (dx, dy em células) e ao longo do "para cima" da cena (dz). */
static void move_target(V3 *v, double dx, double dy, double dup) {
    double e[3], r[3], u[3], f[3];
    cam3_basis(&v->cam, e, r, u, f);
    for (int i = 0; i < 3; i++) v->cam.target[i] += dx * r[i] + dy * u[i];
    v->cam.target[1] += dup;
}

/** @brief Trata uma tecla. @return 0 para sair; 1 continua. */
static int key3(V3 *v, SDL_Keycode k) {
    const double rot = 3.14159265 / 36, mv = v->cam.dist * 0.05;
    switch (k) {
    case SDLK_ESCAPE: return 0;
    case SDLK_SPACE: v->paused = !v->paused; break;
    case SDLK_n: step3(v); v->paused = 1; break;
    case SDLK_r: reset3(v); break;
    case SDLK_LEFT: v->cam.yaw -= rot; break;
    case SDLK_RIGHT: v->cam.yaw += rot; break;
    case SDLK_UP: v->cam.pitch += rot; break;
    case SDLK_DOWN: v->cam.pitch -= rot; break;
    case SDLK_z: v->cam.dist *= 0.85; break;
    case SDLK_x: v->cam.dist /= 0.85; break;
    case SDLK_a: move_target(v, -mv, 0, 0); break;
    case SDLK_d: move_target(v, mv, 0, 0); break;
    case SDLK_w: move_target(v, 0, mv, 0); break;
    case SDLK_s: move_target(v, 0, -mv, 0); break;
    case SDLK_q: move_target(v, 0, 0, mv); break;
    case SDLK_e: move_target(v, 0, 0, -mv); break;
    case SDLK_f: cam3_fit(&v->cam, &v->a); break;
    case SDLK_v: if (v->slice >= 0) { v->last_slice = v->slice; v->slice = -1; } else v->slice = v->last_slice; break;
    case SDLK_PAGEUP: case SDLK_k: if (v->slice >= 0 && v->slice < v->a.d - 1) v->slice++; break;
    case SDLK_PAGEDOWN: case SDLK_j: if (v->slice > 0) v->slice--; break;
    case SDLK_PLUS: case SDLK_EQUALS: case SDLK_KP_PLUS: if (v->delay > 0) v->delay -= 10; break;
    case SDLK_MINUS: case SDLK_KP_MINUS: if (v->delay < 1000) v->delay += 10; break;
    case SDLK_c: v->color = (v->color + 1) % PALETTE_COUNT; break;
    case SDLK_i: v->hud = !v->hud; break;
    case SDLK_h: case SDLK_F1: v->help = !v->help; break;
    case SDLK_F2: v->ui = (v->ui ? v->ui : ui_auto_scale(v->cw, v->ch)) - 1; if (v->ui < 1) v->ui = 1; break;
    case SDLK_F3: v->ui = (v->ui ? v->ui : ui_auto_scale(v->cw, v->ch)) + 1; if (v->ui > UI_SCALE_MAX) v->ui = UI_SCALE_MAX; break;
    case SDLK_F4: v->ui = 0; break;
    case SDLK_LEFTBRACKET: load_elite3(v, v->rank - 1); break;
    case SDLK_RIGHTBRACKET: load_elite3(v, v->rank + 1); break;
    default: break;
    }
    cam3_clamp(&v->cam);
    return 1;
}

int main(int argc, char **argv) {
    V3 v;
    memset(&v, 0, sizeof v);
    v.cfg = sim_default_config();
    v.cfg.w = v.cfg.h = 32;
    v.cfg.density = 0.35;
    v.d = 32;
    v.wseed = 1;
    v.delay = 80;
    v.slice = -1;
    v.color = v.hud = 1;
    v.nbk = NB_MOORE3D;
    const char *rule_s = "B5-7/S4-6", *snapshot = NULL;
    long win_w = 1000, win_h = 700, dd = 0, uiv = 0;
    for (int i = 1; i < argc; i++) {
        int r = parse_sim_opt(argc, argv, &i, &v.cfg, &v.wseed);
        if (r < 0) return 2;
        if (r) continue;
        if (!strcmp(argv[i], "--rule") && i + 1 < argc) rule_s = argv[++i];
        else if (!strcmp(argv[i], "--nb") && i + 1 < argc && neighborhood_parse(argv[i + 1], &v.nbk) == 0) i++;
        else if (!strcmp(argv[i], "--d") && i + 1 < argc && parse_int(argv[i + 1], 1, 1024, &dd) == 0) i++;
        else if (!strcmp(argv[i], "--snapshot") && i + 1 < argc) snapshot = argv[++i];
        else if (!strcmp(argv[i], "--help-panel")) v.help = 1;
        else if (!strcmp(argv[i], "--ui") && i + 1 < argc && parse_int(argv[i + 1], 0, UI_SCALE_MAX, &uiv) == 0) { v.ui = (int)uiv; i++; }
        else if (!strcmp(argv[i], "--checkpoint") && i + 1 < argc) v.ckpath = argv[++i];
        else if (!strcmp(argv[i], "--rank") && i + 1 < argc && parse_int(argv[i + 1], 0, 99, &v.rank) == 0) i++;
        else if (!strcmp(argv[i], "--palette") && i + 1 < argc && palette_parse(argv[i + 1], &v.color) == 0) i++;
        else if (!strcmp(argv[i], "--slice") && i + 1 < argc) { long k; if (parse_int(argv[i + 1], 0, 1023, &k)) goto usage; v.slice = (int)k; i++; }
        else if (!strcmp(argv[i], "--win") && i + 1 < argc && sscanf(argv[i + 1], "%ldx%ld", &win_w, &win_h) == 2 &&
                 win_w >= 320 && win_h >= 240 && win_w <= 8192 && win_h <= 8192) i++;
        else {
        usage:
            fprintf(stderr, "uso: mcview3d [--rule B5-7/S4-6] [--nb moore|vn|moore2d] [--d N] [--slice K] [--win LxA]\n"
                            "                [--snapshot ARQ.ppm] [--help-panel] [--palette idade|fogo|oceano|mono] [opções]\n"
                            "  (padrão 32x32x32, densidade 0.35)\n"
                            "  --ui N   escala da interface 1..8 (0 = automática pela janela; F2/F3/F4 na janela)\n"
                            "  --checkpoint ARQ.bin [--rank K]   abre elites de `microcosmos search3d` ([ ] trocam)\n"
                            "  H na janela mostra os controles\n" SIM_OPTS_HELP);
            return 2;
        }
    }
    if (dd) v.d = (int)dd;
    char err[256];
    if (v.ckpath) { /* configuração exata da busca 3D: sobrepõe as opções */
        SearchConfig3 sc;
        Rule3 er;
        if (checkpoint3_elite(v.ckpath, (int)v.rank, &er, &sc, &v.nelites, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return 1; }
        v.nbk = sc.nb;
        v.cfg.w = sc.w; v.cfg.h = sc.h; v.d = sc.d; v.cfg.bound = sc.bound; v.cfg.steps = sc.steps;
        v.cfg.density = sc.density; v.cfg.soup_frac = sc.soup_frac; v.wseed = sc.world_seed;
        rule_s = NULL;
        v.rule = er;
    }
    neighborhood_make(v.nbk, &v.nb);
    if (v.nbk != NB_MOORE2D && v.d < 3) { fprintf(stderr, "erro: vizinhança 3D exige --d >= 3\n"); return 2; }
    if (rule_s && rule3_parse(rule_s, v.nb.n, &v.rule, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return 2; }
    rule3_format(v.rule, v.rs, sizeof v.rs);
    if (sim_config_validate(&v.cfg, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return 2; }
    if (volume_init(&v.a, v.cfg.w, v.cfg.h, v.d, v.cfg.bound) || volume_init(&v.b, v.cfg.w, v.cfg.h, v.d, v.cfg.bound)) {
        fprintf(stderr, "erro: dimensões inválidas (lados 3..1024, total <= 2^26) ou sem memória\n");
        return 2;
    }
    if (v.slice >= v.d) v.slice = v.d - 1;
    v.last_slice = v.slice >= 0 ? v.slice : v.d / 2;
    size_t N = (size_t)v.cfg.w * (size_t)v.cfg.h * (size_t)v.d;
    v.age = calloc(N, sizeof *v.age);
    v.trail = calloc(N, 1);
    if (!v.age || !v.trail) return 1;
    reset3(&v);
    cam3_fit(&v.cam, &v.a);

    int cw = (int)win_w, ch = (int)win_h, rc = 0;
    v.cw = cw;
    v.ch = ch;
    Canvas cv = { NULL, 0, 0 };
    if (snapshot) { /* sem janela: roda até steps e salva o quadro */
        cv.px = malloc(sizeof(uint32_t) * (size_t)cw * (size_t)ch);
        cv.w = cw;
        cv.h = ch;
        if (!cv.px) return 1;
        while (v.t < v.cfg.steps) step3(&v);
        draw3(&v, &cv);
        rc = canvas_save_ppm(&cv, snapshot) ? 1 : 0;
        if (rc) fprintf(stderr, "erro: não foi possível escrever %s\n", snapshot);
        else printf("quadro 3D t=%ld salvo em %s (%dx%d)\n", v.t, snapshot, cw, ch);
    } else if (SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        rc = 1;
    } else {
        SDL_Window *win = SDL_CreateWindow("Microcosmos 3D", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, cw, ch, SDL_WINDOW_RESIZABLE);
        SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
        if (win && !ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
        SDL_Texture *tex = NULL;
        int running = ren != NULL, drag = 0;
        if (!ren) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); rc = 1; }
        Uint32 last = SDL_GetTicks();
        while (running) {
            if (!tex || cv.w != cw || cv.h != ch) {
                if (tex) SDL_DestroyTexture(tex);
                free(cv.px);
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, cw, ch);
                cv.px = malloc(sizeof(uint32_t) * (size_t)cw * (size_t)ch);
                cv.w = cw;
                cv.h = ch;
                if (!tex || !cv.px) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); rc = 1; break; }
            }
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) running = 0;
                else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) { cw = v.cw = e.window.data1; ch = v.ch = e.window.data2; }
                else if (e.type == SDL_KEYDOWN) running = key3(&v, e.key.keysym.sym);
                else if (e.type == SDL_MOUSEWHEEL) { v.cam.dist *= e.wheel.y > 0 ? 0.9 : 1 / 0.9; cam3_clamp(&v.cam); }
                else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) drag = 1;
                else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) drag = 0;
                else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT && v.slice >= 0) {
                    int gx, gy;
                    if (cam3_pick_slice(&v.cam, &v.a, cw, ch, e.button.x, e.button.y, v.slice, &gx, &gy)) {
                        size_t i = ((size_t)v.slice * (size_t)v.a.h + (size_t)gy) * (size_t)v.a.w + (size_t)gx;
                        v.a.c[i] ^= 1;
                        v.age[i] = v.a.c[i] ? 1 : 0;
                    }
                }
                else if (e.type == SDL_MOUSEMOTION && drag) {
                    v.cam.yaw -= e.motion.xrel * 0.008;
                    v.cam.pitch += e.motion.yrel * 0.008;
                    cam3_clamp(&v.cam);
                }
            }
            Uint32 now = SDL_GetTicks();
            if (!v.paused && now - last >= (Uint32)v.delay) { step3(&v); last = now; }
            draw3(&v, &cv);
            SDL_UpdateTexture(tex, NULL, cv.px, cw * (int)sizeof(uint32_t));
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, NULL, NULL);
            SDL_RenderPresent(ren);
            SDL_Delay(8);
        }
        if (tex) SDL_DestroyTexture(tex);
        if (ren) SDL_DestroyRenderer(ren);
        if (win) SDL_DestroyWindow(win);
        SDL_Quit();
    }
    free(cv.px);
    free(v.age);
    free(v.trail);
    volume_free(&v.a);
    volume_free(&v.b);
    return rc;
}
