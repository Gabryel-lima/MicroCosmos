# Como o Microcosmos funciona — regras, matemática e como ler os resultados

Este documento explica, em linguagem simples, **o que cada configuração faz**, **como cada métrica é calculada**
(com a fórmula exata usada no código) e **como distinguir um comportamento interessante de ruído ou de uma simples
oscilação**. Nada aqui exige conhecimento prévio de autômatos celulares.

---

## 1. O mundo

### 1.1 Grade e células
O mundo é uma grade de `w × h` células (no 3D, `w × h × d`). Cada célula tem um **estado**:

| estado | significado |
|---|---|
| 0 | morta (vazia) |
| 1 | viva |
| 2, 3, … | "morrendo" (só em regras Generations, seção 2.3) |

O tempo anda em **passos** `t = 0, 1, 2, …`. Em cada passo, **todas** as células são atualizadas ao mesmo tempo
(atualização síncrona): o estado novo de cada célula depende apenas do estado anterior dela e dos vizinhos.

### 1.2 Vizinhança
Os **vizinhos** de uma célula são as células ao redor dela:

| vizinhança | quem conta | nº de vizinhos |
|---|---|---|
| Moore 2D (padrão no 2D) | as 8 células que tocam a célula, inclusive na diagonal | 8 |
| Moore 3D (padrão no 3D) | o cubo 3×3×3 em volta, menos o centro | 26 |
| von Neumann 3D (`--nb vn`) | só as 6 faces (cima, baixo, frente, trás, esquerda, direita) | 6 |

Consequência importante: **a informação anda no máximo 1 célula por passo**. Isso é usado como controle no
experimento da seção 6 (o "cone de luz").

### 1.3 Bordas (`--boundary`)
O que acontece com os vizinhos que "caem fora" da grade:

- **`torus` (padrão)**: o mundo dá a volta, como no Pac-Man. Quem sai pela direita entra pela esquerda.
  Matematicamente, a coordenada `i` vira `((i mod n) + n) mod n`.
- **`fixed`**: fora da grade tudo é morto.
- **`reflect`**: a coordenada fora é trocada pela célula da borda (`-1 → 0`, `n → n−1`). Uma célula no canto acaba
  contando a si mesma como vizinha — é um efeito conhecido desta escolha.

### 1.4 Condição inicial: a "sopa" (`--density`, `--frac`, `--world-seed`)
O mundo começa vazio, e um **quadrado central** (cubo, no 3D) recebe células vivas ao acaso:

- o quadrado ocupa a fração `frac` de cada lado (padrão 0,5 → metade da largura e metade da altura, ou seja, 25% da área);
- dentro dele, cada célula nasce viva com probabilidade `density` (padrão 0,5).

Com os padrões, a densidade inicial do mundo inteiro é `0,25 × 0,5 = 12,5%`. Esse número volta na seção 4.

O sorteio usa um gerador pseudoaleatório (*splitmix64*) a partir de `--world-seed`. **Mesma semente = mesma sopa,
sempre**, em qualquer execução. É isso que torna tudo reproduzível.

---

## 2. As regras

### 2.1 Regras B/S (nascimento / sobrevivência)
Uma regra é escrita como `B<números>/S<números>`. Seja `n` o número de vizinhos **vivos** de uma célula:

- se a célula está **morta** e `n` está na lista **B** (*birth*), ela **nasce**;
- se a célula está **viva** e `n` está na lista **S** (*survival*), ela **continua viva**; senão, morre.

Exemplo: o Jogo da Vida é `B3/S23` — nasce com exatamente 3 vizinhos; sobrevive com 2 ou 3.

No código, cada lista é uma máscara de bits: o bit `k` ligado significa "`k` vizinhos está na lista".
No 2D há 9 contagens possíveis (0 a 8) para B e 9 para S, logo `2^18 = 262.144` regras diferentes.

### 2.2 Regras 3D
Mesma ideia, mas com contagens de 0 a 26 e uma escrita com faixas: `B5-7/S4-5,9` = nasce com 5, 6 ou 7 vizinhos;
sobrevive com 4, 5 ou 9. Com `--nb moore2d --d 1`, o motor 3D reproduz **exatamente** o 2D (há um teste que garante isso).

### 2.3 Generations (`/C<n>`)
Em `B2/S/C3` (o *Brian's Brain*), `C3` diz que há 3 estados. Uma célula viva que não sobrevive não some na hora:
vai para o estado 2, depois volta a 0. Com `C4` passaria por 2 → 3 → 0. Só células no estado 1 contam como vizinhas
vivas. Sem o sufixo `/C`, a regra é binária comum.

### 2.4 Mutação (usada na busca)
Mutar uma regra = **inverter exatamente um bit** escolhido ao acaso (um dos 18 no 2D). Ou seja, acrescentar ou tirar
um único número de B ou de S. Mudanças pequenas permitem explorar regras "vizinhas" de uma regra promissora.

---

## 3. As métricas (o que medimos em cada simulação)

Uma simulação ("rollout") roda de `t = 0` até `T` = `--steps` (padrão 300). Seja `N = w × h` o número de células,
`vivas(t)` o número de células no estado 1 no passo `t` e `mudadas(t)` o número de células cujo estado mudou entre
`t−1` e `t`. As métricas são calculadas **durante** a simulação, sem guardar todos os quadros.

| métrica | fórmula | o que diz, em palavras |
|---|---|---|
| `density_mean` | `(1/T) · Σ vivas(t)/N`, para t = 1..T | quanta coisa existe, em média |
| `activity_mean` | `(1/T) · Σ mudadas(t)/N` | quanto o mundo se mexe por passo |
| `persistence` | nº de passos seguidos, desde t = 1, em que o mundo **não está vazio** e **mudou** em relação ao passo anterior | por quanto tempo "algo acontece" |
| `extinct` | 1 se `vivas(T) = 0` | morreu tudo? |
| `cycle_period`, `cycle_start` | primeiro par `s < t` com `estado(s) = estado(t)`: período `t − s`, início `s` | o mundo entrou em repetição? (1 = congelou) |
| `components_final` | nº de grupos separados de células vivas no fim (grupos ligados por vizinhança de Moore) | quantos "objetos" sobraram |
| `com_displacement` | distância entre o centro de massa em `t = 0` e no fim | a massa viva se deslocou? |
| `sensitivity` | fração de células diferentes no fim entre a simulação normal e uma cópia com o bloco 3×3 central invertido em `t = 0` | um empurrãozinho muda o futuro? |

Detalhes importantes:

- **Parada antecipada**: se o mundo fica **idêntico** de um passo para o outro (ponto fixo), nada mais vai mudar.
  A simulação para e completa as médias como se continuasse igual. `steps_simulated` diz quantos passos foram calculados.
- **Detecção de ciclo**: comparar mundos inteiros a cada passo seria caro, então cada estado vira um "resumo" de 64 bits
  (hash FNV-1a). Dois mundos diferentes terem o mesmo resumo é possível, mas extremamente improvável (≈ 1 em 10^19 por par).
- **Centro de massa no toro**: no mundo que "dá a volta", a média comum erra (um objeto que cruza a borda teria centro
  no meio do mapa). Por isso cada eixo usa **média circular**: cada coordenada `x` vira um ângulo `θ = 2π·x/w`, tira-se
  a média dos vetores `(cos θ, sin θ)` e converte-se de volta com `atan2`. A distância usa a **imagem mínima**:
  `dx = min(|x1 − x0|, w − |x1 − x0|)`.
- **Persistência** conta osciladores (que mudam todo passo) como persistentes, mas não mundos congelados ou vazios.

### Exemplos de referência (verificados nos testes)

| padrão | persistence | activity_mean | cycle_period |
|---|---|---|---|
| mundo vazio | 0 | 0 | 1 |
| bloco 2×2 (parado) | 0 | 0 | 1 |
| blinker (pisca a cada passo) | T | 4/N | 2 |
| glider (anda na diagonal) | T | > 0 | 4·lado no toro (volta ao início) |

---

## 4. O score e o filtro de ruído

Para comparar regras, cada uma recebe uma nota (**score**) entre 0 e 1:

```
score = (persistence / T) × min(1, components_final / 16)
```

Em palavras: vale mais quem **continua ativo** o tempo todo e **termina com vários objetos separados** (até 16).

Mas essa nota sozinha é **enganável**: ruído puro também fica "ativo" e cheio de pedaços. Isso foi observado de verdade
(as primeiras buscas premiavam regras que só enchiam a tela de chiado). Por isso o score vira **0** quando:

| condição | por quê |
|---|---|
| a regra tem B0 | célula morta com 0 vizinhos nasce: o vazio inteiro pisca |
| `activity_mean ≥ 0,25` | mais de um quarto do mundo mudando a cada passo é chiado, não estrutura |
| `density_mean ≥ 0,25` | a sopa começa com ~12,5%; passar do dobro significa que a regra "encheu" o mundo |

Esses limites foram escolhidos **olhando as imagens das melhores regras** antes e depois do filtro. São uma heurística
declarada, não uma lei. As regras zeradas continuam registradas (resultado negativo também é dado).

---

## 5. A busca

### 5.1 O arquivo de diversidade (MAP-Elites)
Em vez de guardar só "a melhor regra", o programa guarda **a melhor de cada tipo**. O "tipo" é uma posição numa grade
10 × 10:

- **eixo X**: `persistence / T` (de 0 a 1), dividido em 10 faixas;
- **eixo Y**: `activity_mean / 0,25`, dividido em 10 faixas (atividade ≥ 0,25 cai na última).

Cada célula dessa grade guarda a regra de maior score que caiu nela (só troca se a nova for **estritamente** melhor).
Assim aparecem regras calmas, agitadas, curtas, longas… e não só um tipo.

- **cobertura** = quantas das 100 células estão ocupadas;
- **qd_score** = soma dos scores das elites (qualidade + diversidade).

### 5.2 As três estratégias (mesmo orçamento de avaliações)
- **`random`**: sorteia todas as regras. É a referência: se uma estratégia "inteligente" não supera isto, não vale a pena.
- **`evo`**: mantém uma população de 16; escolhe dois ao acaso, o melhor vira pai, gera um filho por mutação e, se o filho
  for melhor que o pior da população, o substitui.
- **`map`**: o pai é uma elite sorteada do arquivo; o filho mutado tenta entrar na sua célula.

### 5.3 Checkpoints
O estado inteiro da busca (sorteador, população, arquivo, candidatos) é salvo em disco. Como tudo depende só das
sementes, **retomar dá exatamente o mesmo resultado** de rodar sem parar — isso é testado byte a byte.

---

## 6. O experimento de intervenção (`probe`, fase 5)

Pergunta: **um sinal colocado num lugar produz efeito em outro lugar?**

1. Roda-se a mesma sopa duas vezes: **controle** (sem mexer) e **sinal** (inverte-se um bloco 3×3, a "fonte", em `t = 0`).
2. Observa-se um quadrado 5×5, o **alvo**, a `dist` células de distância.
3. A **chegada** é o primeiro passo em que o alvo fica diferente entre as duas execuções.

**Controles que tornam o resultado confiável:**

- **Sem sinal, diferença zero**: a simulação é determinística, então as duas execuções sem intervenção são idênticas.
  Toda diferença vem do sinal.
- **Cone de luz**: como nada anda mais de 1 célula por passo, a chegada mais cedo possível é
  `gap = dist − 3` (descontando o raio 1 da fonte e o raio 2 do alvo). Chegar antes disso é **impossível**; se acontecer,
  o medidor está errado. O programa conta essas violações (sempre deram 0).
- **Várias sementes** (`--reps`): o resultado é a fração de sementes em que o sinal chegou, não um caso isolado.

"Detectado" quer dizer só "o alvo mudou a tempo". **Não** quer dizer comunicação ou intenção.

---

## 7. Como saber se algo "emergiu" de verdade (e não é ruído ou oscilação)

Use esta lista de perguntas, em ordem. Cada uma tem um número ou um comando que responde.

| pergunta | como checar | sinal de ruído / trivial | sinal de comportamento interessante |
|---|---|---|---|
| **1. Morreu ou congelou?** | `extinct`, `cycle_period = 1` | sim → nada aconteceu | não |
| **2. É só um pisca-pisca?** | `cycle_period` pequeno (2, 3…) e `cycle_start` cedo | o mundo entra em repetição curta logo: oscilação trivial | ciclo longo, tardio ou ausente |
| **3. É chiado?** | `activity_mean`, `density_mean` | atividade ≥ 0,25 ou densidade ≥ 0,25: a tela "ferve" ou enche | atividade baixa/moderada, densidade perto da inicial |
| **4. Tem estrutura?** | `components_final`, imagem (`--snapshot`, `tui`) | um único borrão ou poeira uniforme | vários objetos separados e reconhecíveis |
| **5. Algo se move?** | `com_displacement`, olhar a animação | deslocamento ~0 com tudo parado | objetos que andam (como gliders) |
| **6. É reproduzível?** | rodar com outras `--world-seed` | só aparece em uma semente | aparece na maioria das sementes |
| **7. Reage e transmite?** | `sensitivity`, `probe --reps 10` | perturbação some (sensitivity ≈ 0) ou toma o mundo todo | efeito local que chega a outra região, respeitando o cone de luz, em várias sementes |
| **8. Não é artefato da métrica?** | ver a regra no `mcview` | score alto mas a imagem é chiado | a imagem confirma o número |

Regra de bolso:

- **Ruído** = muita mudança, sem forma, sem repetição útil, e muda totalmente com a semente.
- **Oscilação trivial** = repete com período curto logo no começo (o detector de ciclo pega).
- **Comportamento reproduzível interessante** = estruturas que persistem sem congelar, aparecem em **várias sementes**,
  e/ou propagam efeitos de forma local e mensurável.

E sempre: **um número só não prova nada**. Os indicadores reduzem a chance de se enganar, mas a última palavra é
combinar métricas, várias sementes e olhar a simulação.

---

## 8. Resumo das configurações

| opção | padrão | efeito |
|---|---|---|
| `--rule` | `B3/S23` | regra (seção 2) |
| `--w`, `--h` (`--d` no 3D) | 64×64 (32³ ou 16³ no 3D) | tamanho do mundo |
| `--boundary` | `torus` | borda (seção 1.3) |
| `--steps` | 300 | duração `T` de cada simulação |
| `--density`, `--frac` | 0,5 e 0,5 | sopa inicial (seção 1.4) |
| `--world-seed` | 1 | semente da sopa |
| `--no-perturb` | (mede) | desliga o cálculo de `sensitivity` (economiza uma simulação extra) |
| `--strategy`, `--budget`, `--pop`, `--seed` | `map`, 200, 16, 1 | busca (seção 5) |
| `--nb` | `moore` no 3D | vizinhança (seção 1.2) |
| `--dist`, `--reps` | 20, 5 | experimento de intervenção (seção 6) |
| `--palette`, `--ui` | `idade`, automática | só aparência; não altera nenhum resultado |
