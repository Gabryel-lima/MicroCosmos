# CLAUDE.md — Microcosmos

Instruções do projeto. Complementam o protocolo de verificação em `../CLAUDE.md` (que continua valendo).
Detalhes de uso e definições das métricas: [README.md](README.md).
**Onde paramos / próximos passos (inclui o plano 2D → 3D): [docs/ROADMAP.md](docs/ROADMAP.md) — ler no início de cada sessão e atualizar no fim.**

## Roadmap como instrução (obrigatório)

- Toda ideia ou pedido que não for feito na hora vai para `docs/ROADMAP.md` → **Pendente**, com o porquê.
- Ao começar um item: mover para **Em andamento** e registrar onde parou (arquivo/função) antes de o limite acabar.
- Ao concluir **e verificar**: mover para **Concluído (recente)** com uma linha de evidência (teste/comando).
- No início da sessão seguinte: **remover** os concluídos (o histórico fica no README e no git).
- Nunca deixar código pela metade fora do roadmap: ou termina e verifica, ou reverte e anota. `make help` lista todos os comandos.

## O que é

Laboratório de vida artificial: autômatos celulares 2D e 3D (regras B/S, Generations, vizinhanças Moore/von Neumann),
busca de regras (aleatória, evolutiva, MAP-Elites), experimento de intervenção (`probe`) e visualização. Objetivo: distinguir estruturas persistentes/funcionais
de ruído visualmente complexo, de forma **reproduzível**. Nunca alegar "vida", "comunicação" ou "inteligência".

## Decisões do usuário (padrão do projeto)

- **C11 + SDL2**, não Python. Sem dependências além de libc, libm e SDL2 (só no visualizador).
- **Motor sem gráficos**: `src/` (rules, world, sim, metrics, search, export, render) não inclui SDL.
  Só `src/viewer.c` e `src/viewer3d.c` usam SDL2. `render.c`/`render3d.c` desenham sem SDL num `Canvas` (testável).
- **Dois modos sempre**: sem janela (CLI/`tui` braille no terminal, `--snapshot`) e com janela (`mcview`, `mcview3d`). Toda
  funcionalidade visual nova deve existir nos dois, quando fizer sentido.
- **Reprodução do começo ao fim**: qualquer regra/elite deve poder ser reaberta com os parâmetros exatos
  (grade, borda, passos, sopa, `world_seed`) e rodada de t=0 até `steps`.
- **Evolução com checkpoints**: buscas longas salvam o estado completo (`--checkpoint-every`), Ctrl+C salva,
  `--resume` continua. Retomar deve dar resultado **idêntico** ao de uma execução sem interrupção.
- **Verificação visual**: interface nova é conferida gerando `--snapshot` e olhando a imagem (já pegou 3 defeitos que os testes não pegaram).
- **Visual agradável**: paletas (idade, fogo, oceano, mono), rastro de morte, arte braille (bloco 2×4), cubos 3D sombreados.
- **Interface responsiva**: o HUD escala com a janela (`ui_auto_scale`), `--ui N` fixa, F2/F3/F4 ajustam; todo painel novo
  deve usar a escala `S` (nada de tamanhos fixos em pixels) e ser conferido por `--snapshot` em janela pequena e grande.
- **Economia de tokens**: o usuário tem limite semanal. Trabalhar em incrementos pequenos, saídas de comando
  curtas (`tail`, `grep`), editar com substituições pontuais e não reler arquivos sem necessidade.
- Documentação, comentários e mensagens em **português**.

## Arquitetura

| Arquivo | Papel |
|---|---|
| `src/mc.h` | interface única do motor; RNG splitmix64 com estado explícito (nada global) |
| `src/rules.c` | parse/format/mutação B/S (bitmask de 9 bits por parte) |
| `src/world.c` | grade, bordas (`torus`/`fixed`/`reflect`), sopa por semente, hash FNV-1a |
| `src/sim.c` | passo e `rollout` com métricas online; para em ponto fixo e extrapola |
| `src/metrics.c` | componentes 8-conexos, centro de massa circular no toro |
| `src/search.c` | `SearchState` passo a passo, arquivo 10×10, checkpoints binários validados |
| `src/export.c` | `run.json`, `candidates.jsonl`, `archive.json`, `summary.csv`, PPM |
| `src/render.c` | idade/rastro, paleta, braille |
| `src/main.c`, `src/opts.c` | CLI: run, search, compare, elites, tui, probe, run3d, search3d, elites3d, smoke |
| `src/probe.c` | fase 5: intervenção com controle de cone de luz |
| `src/search3d.c` | busca 3D retomável, checkpoint `MC3D` |
| `src/volume.c` | motor volumétrico 2D/3D: vizinhanças, `Rule3` com faixas, `rollout3`, métricas 3D |
| `src/render3d.c` | câmera orbital, projeção, z-buffer, linhas, gizmo (sem SDL) |
| `src/viewer3d.c` | mcview3d (SDL2) |
| `src/viewer.c` | mcview (SDL2) |
| `tests/test_main.c`, `tests/mutate.sh` | testes e mutation testing |

## Documentação no código (obrigatório)

- **Todo arquivo** do projeto (`.c`, `.h`, scripts) começa com um comentário de cabeçalho no estilo
  Doxygen (`/** @file ... @brief ... */`) dizendo o papel do arquivo.
- **Toda função** pública (e as estáticas não triviais) tem comentário Doxygen com `@brief`, `@param`
  para cada parâmetro (incluindo unidade, intervalo válido e se pode ser NULL) e `@return` com o
  significado de cada valor (ex.: `0` ok, `-1` erro).
- **Todo enum** documenta o tipo e **cada valor** (`/**< ... */` ao lado do item).
- **Toda struct** documenta o tipo e **cada campo** (`/**< ... */`), com unidade/intervalo quando houver.
- Funções públicas são documentadas no header (`src/mc.h`, `src/opts.h`), que é o que a IDE mostra;
  nos `.c` ficam o `@file` e a documentação das funções `static`.
- Objetivo: tudo legível pelo hover/autocomplete da IDE e gerável com Doxygen. Código novo ou alterado
  sem esses comentários não está pronto.

## Comandos e `make help` (obrigatório)

- Todo comando ou opção nova dos binários (`microcosmos`, `mcview`, `mcview3d`) deve aparecer **no mesmo incremento**:
  1. na mensagem de uso do binário (`usage()` / `--help`);
  2. no `make help`, na lista de "Comandos diretos", com as opções;
  3. num alvo de exemplo `ex-*` do Makefile com comentário `## descrição` (aparece no help);
  4. no README.
- Exemplo: `--save`/`--checkpoint-dir` entraram no uso, no help (`search ... --save ... --checkpoint-dir ...`),
  no alvo `make ex-save` e no README.

## Padrões de código

- `-std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror`: zero warnings. Não usar `M_PI` (use `PI` local).
- Funções retornam `0`/`-1` e escrevem erro legível em `char *err, size_t errlen`; entrada inválida da
  CLI sai com código 2 e mensagem.
- Toda aleatoriedade vem de uma semente passada explicitamente; mesma semente ⇒ mesma saída byte a byte.
- Arquivos lidos do disco (checkpoints) são **não confiáveis**: validar cabeçalho, tamanhos, índices e
  invariantes; rejeitar bytes extras. Escrita atômica (`.tmp` + `rename`).
- Resultados negativos (candidatos extintos/ruins) são registrados, nunca descartados.
- Ao mudar o score/métricas, declarar a definição no README e em `run.json`.

## Verificação (obrigatória antes de dizer "pronto")

```sh
make build && make test && make mutate && make smoke   # ou: make check
make coverage                                           # informativo, não é prova
```

- Oráculos dos testes vêm de padrões conhecidos (bloco, blinker, glider) e contas à mão, nunca da própria
  implementação. Se um teste falhar, primeiro verificar se o **oráculo** está errado e explicar.
- Todo mutante sobrevivente é classificado; se for bug plausível, adicionar teste. Novo código de lógica ⇒
  novos mutantes em `tests/mutate.sh`.
- Parsers e leitores de arquivo recebem fuzz (dentro de `make test`, sob ASan/UBSan).
- Refatoração ⇒ teste diferencial: o `compare` e o `candidates.jsonl` devem sair idênticos aos de antes.
- **Recompilar antes de medir**: `make test` não recompila a CLI; use `make build` antes de `compare`/`search`
  (já houve um resultado "idêntico" falso por binário velho).
- Relatório final no formato do `../CLAUDE.md` (Implementado / Verificado / Não verificado / … / Conclusão).

## Lições registradas

- O score heurístico é **explorável**: B0 e depois regras que cobrem o mundo dominavam. Filtros de ruído (B0, atividade
  ≥ 0.25, densidade ≥ 0.25) foram escolhidos **olhando as elites por `--snapshot`** — repetir isso ao mudar o score.
- Edições por fatiamento de arquivo (substituir "do início da função até o próximo `/**`") já apagaram funções vizinhas:
  depois de editar assim, conferir que o build linka e que os testes daquela parte rodam.
- Diferenças entre estratégias com 5 sementes e uma condição inicial não são evidência estatística.
- Checkpoint binário não é portátil entre arquiteturas/builds (o cabeçalho detecta e recusa).
