# Roadmap — Microcosmos

Fonte única de trabalho futuro. **Ciclo de vida** (ver CLAUDE.md):
1. Ideia/pedido novo entra em **Pendente** (com o porquê e, se houver, onde mexer).
2. Ao começar, move para **Em andamento** com o ponto exato onde parou.
3. Ao concluir e verificar, vai para **Concluído (recente)** com uma linha de evidência.
4. Na sessão seguinte, itens concluídos são **removidos** (o histórico fica no README/CLAUDE.md/git).

## Em andamento

_(nada)_

## Pendente

- **Teste de interação real (precisa de uma pessoa)**: abrir `mcview` e `mcview3d` num monitor e conferir teclado, mouse,
  arrastar, roda, redimensionar e F2/F3/F4. Tudo foi verificado só por `--snapshot` e driver de vídeo fictício.
- **Comparação estatística das estratégias** com os filtros de score atuais: mais sementes e condições iniciais
  (`compare --reps 30` em várias `--world-seed`), reportando mediana e dispersão. Os números do README são de antes dos filtros.

## Decisões registradas (não são pendências)

- **Não unificar os motores 2D e 3D**: o 2D é 2,6× mais rápido (medido) e o checkpoint `MCK1` fica estável; o teste
  diferencial (d=1 + Moore 2D == motor 2D) garante a equivalência. Rever só se a manutenção duplicada virar problema.
- **Sem otimização do motor 3D por enquanto**: 32³ ≈ 2 ms/passo no mcview3d (medido); perfilar de novo se for preciso 64³+ interativo.

## Concluído (recente — remover na próxima sessão)

- Braille 3D por fatia (`run3d --braille --slice K`, `volume_slice`). Evidência: testes de fatia e `--slice` inválido → código 2.
- Desempenho medido (tabela no README) e decisão de não otimizar.
- Cobertura: `export.c` 0% → 100% (testes de JSON/JSONL/CSV/PPM) e braille colorido testado.
- Score sem ruído: B0, `activity_mean ≥ 0.25` ou `density_mean ≥ 0.25` → 0 (`rule_score`, também no `run`).
  Evidência: elites antes/depois inspecionadas por `--snapshot` (antes: mundo coberto; depois: estruturas e osciladores).
- Fase 5 (`probe`): intervenção com controle de cone de luz. Evidência: oráculo de propagação a 1 célula/passo, propriedade
  "nenhuma chegada antes do cone" em 40 regras/bordas aleatórias, agregação testada com chegada impossível sintética.
- mcview3d: faces sombreadas (rasterização com 1/z perspectivo, faces internas e de costas descartadas) e edição por clique
  na fatia (`cam3_pick_slice`). Evidência: projetar→escolher devolve a mesma célula em 100+ câmeras; snapshots.
- Generations (`/C<n>`) e paletas (idade/fogo/oceano/mono). Evidência: oráculo do Brian's Brain à mão; states=2 idêntico ao
  binário (passos e rollout); `run B3/S23` com métricas iguais às de antes; snapshots.
- Interface responsiva: escala automática do HUD, `--ui N`, F2/F3/F4, painéis/minimapa/gizmo proporcionais.
  Evidência: `ui_auto_scale` testada (inclusive janela retrato) e snapshots em 4 resoluções.
- Busca 3D (`search3d`, `elites3d`, checkpoint `MC3D`, `mcview3d --checkpoint`). Evidência: retomada idêntica (testes e CLI,
  inclusive Ctrl+C), propriedade da elite máxima por célula, fuzz do leitor (300 arquivos), regra fora da faixa recusada.
- Mutation testing: 68/68 mutantes detectados (1 mutante equivalente documentado e removido da lista).
