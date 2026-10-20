/**
 * @file viewer.c
 * @brief Visualizador interativo SDL2 (opcional; o motor não depende dele).
 *
 * Desenha tudo num Canvas (render.c) e só usa SDL para janela, eventos e envio da imagem.
 * Controles: ver HELP_LINES abaixo (a tecla H mostra o painel de ajuda na própria janela).
 */
#include "mc.h"
#include "opts.h"

#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>

/** @brief Linhas do painel de ajuda (fonte 3x5: só ASCII, sem acentos). */
static const char *HELP_LINES[] = {
    "CONTROLES",
    "ESPACO  PAUSA / CONTINUA",
    "N       AVANCA 1 PASSO",
    "R       REINICIA (T=0)",
    "P       PERTURBA 3X3 CENTRAL",
    "SETAS / WASD   MOVE A CAMERA (SHIFT = RAPIDO)",
    "RODA / Z X     ZOOM NO CURSOR",
    "BOTAO DIREITO  ARRASTA A CAMERA",
    "BOTAO ESQUERDO ALTERNA CELULA",
    "F       ENQUADRA O MUNDO",
    "0       CENTRALIZA NA ORIGEM",
    "+ -     VELOCIDADE",
    "C       PALETA: MONO / IDADE / FOGO / OCEANO",
    "G       GRADE   M  MINIMAPA   I  HUD",
    "[ ]     ELITE ANTERIOR / PROXIMA",
    "F2 F3   DIMINUI / AUMENTA A INTERFACE   F4 AUTO",
    "H       ESTA AJUDA    ESC  SAIR",
};

/** @brief Estado do visualizador. */
typedef struct {
    SimConfig cfg;        /**< Configuração da simulação. */
    uint64_t wseed;       /**< Semente da condição inicial. */
    Rule rule;            /**< Regra atual. */
    char rs[RULE_STR_MAX + 8];/**< Regra em texto (com /C<n> se Generations). */
    int states;           /**< Estados (2 = binário; > 2 = Generations). */
    const char *ckpath;   /**< Checkpoint de onde vêm as elites; NULL se --rule. */
    long rank;            /**< Elite atual (0 = melhor). */
    int nelites;          /**< Número de elites no checkpoint. */
    World a, b;           /**< Estado atual e buffer do próximo passo. */
    uint16_t *age;        /**< Idade por célula. */
    uint8_t *trail;       /**< Rastro de morte por célula. */
    long t;               /**< Passo atual. */
    View view;            /**< Câmera. */
    int paused;           /**< 1 = pausado. */
    int delay;            /**< Milissegundos entre passos (0 = um por quadro). */
    int color, grid, minimap, hud, help; /**< Opções de exibição (1 = ligada; color = paleta). */
    int ui;               /**< Escala do HUD (1..UI_SCALE_MAX); 0 = automática pelo tamanho da janela. */
} Viewer;

/** @brief Volta a t=0 com a sopa da semente e zera idades/rastros. @param v Visualizador. */
static void reset_world(Viewer *v) {
    size_t N = (size_t)v->cfg.w * (size_t)v->cfg.h;
    world_soup(&v->a, v->wseed, v->cfg.density, v->cfg.soup_frac);
    memset(v->age, 0, N * sizeof *v->age);
    memset(v->trail, 0, N);
    age_update(&v->a, v->age, v->trail);
    v->t = 0;
}

/** @brief Avança um passo da simulação. @param v Visualizador. */
static void step(Viewer *v) {
    sim_step_gen(v->rule, v->states, &v->a, &v->b);
    World tmp = v->a;
    v->a = v->b;
    v->b = tmp;
    age_update(&v->a, v->age, v->trail);
    v->t++;
    if (v->t == v->cfg.steps) v->paused = 1; /* fim do rollout definido pelos parâmetros */
}

/**
 * @brief Troca para outra elite do checkpoint (mesma configuração) e reinicia.
 * @param v Visualizador. @param nr Novo rank. @return 0 ok; -1 fora do intervalo ou erro.
 */
static int load_elite(Viewer *v, long nr) {
    char err[256];
    SearchConfig sc;
    if (!v->ckpath || nr < 0 || nr >= v->nelites) return -1;
    if (checkpoint_elite(v->ckpath, (int)nr, &v->rule, &sc, NULL, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return -1; }
    v->rank = nr;
    v->states = 2; /* elites de checkpoint são binárias */
    rule_format_gen(v->rule, v->states, v->rs, sizeof v->rs);
    reset_world(v);
    v->paused = 0;
    return 0;
}

/**
 * @brief Desenha HUD (estado, câmera, cursor), legenda, eixos, minimapa e ajuda.
 * @param v Visualizador. @param cv Canvas da janela. @param mx,my Posição do mouse.
 */
static void draw_overlay(const Viewer *v, Canvas *cv, int mx, int my) {
    const int S = v->ui ? v->ui : ui_auto_scale(cv->w, cv->h), LH = 7 * S, P = 4 * S; /* escala, linha, margem */
    const uint32_t TXT = 0xFFE8E8F0u, DIM = 0xFFA0A0B8u, PANEL = 0xB0101018u;
    char l[5][200];
    int cx, cy, has = view_cell(&v->view, &v->a, mx, my, &cx, &cy);
    const char *st = v->t >= v->cfg.steps && v->paused ? "FIM" : v->paused ? "PAUSADO" : "RODANDO";
    snprintf(l[0], sizeof l[0], "%s", v->rs);
    if (v->ckpath) snprintf(l[0] + strlen(l[0]), sizeof l[0] - strlen(l[0]), "   ELITE %ld/%d", v->rank, v->nelites - 1);
    snprintf(l[1], sizeof l[1], "T %ld/%d   VIVAS %d   %s", v->t, v->cfg.steps, world_count(&v->a), st);
    snprintf(l[2], sizeof l[2], "GRADE %dX%d   BORDA %s   SEED %llu", v->cfg.w, v->cfg.h, boundary_name(v->cfg.bound),
             (unsigned long long)v->wseed);
    snprintf(l[3], sizeof l[3], "CAMERA %.1f,%.1f  ZOOM %.2f  VEL %dMS  PALETA %s  UI %d%s", v->view.x, v->view.y,
             v->view.zoom, v->delay, palette_name(v->color), S, v->ui ? "" : " AUTO");
    if (has) {
        uint8_t st8 = world_get(&v->a, cx, cy);
        snprintf(l[4], sizeof l[4], "CURSOR %d,%d  %s  IDADE %u", cx, cy, st8 == 1 ? "VIVA" : st8 ? "MORRENDO" : "MORTA",
                 v->age[(size_t)cy * (size_t)v->a.w + (size_t)cx]);
    } else snprintf(l[4], sizeof l[4], "CURSOR FORA DO MUNDO");
    if (v->hud) {
        int w = 0;
        for (int i = 0; i < 5; i++) if (text_width(l[i], S) > w) w = text_width(l[i], S);
        canvas_fill_rect(cv, P, P, w + 2 * P, 5 * LH + 2 * P, PANEL);
        for (int i = 0; i < 5; i++) canvas_text(cv, 2 * P, 2 * P + i * LH, l[i], i ? DIM : TXT, S);
        /* rodapé: legenda da paleta, eixos e atalhos */
        const char *axes = "EIXOS: +X >   +Y V   (ORIGEM 0,0 NO CANTO)", *keys = "H AJUDA   F2/F3 TAMANHO DA UI";
        int bw = text_width(axes, S), ly = cv->h - 6 * LH - P;
        if (text_width(keys, S) > bw) bw = text_width(keys, S);
        canvas_fill_rect(cv, P, ly - P, bw + 2 * P, 6 * LH + P, PANEL);
        if (v->color) render_palette_legend(cv, 2 * P, ly, 80 * S, S, v->color);
        else canvas_text(cv, 2 * P, ly, "MONOCROMATICO (C TROCA A PALETA)", DIM, S);
        canvas_text(cv, 2 * P, ly + 3 * LH, axes, DIM, S);
        canvas_text(cv, 2 * P, ly + 4 * LH, keys, TXT, S);
        if (v->states > 2) canvas_text(cv, 2 * P, ly + 5 * LH, "AZUL = MORRENDO", 0xFF6E8CFFu, S);
    }
    if (v->minimap) {
        int size = 70 * S;
        if (size > cv->w / 3) size = cv->w / 3; /* janela estreita: o minimapa não cobre a cena */
        if (size > cv->h / 3) size = cv->h / 3;
        int mx0 = cv->w - size - 2 * P;
        canvas_fill_rect(cv, mx0 - P, P, size + 2 * P, size + LH + 3 * P, PANEL);
        canvas_text(cv, mx0, 2 * P, "MINIMAPA", DIM, S);
        render_minimap(cv, &v->a, &v->view, mx0, 2 * P + LH, size);
    }
    if (v->help) {
        int n = (int)(sizeof HELP_LINES / sizeof *HELP_LINES), w = 0;
        for (int i = 0; i < n; i++) if (text_width(HELP_LINES[i], S) > w) w = text_width(HELP_LINES[i], S);
        int x = (cv->w - w) / 2, y = (cv->h - n * LH) / 2;
        canvas_fill_rect(cv, x - 2 * P, y - 2 * P, w + 4 * P, n * LH + 3 * P, 0xE0080810u);
        for (int i = 0; i < n; i++) canvas_text(cv, x, y + i * LH, HELP_LINES[i], i ? TXT : 0xFFFFD050u, S);
    }
}

/**
 * @brief Trata uma tecla.
 * @param v Visualizador. @param k Tecla SDL. @param shift 1 se Shift pressionado.
 * @param cw,ch Tamanho da janela (para enquadrar e zoom no centro). @return 0 para sair; 1 continua.
 */
static int on_key(Viewer *v, SDL_Keycode k, int shift, int cw, int ch) {
    double pan = (shift ? 160.0 : 40.0) / v->view.zoom; /* pixels de tela convertidos em células */
    switch (k) {
    case SDLK_ESCAPE: return 0;
    case SDLK_SPACE: v->paused = !v->paused; break;
    case SDLK_n: step(v); v->paused = 1; break;
    case SDLK_r: reset_world(v); break;
    case SDLK_p: world_perturb_center(&v->a); break;
    case SDLK_LEFT: case SDLK_a: v->view.x -= pan; break;
    case SDLK_RIGHT: case SDLK_d: v->view.x += pan; break;
    case SDLK_UP: case SDLK_w: v->view.y -= pan; break;
    case SDLK_DOWN: case SDLK_s: v->view.y += pan; break;
    case SDLK_z: view_zoom_at(&v->view, cw / 2, ch / 2, 1.25); break;
    case SDLK_x: view_zoom_at(&v->view, cw / 2, ch / 2, 0.8); break;
    case SDLK_f: view_fit(&v->view, &v->a, cw, ch); break;
    case SDLK_0: v->view.x = -cw / v->view.zoom / 2; v->view.y = -ch / v->view.zoom / 2; break;
    case SDLK_PLUS: case SDLK_EQUALS: case SDLK_KP_PLUS: if (v->delay > 0) v->delay -= 10; break;
    case SDLK_MINUS: case SDLK_KP_MINUS: if (v->delay < 500) v->delay += 10; break;
    case SDLK_c: v->color = (v->color + 1) % PALETTE_COUNT; break; /* mono -> idade -> fogo -> oceano */
    case SDLK_g: v->grid = !v->grid; break;
    case SDLK_m: v->minimap = !v->minimap; break;
    case SDLK_i: v->hud = !v->hud; break;
    case SDLK_h: case SDLK_F1: v->help = !v->help; break;
    case SDLK_F2: v->ui = (v->ui ? v->ui : ui_auto_scale(cw, ch)) - 1; if (v->ui < 1) v->ui = 1; break;
    case SDLK_F3: v->ui = (v->ui ? v->ui : ui_auto_scale(cw, ch)) + 1; if (v->ui > UI_SCALE_MAX) v->ui = UI_SCALE_MAX; break;
    case SDLK_F4: v->ui = 0; break; /* volta à escala automática */
    case SDLK_LEFTBRACKET: load_elite(v, v->rank - 1); break;
    case SDLK_RIGHTBRACKET: load_elite(v, v->rank + 1); break;
    default: break;
    }
    return 1;
}

int main(int argc, char **argv) {
    Viewer v;
    memset(&v, 0, sizeof v);
    v.cfg = sim_default_config();
    v.wseed = 1;
    v.delay = 30;
    v.color = v.grid = v.minimap = v.hud = 1;
    const char *rule_s = "B3/S23";
    long scale = 0, win_w = 1000, win_h = 700, uiv = 0;
    const char *snapshot = NULL;
    int show_help = 0;
    for (int i = 1; i < argc; i++) {
        int r = parse_sim_opt(argc, argv, &i, &v.cfg, &v.wseed);
        if (r < 0) return 2;
        if (r) continue;
        if (!strcmp(argv[i], "--rule") && i + 1 < argc) rule_s = argv[++i];
        else if (!strcmp(argv[i], "--checkpoint") && i + 1 < argc) v.ckpath = argv[++i];
        else if (!strcmp(argv[i], "--rank") && i + 1 < argc && parse_int(argv[i + 1], 0, 99, &v.rank) == 0) i++;
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc && parse_int(argv[i + 1], 1, 64, &scale) == 0) i++;
        else if (!strcmp(argv[i], "--snapshot") && i + 1 < argc) snapshot = argv[++i];
        else if (!strcmp(argv[i], "--ui") && i + 1 < argc && parse_int(argv[i + 1], 0, UI_SCALE_MAX, &uiv) == 0) { v.ui = (int)uiv; i++; }
        else if (!strcmp(argv[i], "--palette") && i + 1 < argc && palette_parse(argv[i + 1], &v.color) == 0) i++;
        else if (!strcmp(argv[i], "--help-panel")) show_help = 1;
        else if (!strcmp(argv[i], "--win") && i + 1 < argc && sscanf(argv[i + 1], "%ldx%ld", &win_w, &win_h) == 2 &&
                 win_w >= 320 && win_h >= 240 && win_w <= 8192 && win_h <= 8192) i++;
        else {
            fprintf(stderr, "uso: mcview [--rule B3/S23[/C<n>] | --checkpoint ARQ.bin [--rank K]] [--scale N] [--win LxA] [--palette P] [opções]\n"
                            "  /C<n>       Generations com n estados (ex.: B2/S/C3 = Brian's Brain)\n"
                            "  --palette P idade (padrão) | fogo | oceano | mono\n"
                            "  --ui N      escala da interface 1..8 (0 = automática pela janela; F2/F3/F4 na janela)\n"
                            "  --scale N   zoom inicial (pixels por célula); padrão: enquadra o mundo\n"
                            "  --win LxA   tamanho da janela (padrão 1000x700; redimensionável)\n"
                            "  --snapshot ARQ.ppm  sem janela: roda até --steps e salva o quadro com HUD\n"
                            "  --help-panel        abre (ou fotografa) com o painel de ajuda visível\n"
                            "  a simulação pausa ao atingir --steps (padrão 300); H mostra os controles\n" SIM_OPTS_HELP);
            return 2;
        }
    }
    char err[256];
    if (v.ckpath) { /* configuração exata da busca: sobrepõe as opções de simulação */
        SearchConfig sc;
        if (checkpoint_elite(v.ckpath, (int)v.rank, &v.rule, &sc, &v.nelites, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return 1; }
        v.cfg = sc.sim;
        v.wseed = sc.world_seed;
    } else if (rule_parse_gen(rule_s, &v.rule, &v.states, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return 2; }
    if (!v.states) v.states = 2;
    rule_format_gen(v.rule, v.states, v.rs, sizeof v.rs);
    if (sim_config_validate(&v.cfg, err, sizeof err)) { fprintf(stderr, "erro: %s\n", err); return 2; }
    size_t N = (size_t)v.cfg.w * (size_t)v.cfg.h;
    if (world_init(&v.a, v.cfg.w, v.cfg.h, v.cfg.bound) || world_init(&v.b, v.cfg.w, v.cfg.h, v.cfg.bound)) return 1;
    v.age = calloc(N, sizeof *v.age);
    v.trail = calloc(N, 1);
    if (!v.age || !v.trail) return 1;
    reset_world(&v);
    v.help = show_help;

    if (snapshot) { /* modo sem janela: mesmo desenho do mcview, sem SDL */
        int cw = (int)win_w, ch = (int)win_h, rc = 1;
        Canvas cv = { malloc(sizeof(uint32_t) * (size_t)cw * (size_t)ch), cw, ch };
        if (cv.px) {
            if (scale) { v.view.zoom = (double)scale; v.view.x = (v.cfg.w - cw / v.view.zoom) / 2; v.view.y = (v.cfg.h - ch / v.view.zoom) / 2; }
            else view_fit(&v.view, &v.a, cw, ch);
            while (v.t < v.cfg.steps) step(&v);
            render_world(&cv, &v.a, v.age, v.trail, &v.view, v.color, v.grid);
            draw_overlay(&v, &cv, cw / 2, ch / 2);
            rc = canvas_save_ppm(&cv, snapshot) ? 1 : 0;
            if (rc) fprintf(stderr, "erro: não foi possível escrever %s\n", snapshot);
            else printf("quadro t=%ld salvo em %s (%dx%d)\n", v.t, snapshot, cw, ch);
        }
        free(cv.px);
        free(v.age);
        free(v.trail);
        world_free(&v.a);
        world_free(&v.b);
        return rc;
    }

    if (SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    int cw = (int)win_w, ch = (int)win_h;
    SDL_Window *win = SDL_CreateWindow("Microcosmos", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, cw, ch, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (win && !ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE); /* sem aceleração disponível */
    SDL_Texture *tex = NULL;
    Canvas cv = { NULL, 0, 0 };
    if (scale) { v.view.zoom = (double)scale; v.view.x = (v.cfg.w - cw / v.view.zoom) / 2; v.view.y = (v.cfg.h - ch / v.view.zoom) / 2; }
    else view_fit(&v.view, &v.a, cw, ch);

    int running = ren != NULL, drag = 0, mx = 0, my = 0;
    if (!ren) fprintf(stderr, "SDL: %s\n", SDL_GetError());
    Uint32 last_step = SDL_GetTicks();
    while (running) {
        if (!tex || cv.w != cw || cv.h != ch) { /* (re)cria textura e canvas no tamanho da janela */
            if (tex) SDL_DestroyTexture(tex);
            free(cv.px);
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, cw, ch);
            cv.px = malloc(sizeof(uint32_t) * (size_t)cw * (size_t)ch);
            cv.w = cw;
            cv.h = ch;
            if (!tex || !cv.px) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); break; }
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) { cw = e.window.data1; ch = e.window.data2; }
            else if (e.type == SDL_KEYDOWN) running = on_key(&v, e.key.keysym.sym, (e.key.keysym.mod & KMOD_SHIFT) != 0, cw, ch);
            else if (e.type == SDL_MOUSEWHEEL) view_zoom_at(&v.view, mx, my, e.wheel.y > 0 ? 1.15 : 1 / 1.15);
            else if (e.type == SDL_MOUSEMOTION) {
                if (drag) { v.view.x -= e.motion.xrel / v.view.zoom; v.view.y -= e.motion.yrel / v.view.zoom; }
                mx = e.motion.x;
                my = e.motion.y;
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT) drag = 1;
            else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_RIGHT) drag = 0;
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                int x, y;
                if (view_cell(&v.view, &v.a, e.button.x, e.button.y, &x, &y)) *world_at(&v.a, x, y) ^= 1;
            }
        }
        Uint32 now = SDL_GetTicks();
        if (!v.paused && now - last_step >= (Uint32)v.delay) { step(&v); last_step = now; }
        render_world(&cv, &v.a, v.age, v.trail, &v.view, v.color, v.grid);
        draw_overlay(&v, &cv, mx, my);
        SDL_UpdateTexture(tex, NULL, cv.px, cw * (int)sizeof(uint32_t));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        char title[160];
        snprintf(title, sizeof title, "Microcosmos %s  t=%ld/%d", v.rs, v.t, v.cfg.steps);
        SDL_SetWindowTitle(win, title);
        SDL_Delay(8);
    }
    free(cv.px);
    if (tex) SDL_DestroyTexture(tex);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
    free(v.age);
    free(v.trail);
    world_free(&v.a);
    world_free(&v.b);
    return 0;
}
