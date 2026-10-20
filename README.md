# Microcosmos

Laboratório de autômatos celulares 2D e 3D: busca de regras (aleatória, evolutiva, MAP-Elites), métricas reproduzíveis,
experimento de intervenção com controle e visualização (terminal em braille e janelas SDL2 2D/3D).
Nada aqui demonstra "vida", "comunicação" ou função — são indicadores medidos sob condições declaradas.

`make help` lista todos os comandos, opções e exemplos (com cores). Próximos passos: [docs/ROADMAP.md](docs/ROADMAP.md).

**Como funciona por dentro** — regras, matemática de cada métrica, score, busca, experimento de intervenção e um guia para
distinguir comportamento emergente de ruído ou oscilação: [docs/TEORIA.md](docs/TEORIA.md).

## Compilar e verificar

Requisitos: gcc (C11), make, pkg-config, SDL2 (só para `mcview`/`mcview3d`). `make deps` mostra o comando apt.

```sh
make build           # bin/microcosmos (CLI, sem SDL), bin/mcview (2D), bin/mcview3d (3D)
make test            # testes com AddressSanitizer + UBSan (inclui fuzz de parsers e checkpoints)
make mutate          # mutation testing (tests/mutate.sh, mutantes compilados com sanitizers)
make coverage        # cobertura de linhas/branches (gcov) — informativo, não é prova
make check           # build + test + mutate + smoke
make install         # instala os três binários em ~/.local/bin (PREFIX=...)
```

## 2D

```sh
./bin/microcosmos run --rule B3/S23 --world-seed 5 [--braille] [--ppm final.ppm]
./bin/microcosmos run --rule B2/S/C3                 # Generations: /C<n> estados (Brian's Brain)
./bin/microcosmos tui --rule B36/S23 --fps 20 --palette fogo   # animação braille colorida no terminal
./bin/mcview --rule B3/S23 [--palette oceano] [--ui N] [--win 1280x800]
./bin/mcview --rule B3/S23 --steps 120 --snapshot quadro.ppm   # sem janela: salva o quadro com HUD
```

### Busca e checkpoints

```sh
./bin/microcosmos search --strategy map --budget 2000 --checkpoint-every 50 --out runs/evo1   # Ctrl+C salva e sai
./bin/microcosmos search --resume runs/evo1/checkpoint.bin [--budget 5000]                    # continua / estende
./bin/microcosmos search ... --save meus/ck/final.bin --checkpoint-dir meus/ck/hist           # caminhos próprios
./bin/microcosmos elites --checkpoint runs/evo1/checkpoint.bin
./bin/microcosmos tui    --checkpoint runs/evo1/checkpoint.bin --rank 0
./bin/mcview             --checkpoint runs/evo1/checkpoint.bin --rank 0    # [ e ] trocam de elite
./bin/microcosmos compare --budget 200 --reps 5 --out runs/compare        # random vs evo vs map, mesmo orçamento
```

- Uma elite é reproduzida com a configuração exata do checkpoint (grade, borda, passos, sopa, `world_seed`), de t=0 até `steps`.
- Checkpoint = estado completo da busca, escrito de forma atômica. **Não é portátil** entre arquiteturas/builds; o leitor
  valida cabeçalho, índices, invariantes e bytes extras (arquivo corrompido é recusado com mensagem).
- Verificado: retomar de qualquer checkpoint (inclusive após Ctrl+C) gera `candidates.jsonl` idêntico ao da execução sem interrupção.

### Fase 5 — intervenção com controle

```sh
./bin/microcosmos probe --rule B3/S23 --dist 20 --reps 10 --steps 200
```

Por semente: um rollout de controle e outro com o 3×3 da fonte invertido em t=0; mede o primeiro passo em que o alvo 5×5,
a `dist` células, difere do controle. **Controle negativo**: com vizinhança de raio 1 nada viaja mais de 1 célula/passo,
então chegar antes de `gap = dist − 3` é impossível — violações são contadas (saída ≠ 0 indicaria defeito). "Detectado"
significa apenas "o alvo mudou dentro do orçamento"; não é chamado de comunicação.

Observado (10 sementes, 64×64, toro): `B3/S23` detectado em 9/10, chegadas 18–52 (gap 17), 0 violações;
a elite estável `B78/S13458` em 0/10.

## 3D

```sh
./bin/microcosmos run3d --rule B5-7/S4-6 --steps 60 --density 0.3 --braille [--slice 16]   # 32³, Moore 26
./bin/microcosmos search3d --strategy map --budget 150 --checkpoint-every 50 --out runs/e3d     # 16³ por padrão
./bin/microcosmos elites3d --checkpoint runs/e3d/checkpoint3d.bin
./bin/mcview3d [--rule B5-7/S4-6] [--nb moore|vn] [--d N] [--slice K] [--ui N] [--palette P]
./bin/mcview3d --checkpoint runs/e3d/checkpoint3d.bin --rank 0                                 # [ e ] trocam
```

- Regras 3D aceitam números, faixas e vírgulas (`B5-7/S4-5,9`, contagens 0..26). Com `--nb moore2d --d 1` o motor
  volumétrico reproduz exatamente o 2D (teste diferencial).
- Componentes (26-conexos) e centro de massa são medidos em 3D; `sensitivity` não (`-1`).
- `search3d` usa as mesmas estratégias, arquivo e filtros de score do 2D, com checkpoint próprio (`MC3D`); retomada
  verificada como idêntica (inclusive após Ctrl+C).
- `mcview3d`: cada célula é um cubo com faces iluminadas (topo claro, base escura), z-buffer, névoa por profundidade,
  caixa do mundo e gizmo X/Y/Z. Modo fatia (V, PgUp/PgDn) e edição com o botão direito na fatia.

## Interface (mcview e mcview3d)

- **Escala automática** do HUD pelo tamanho da janela (`min(L/480, A/320)`, 1..8); `--ui N` fixa; **F2/F3** diminuem/aumentam,
  **F4** volta ao automático. Painéis, minimapa e gizmo acompanham a escala; a janela é redimensionável.
- **Paletas** (C alterna; `--palette`): `idade` (ciano → amarelo → laranja → magenta), `fogo`, `oceano`, `mono`.
  Rastro violeta de células que morreram; em Generations, estados "morrendo" em azul.
- **H** mostra todos os controles na própria janela. Interface conferida por `--snapshot` em 640×400, 800×520, 1600×1000 e 1920×1080.

## Definições (indicadores, não provas)

- `persistence`: passos consecutivos desde t=1 com estado não vazio **e** diferente do anterior (osciladores contam).
- `activity_mean`, `density_mean`: médias de células alteradas / vivas (estado 1) por N em t=1..T.
- `cycle_period`/`cycle_start`: primeira repetição de estado por hash FNV-1a de 64 bits (colisão possível).
- `com_displacement`: deslocamento do centro de massa (média circular e imagem mínima no toro).
- `components_final`: componentes conexos (8 no 2D, 26 no 3D). `sensitivity`: fração diferente no fim após inverter o 3×3 central.
- **Score**: `persistence/T × min(1, components_final/16)`, mas **0 para ruído** — regra com B0, `activity_mean ≥ 0.25` ou
  `density_mean ≥ 0.25` (a sopa inicial ocupa ~12,5%). Critérios escolhidos após inspeção visual das elites: sem eles, o topo era
  ruído que cobria o mundo; com eles, estruturas estáveis e osciladores. Candidatos com score 0 continuam registrados.
- **Arquivo**: grade 10×10; X = `persistence/T`, Y = `activity_mean/0.25`; substitui só com score estritamente maior.
- **Estratégias** com o mesmo orçamento: `random`; `evo` (população 16, torneio binário, substitui o pior); `map` (pais entre as elites).

## Desempenho medido (CPU, sem paralelismo)

| carga | tempo |
|---|---|
| 2D 256×256, 1000 passos (motor 2D) | 0,46 s |
| idem no motor volumétrico com d=1 | 1,18 s |
| 3D 32³, 100 passos | 0,23 s |
| 3D 64³, 50 passos | 0,87 s |

Decisão: sem otimização por enquanto — o `mcview3d` em 32³ gasta ~2 ms por passo.

## Decisões e desvios da especificação

- **C11 + SDL2 em vez de Python** (decisão do usuário): mutation testing por script com mutantes escritos à mão.
- **Sem `configs/default.json`**: padrões no código e nas opções; toda a configuração usada vai para `run.json`/`run3d.json`.
- **Imagens PPM (P6)**, sem dependência de PNG.
- **Bordas**: `torus` (padrão), `fixed` (fora = morto), `reflect` (espelha para a célula da borda).
- **Condição inicial**: sopa com densidade `density` num quadrado/cubo central de lado `frac × lado`; a mesma `world_seed` para todos os candidatos.
- **Rollout para em ponto fixo** e extrapola as médias (`steps_simulated` registra o que foi calculado).
- **Motores 2D e 3D separados** (não unificados): o 2D é 2,6× mais rápido e tem formato de checkpoint estável; a igualdade
  entre os dois é garantida por teste diferencial. Ver [docs/ROADMAP.md](docs/ROADMAP.md).

## Resultado da comparação de estratégias (não é conclusão)

`compare --budget 200 --reps 5` (64×64, 300 passos, world_seed 1) **antes** dos filtros de atividade/densidade:
qd_score mediano random 4.44, evo 4.59, map 6.35. Só 5 sementes e uma condição inicial: não é evidência estatística.
