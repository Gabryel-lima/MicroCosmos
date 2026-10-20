CC      ?= gcc
CFLAGS  ?= -std=c11 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -Wpedantic -Wshadow -Werror
SAN     := -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
CORE    := src/rules.c src/world.c src/sim.c src/metrics.c src/search.c src/export.c src/render.c src/volume.c src/render3d.c src/probe.c src/search3d.c
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)
PREFIX  ?= $(HOME)/.local
B       := ./bin/microcosmos
V       := ./bin/mcview
V3      := ./bin/mcview3d
EX      := runs/exemplo

.DEFAULT_GOAL := help

##@ Ajuda
help: ## Mostra esta ajuda (alvo padrão)
	@printf "\n\033[1;36mMicrocosmos\033[0m — autômatos celulares, busca evolutiva e visualização\n"
	@printf "uso: \033[1mmake \033[33m<alvo>\033[0m   (variáveis: \033[35mPREFIX\033[0m=$(PREFIX) \033[35mCC\033[0m=$(CC) \033[35mRANK\033[0m=0)\n"
	@awk 'BEGIN {FS = ":.*## "} \
	  /^##@/ { printf "\n\033[1;34m%s\033[0m\n", substr($$0, 5) } \
	  /^[a-zA-Z0-9_-]+:.*## / { printf "  \033[33m%-15s\033[0m %s\n", $$1, $$2 }' $(MAKEFILE_LIST)
	@printf "\n\033[1;34mComandos diretos (após make build)\033[0m\n"
	@printf "  \033[32m%s\033[0m\n" \
	  "$(B) run --rule B3/S23[/C<n>] [--braille] [--ppm f.ppm]   (/C<n> = Generations, ex.: B2/S/C3)" \
	  "$(B) tui --rule B36/S23 --fps 20 --steps 1000 [--mono] [--palette idade|fogo|oceano|mono]" \
	  "$(B) search --strategy random|evo|map --budget N --seed N --checkpoint-every N --out DIR" \
	  "$(B) search --resume DIR/checkpoint.bin [--budget N]" \
	  "$(B) search ... --save meus/ck/final.bin --checkpoint-dir meus/ck/hist   (caminhos próprios)" \
	  "$(B) elites --checkpoint DIR/checkpoint.bin" \
	  "$(B) tui --checkpoint DIR/checkpoint.bin --rank K" \
	  "$(B) compare --budget 200 --reps 5 --out DIR" \
	  "$(V3) [--rule B5-7/S4-6] [--nb moore|vn] [--d N] [--slice K] [--win LxA] [--ui N] [--palette P] [--snapshot f.ppm] [--help-panel]" \
	  "$(B) probe --rule B3/S23 --dist 20 --reps 10   (fase 5: intervenção com controle de cone de luz)" \
	  "$(B) search3d [--strategy map] [--budget N] [--nb moore|vn] [--d N] [--checkpoint-every N] [--save F] [--resume F]" \
	  "$(B) elites3d --checkpoint DIR/checkpoint3d.bin     |     $(V3) --checkpoint DIR/checkpoint3d.bin --rank K" \
	  "$(B) run3d --rule B5-7/S4-6 [--nb moore|vn|moore2d] [--d N] [--braille [--slice K]]   (3D; regra com faixas)" \
	  "$(V) --rule B3/S23[/C<n>] [--scale N] [--win 1280x800] [--ui N] [--palette P] [--snapshot f.ppm] [--help-panel]" \
	  "$(V) --checkpoint DIR/checkpoint.bin --rank K"
	@printf "  opções de simulação: --w --h --boundary torus|fixed|reflect --steps --density --frac --world-seed --no-perturb\n"
	@printf "  mcview: espaço pausa · N passo · R reinicia · P perturba · setas/WASD câmera (Shift rápido) · roda/Z/X zoom\n"
	@printf "          botão dir. arrasta · botão esq. alterna célula · F enquadra · 0 origem · +/- velocidade · C cores\n"
	@printf "          G grade · M minimapa · I HUD · [ ] elites · H ajuda na tela · Esc sai\n"
	@printf "  mcview3d: arrastar/setas giram · roda/Z/X zoom · WASD/QE movem o alvo · F enquadra · V fatia · PgUp/PgDn ou K/J camada\n"
	@printf "            espaço/N/R simulação · +/- velocidade · C paleta · I HUD · botão dir. edita na fatia · H ajuda · Esc\n"
	@printf "  ambos: F2/F3 diminuem/aumentam a interface, F4 volta à escala automática (--ui N fixa; 0 = auto)\n"
	@printf "\n\033[1;34mLegenda\033[0m  \033[1;34mazul\033[0m = seção  \033[33mamarelo\033[0m = alvo make  \033[35mmagenta\033[0m = variável  \033[32mverde\033[0m = comando direto\n\n"

##@ Build
build: bin/microcosmos bin/mcview bin/mcview3d ## Compila CLI e visualizadores SDL2 (2D e 3D)
all: build ## Sinônimo de build
cli: bin/microcosmos ## Compila só a CLI (sem SDL2)

bin/microcosmos: $(CORE) src/opts.c src/main.c src/mc.h src/opts.h | bin
	$(CC) $(CFLAGS) $(CORE) src/opts.c src/main.c -lm -o $@

bin/mcview: $(CORE) src/opts.c src/viewer.c src/mc.h src/opts.h | bin
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(CORE) src/opts.c src/viewer.c $(SDL_LIBS) -lm -o $@

bin/mcview3d: $(CORE) src/opts.c src/viewer3d.c src/mc.h src/opts.h | bin
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(CORE) src/opts.c src/viewer3d.c $(SDL_LIBS) -lm -o $@

bin/test: $(CORE) tests/test_main.c src/mc.h | bin
	$(CC) $(filter-out -O2,$(CFLAGS)) $(SAN) $(CORE) tests/test_main.c -lm -o $@

bin/test_cov: $(CORE) tests/test_main.c src/mc.h | bin
	$(CC) -std=c11 -D_DEFAULT_SOURCE -O0 --coverage $(CORE) tests/test_main.c -lm -o $@

bin:
	mkdir -p bin

clean: ## Remove binários e artefatos de cobertura (mantém runs/)
	rm -rf bin *.gcov *.gcda *.gcno

clean-runs: ## Remove resultados em runs/ (irreversível)
	rm -rf runs

##@ Instalação
deps: ## Mostra o comando para instalar dependências (Debian/Ubuntu)
	@printf "sudo apt install build-essential pkg-config libsdl2-dev\n"
install: build ## Instala microcosmos e mcview em PREFIX/bin
	install -d $(PREFIX)/bin
	install -m 755 bin/microcosmos bin/mcview bin/mcview3d $(PREFIX)/bin/
uninstall: ## Remove os binários de PREFIX/bin
	rm -f $(PREFIX)/bin/microcosmos $(PREFIX)/bin/mcview $(PREFIX)/bin/mcview3d

##@ Verificação
test: bin/test ## Testes com ASan + UBSan
	./bin/test
mutate: ## Mutation testing (tests/mutate.sh)
	./tests/mutate.sh
coverage: bin/test_cov ## Cobertura de linhas/branches (gcov)
	rm -f bin/*.gcda && ./bin/test_cov && cd bin && gcov -b test_cov-*.gcda | grep -A3 "File 'src" ; rm -f bin/*.gcov
smoke: bin/microcosmos ## Busca curta e previsível (runs/smoke)
	$(B) smoke
check: build test mutate smoke ## build + test + mutate + smoke

##@ Exemplos
ex-life: bin/microcosmos ## Jogo da Vida: métricas + estado final em braille
	$(B) run --rule B3/S23 --world-seed 5 --braille
ex-tui: bin/microcosmos ## Animação braille colorida no terminal (Ctrl+C sai)
	$(B) tui --rule B36/S23 --fps 20 --steps 600
ex-view: bin/mcview ## Janela SDL2 com o Jogo da Vida (HUD, minimapa; H = ajuda)
	$(V) --rule B3/S23
ex-snapshot: bin/mcview ## Sem janela: salva o quadro do mcview com HUD em runs/snapshot.ppm
	mkdir -p runs && $(V) --rule B3/S23 --steps 120 --snapshot runs/snapshot.ppm
ex-view-hidpi: bin/mcview ## Interface grande (--ui 4) para telas de alta resolução; F2/F3 ajustam
	$(V) --rule B3/S23 --ui 4 --win 1600x1000
ex-view-big: bin/mcview ## Mundo 256x256 em janela 1280x800 para explorar com câmera/zoom
	$(V) --rule B3/S23 --w 256 --h 256 --frac 0.8 --steps 5000 --win 1280x800
ex-search: bin/microcosmos ## Busca MAP-Elites com checkpoints em runs/exemplo
	$(B) search --strategy map --budget 400 --checkpoint-every 50 --out $(EX)
ex-resume: bin/microcosmos ## Retoma runs/exemplo e estende o orçamento para 800
	$(B) search --resume $(EX)/checkpoint.bin --budget 800 --out $(EX)
ex-save: bin/microcosmos ## Busca salvando checkpoints em caminhos próprios (runs/salvos)
	$(B) search --strategy evo --budget 200 --checkpoint-every 100 --checkpoint-dir runs/salvos/hist --save runs/salvos/final.bin --out runs/salvos/out
ex-elites: bin/microcosmos ## Lista as elites do checkpoint de runs/exemplo
	$(B) elites --checkpoint $(EX)/checkpoint.bin
ex-replay: bin/microcosmos ## Reproduz uma elite em braille (RANK=0 é a melhor)
	$(B) tui --checkpoint $(EX)/checkpoint.bin --rank $(or $(RANK),0)
ex-replay-view: bin/mcview ## Reproduz as elites em janela ([ e ] trocam)
	$(V) --checkpoint $(EX)/checkpoint.bin --rank $(or $(RANK),0)
ex-3d: bin/microcosmos ## Motor 3D: 32x32x32, Moore 26 vizinhos, projeção braille
	$(B) run3d --rule B5-7/S4-6 --steps 60 --density 0.3 --braille --slice 16
ex-3d-view: bin/mcview3d ## Janela 3D (câmera orbital, fatias, gizmo de eixos; H = ajuda)
	$(V3)
ex-3d-snapshot: bin/mcview3d ## Sem janela: salva o quadro 3D em runs/snapshot3d.ppm
	mkdir -p runs && $(V3) --steps 20 --snapshot runs/snapshot3d.ppm
ex-search3d: bin/microcosmos ## Busca 3D MAP-Elites (16³) com checkpoints em runs/exemplo3d
	$(B) search3d --strategy map --budget 150 --checkpoint-every 50 --out runs/exemplo3d
ex-elites3d: bin/microcosmos ## Lista as elites da busca 3D de runs/exemplo3d
	$(B) elites3d --checkpoint runs/exemplo3d/checkpoint3d.bin
ex-replay3d: bin/mcview3d ## Abre as elites 3D em janela ([ e ] trocam; RANK=0 é a melhor)
	$(V3) --checkpoint runs/exemplo3d/checkpoint3d.bin --rank $(or $(RANK),0)
ex-probe: bin/microcosmos ## Fase 5: sinal na fonte, efeito no alvo a 20 células, 10 sementes
	$(B) probe --rule B3/S23 --dist 20 --reps 10 --steps 200
ex-brain: bin/microcosmos ## Generations: Brian's Brain (B2/S/C3) em braille, paleta fogo
	$(B) tui --rule B2/S/C3 --steps 300 --fps 20 --palette fogo
ex-brain-view: bin/mcview ## Generations em janela (azul = morrendo; C troca a paleta)
	$(V) --rule B2/S/C3 --steps 2000 --palette oceano
ex-compare: bin/microcosmos ## Compara random/evo/map com orçamento igual
	$(B) compare --budget 200 --reps 5 --out runs/compare

.PHONY: help build all cli clean clean-runs deps install uninstall test mutate coverage smoke check \
        ex-life ex-tui ex-view ex-snapshot ex-view-hidpi ex-view-big ex-search ex-save ex-resume ex-elites ex-replay ex-replay-view ex-3d ex-3d-view ex-3d-snapshot ex-search3d ex-elites3d ex-replay3d ex-probe ex-brain ex-brain-view ex-compare
