/**
 * @file mc.h
 * @brief Interface pública do motor Microcosmos.
 *
 * O motor (regras, mundo, simulação, métricas, busca, exportação e renderização sem janela)
 * não depende de SDL. Convenções gerais:
 * - funções que podem falhar retornam `0` (ok) ou `-1` (erro) e, quando recebem `err`/`errlen`,
 *   escrevem ali uma mensagem legível em português;
 * - toda aleatoriedade usa um estado de RNG explícito (`uint64_t *rng`); mesma semente ⇒ mesma saída.
 */
#ifndef MC_H
#define MC_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/** @brief Versão do motor gravada em `run.json` e na saída de `run`. */
#define MC_ENGINE_VERSION "0.1.0"

/* =====================================================================================
 * RNG determinístico (splitmix64)
 * ===================================================================================== */

/**
 * @brief Avança o gerador splitmix64 e devolve 64 bits pseudoaleatórios.
 * @param s Estado do gerador (não NULL); é modificado.
 * @return Próximo valor de 64 bits.
 */
static inline uint64_t rng_next(uint64_t *s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/**
 * @brief Número real uniforme em [0, 1) com 53 bits de precisão.
 * @param s Estado do gerador (não NULL); é modificado.
 * @return Valor em [0, 1).
 */
static inline double rng_unit(uint64_t *s) { return (double)(rng_next(s) >> 11) * 0x1.0p-53; }

/**
 * @brief Inteiro em [0, n) (viés de módulo desprezível para n pequeno).
 * @param s Estado do gerador (não NULL); é modificado.
 * @param n Limite superior exclusivo; deve ser > 0.
 * @return Valor em [0, n).
 */
static inline uint32_t rng_below(uint64_t *s, uint32_t n) { return (uint32_t)(rng_next(s) % n); }

/* =====================================================================================
 * Regras B/S totalísticas, vizinhança de Moore
 * ===================================================================================== */

/** @brief Regra totalística B/S como duas máscaras de 9 bits (bit k ligado = k vizinhos ativos, k em 0..8). */
typedef struct {
    uint16_t birth;   /**< Contagens de vizinhos que fazem uma célula morta nascer (parte "B"). */
    uint16_t survive; /**< Contagens de vizinhos que mantêm uma célula viva (parte "S"). */
} Rule;

/** @brief Máscara dos 9 bits válidos de cada parte da regra. */
#define RULE_MASK 0x1FFu
/** @brief Tamanho de buffer suficiente para a forma textual de qualquer regra (inclui o '\\0'). */
#define RULE_STR_MAX 24

/**
 * @brief Lê uma regra no formato "B<dígitos>/S<dígitos>" (ex.: "B3/S23"; letras B/S sem distinção de caixa).
 * @param s Texto da regra; NULL é erro.
 * @param out Regra lida (não NULL); só é escrita em caso de sucesso.
 * @param err Buffer de mensagem de erro; pode ser NULL.
 * @param errlen Tamanho de @p err.
 * @return 0 se válida; -1 se inválida (dígito > 8, repetido, separador ausente, caractere extra...).
 */
int rule_parse(const char *s, Rule *out, char *err, size_t errlen);

/**
 * @brief Escreve a forma canônica da regra (dígitos em ordem crescente, ex.: "B36/S23").
 * @param r Regra.
 * @param buf Destino (não NULL).
 * @param n Tamanho de @p buf; use RULE_STR_MAX.
 */
void rule_format(Rule r, char *buf, size_t n);

/**
 * @brief Lê uma regra com sufixo opcional de estados (Generations): "B2/S/C3" (Brian's Brain).
 * Sem sufixo, states = 2 (binário, idêntico a rule_parse).
 * @param s Texto. @param out Regra. @param states Número de estados em 2..255 (saída).
 * @param err Mensagem (pode ser NULL). @param errlen Tamanho. @return 0 ok; -1 inválida.
 */
int rule_parse_gen(const char *s, Rule *out, int *states, char *err, size_t errlen);

/** @brief Forma canônica com "/C<n>" quando states > 2. @param r Regra. @param states Estados. @param buf Destino. @param n Tamanho. */
void rule_format_gen(Rule r, int states, char *buf, size_t n);

/**
 * @brief Compara duas regras considerando só os bits válidos.
 * @return 1 se iguais, 0 caso contrário.
 */
int rule_equal(Rule a, Rule b);

/**
 * @brief Sorteia uma regra uniformemente entre as 2^18 possíveis.
 * @param rng Estado do gerador (não NULL).
 * @return Regra sorteada.
 */
Rule rule_random(uint64_t *rng);

/**
 * @brief Mutação: inverte exatamente um dos 18 bits (9 de B, 9 de S), escolhido uniformemente.
 * @param r Regra de origem.
 * @param rng Estado do gerador (não NULL).
 * @return Regra mutada (difere de @p r em exatamente um bit).
 */
Rule rule_mutate(Rule r, uint64_t *rng);

/* =====================================================================================
 * Mundo
 * ===================================================================================== */

/** @brief Tratamento das bordas da grade. */
typedef enum {
    BOUND_TORUS,  /**< Periódica: sai por um lado e entra pelo oposto. */
    BOUND_FIXED,  /**< Fixa: fora da grade conta como célula morta. */
    BOUND_REFLECT /**< Refletora: coordenada fora espelha para a célula da borda (i<0 → 0, i>=n → n-1). */
} Boundary;

/** @brief Grade 2D de células binárias. */
typedef struct {
    int w;          /**< Largura em células (3..4096). */
    int h;          /**< Altura em células (3..4096). */
    Boundary bound; /**< Borda usada na vizinhança. */
    uint8_t *c;     /**< w*h células em ordem de linhas (0 = morta, 1 = viva); dono: world_init/world_free. */
} World;

/**
 * @brief Índice de uma coordenada após aplicar a borda.
 * @param i Coordenada, possivelmente fora de [0, n).
 * @param n Tamanho do eixo.
 * @param b Tipo de borda.
 * @return Índice em [0, n), ou -1 se fora do mundo com borda fixa.
 */
static inline int mc_wrap(int i, int n, Boundary b) {
    if (i >= 0 && i < n) return i;
    if (b == BOUND_TORUS) return ((i % n) + n) % n;
    if (b == BOUND_REFLECT) return i < 0 ? 0 : n - 1;
    return -1;
}

/**
 * @brief Ponteiro para a célula (x, y), sem verificação de limites.
 * @param w Mundo. @param x Coluna em [0, w). @param y Linha em [0, h).
 * @return Ponteiro para a célula.
 */
static inline uint8_t *world_at(World *w, int x, int y) { return &w->c[(size_t)y * (size_t)w->w + (size_t)x]; }

/**
 * @brief Valor da célula (x, y), sem verificação de limites.
 * @param w Mundo. @param x Coluna em [0, w). @param y Linha em [0, h).
 * @return 0 (morta) ou 1 (viva).
 */
static inline uint8_t world_get(const World *w, int x, int y) { return w->c[(size_t)y * (size_t)w->w + (size_t)x]; }

/**
 * @brief Aloca um mundo vazio.
 * @param w Mundo a inicializar (não NULL); em erro, `w->c` fica NULL.
 * @param width Largura (3..4096). @param height Altura (3..4096).
 * @param b Borda.
 * @return 0 ok; -1 se dimensões inválidas ou sem memória.
 */
int world_init(World *w, int width, int height, Boundary b);

/** @brief Libera as células (seguro chamar duas vezes). @param w Mundo. */
void world_free(World *w);

/** @brief Zera todas as células. @param w Mundo. */
void world_clear(World *w);

/**
 * @brief Copia células e borda.
 * @param dst Destino; deve ter as mesmas dimensões de @p src.
 * @param src Origem.
 */
void world_copy(World *dst, const World *src);

/**
 * @brief Condição inicial reproduzível: limpa o mundo e preenche um quadrado central com ruído.
 * @param w Mundo.
 * @param seed Semente da sopa (mesma semente ⇒ mesma sopa).
 * @param density Probabilidade de cada célula do quadrado nascer viva, em [0, 1].
 * @param frac Fração de cada eixo ocupada pelo quadrado central, em [0, 1].
 */
void world_soup(World *w, uint64_t seed, double density, double frac);

/** @brief Inverte o bloco 3x3 em torno do centro (perturbação padrão). @param w Mundo. */
void world_perturb_center(World *w);

/** @brief Número de células vivas (estado 1; estados 2.. de Generations não contam). @param w Mundo. @return Contagem. */
int world_count(const World *w);

/**
 * @brief Número de células diferentes entre dois mundos de mesmas dimensões.
 * @return Distância de Hamming.
 */
int world_diff(const World *a, const World *b);

/**
 * @brief Hash FNV-1a de 64 bits das células (usado para detectar ciclos; colisões são possíveis).
 * @param w Mundo. @return Hash.
 */
uint64_t world_hash(const World *w);

/**
 * @brief Converte "torus", "fixed" ou "reflect" em Boundary.
 * @param s Texto. @param out Resultado (só escrito em sucesso).
 * @return 0 ok; -1 se desconhecido ou NULL.
 */
int boundary_parse(const char *s, Boundary *out);

/** @brief Nome textual da borda. @param b Borda. @return "torus", "fixed", "reflect" ou "?". */
const char *boundary_name(Boundary b);

/* =====================================================================================
 * Simulação
 * ===================================================================================== */

/**
 * @brief Um passo síncrono da regra (vizinhança de Moore, 8 vizinhos, borda de @p src).
 * @param r Regra.
 * @param src Estado atual.
 * @param dst Próximo estado; mesmas dimensões, distinto de @p src.
 */
void sim_step(Rule r, const World *src, World *dst);

/**
 * @brief Passo Generations: estado 0 nasce por B; estado 1 fica por S, senão vai a 2 (ou 0 se states = 2);
 * estados 2..states-1 avançam até voltar a 0. Só vizinhos no estado 1 contam. states = 2 ≡ sim_step.
 * @param r Regra. @param states Número de estados (2..255). @param src Atual. @param dst Próximo (distinto).
 */
void sim_step_gen(Rule r, int states, const World *src, World *dst);

/** @brief Parâmetros de um rollout (simulação de t=0 até `steps`). */
typedef struct {
    int w;            /**< Largura da grade (3..4096). */
    int h;            /**< Altura da grade (3..4096). */
    Boundary bound;   /**< Borda. */
    int steps;        /**< Passos por rollout (1..1000000). */
    double density;   /**< Densidade da sopa inicial, em [0, 1]. */
    double soup_frac; /**< Fração central (por eixo) ocupada pela sopa, em [0, 1]. */
    int perturb;      /**< 1 = mede sensibilidade (custa um rollout extra); 0 = não mede. */
} SimConfig;

/** @brief Configuração padrão: 64x64, toro, 300 passos, sopa 0.5 em 50% central, com perturbação. */
SimConfig sim_default_config(void);

/**
 * @brief Valida os intervalos de uma configuração.
 * @param c Configuração. @param err Mensagem de erro. @param errlen Tamanho de @p err.
 * @return 0 ok; -1 inválida.
 */
int sim_config_validate(const SimConfig *c, char *err, size_t errlen);

/** @brief Descritores de um rollout. São indicadores observáveis, não provas de vida ou função. */
typedef struct {
    int steps;               /**< Orçamento nominal T. */
    int steps_simulated;     /**< Passos efetivamente calculados (para ao atingir ponto fixo). */
    int alive_final;         /**< Células vivas no estado final. */
    int extinct;             /**< 1 se o estado final está vazio. */
    int persistence;         /**< Passos consecutivos desde t=1 com estado não vazio E diferente do anterior. */
    double density_mean;     /**< Média de vivas/N em t=1..T. */
    double activity_mean;    /**< Média de células alteradas/N em t=1..T. */
    int cycle_period;        /**< Período da primeira repetição de estado (por hash); 0 = nenhuma; 1 = estático. */
    int cycle_start;         /**< Passo em que o ciclo detectado começa. */
    double com_displacement; /**< Distância do centro de massa entre t=0 e o final (imagem mínima no toro). */
    int components_final;    /**< Componentes 8-conexos no estado final. */
    double sensitivity;      /**< Fração de células diferentes no final após inverter o 3x3 central em t=0; -1 se não medido. */
} Metrics;

/**
 * @brief Executa um rollout e calcula as métricas online.
 * @param r Regra.
 * @param cfg Configuração (dimensões devem bater com @p init).
 * @param init Estado em t=0 (não é modificado).
 * @param m Métricas de saída (não NULL).
 * @param final_out Recebe o estado final; pode ser NULL.
 * @return 0 ok; -1 sem memória.
 */
int rollout(Rule r, const SimConfig *cfg, const World *init, Metrics *m, World *final_out);

/**
 * @brief rollout com Generations (states = 2 ≡ rollout). "Vivas" = estado 1; atividade conta qualquer mudança de estado.
 * @param r Regra. @param states Estados (2..255). @param cfg,init,m,final_out Como em rollout. @return 0 ok; -1 erro.
 */
int rollout_gen(Rule r, int states, const SimConfig *cfg, const World *init, Metrics *m, World *final_out);

/* =====================================================================================
 * Métricas puras sobre estados
 * ===================================================================================== */

/**
 * @brief Conta componentes 8-conexos de células vivas, respeitando a borda.
 * @param w Mundo. @return Número de componentes; -1 sem memória.
 */
int metric_components(const World *w);

/**
 * @brief Centro de massa das células vivas (média circular por eixo no toro, resultado em [0, n)).
 * @param w Mundo. @param cx Coordenada x de saída. @param cy Coordenada y de saída.
 * @return 1 se calculado; 0 se o mundo está vazio (saídas não escritas).
 */
int metric_com(const World *w, double *cx, double *cy);

/**
 * @brief Distância entre dois pontos; no toro usa a imagem mínima.
 * @param w Mundo (define dimensões e borda). @param x0,y0 Ponto inicial. @param x1,y1 Ponto final.
 * @return Distância euclidiana em células.
 */
double metric_com_distance(const World *w, double x0, double y0, double x1, double y1);

/* =====================================================================================
 * Busca e arquivo de diversidade
 * ===================================================================================== */

/** @brief Estratégia de busca (todas com o mesmo orçamento de avaliações). */
typedef enum {
    STRAT_RANDOM, /**< Baseline: todas as regras sorteadas. */
    STRAT_EVO,    /**< Evolutiva de estado estacionário: torneio binário, substitui o pior; sem arquivo. */
    STRAT_MAP     /**< MAP-Elites mínimo: pais escolhidos entre as elites do arquivo. */
} Strategy;

/** @brief Número de bins por eixo do arquivo (grade ARCH_BINS x ARCH_BINS). */
#define ARCH_BINS 10
/** @brief Atividade média a partir da qual o candidato cai no último bin do eixo Y (e é tratado como ruído no score). */
#define ARCH_ACT_MAX 0.25
/** @brief Densidade média a partir da qual a regra é tratada como ruído no score (o mundo fica coberto). */
#define SCORE_DENS_MAX 0.25

/** @brief Um candidato avaliado. Todos são registrados, inclusive os ruins. */
typedef struct {
    int id;              /**< Ordem de avaliação (0..budget-1). */
    int parent;          /**< id do pai; -1 se sorteado. */
    int gen;             /**< Geração: 0 se sorteado, senão gen do pai + 1. */
    Rule rule;           /**< Regra avaliada. */
    uint64_t world_seed; /**< Semente da condição inicial usada. */
    Metrics m;           /**< Descritores do rollout. */
    double score;        /**< Score declarado (candidate_score; 0 para regras B0). */
    int cell;            /**< Célula do arquivo: y*ARCH_BINS + x. */
} Candidate;

/** @brief Arquivo de diversidade estilo MAP-Elites. */
typedef struct {
    int elite[ARCH_BINS * ARCH_BINS]; /**< Por célula: índice do melhor candidato, ou -1 se vazia. */
} Archive;

/** @brief Parâmetros de uma busca. */
typedef struct {
    Strategy strat;      /**< Estratégia. */
    int budget;          /**< Número total de avaliações (>= 1). */
    int pop;             /**< Avaliações aleatórias iniciais e tamanho da população (EVO) (>= 1). */
    uint64_t seed;       /**< Semente da busca (sorteios e mutações). */
    uint64_t world_seed; /**< Semente da condição inicial, igual para todos os candidatos. */
    SimConfig sim;       /**< Configuração de cada rollout. */
} SearchConfig;

/** @brief Resultado (parcial ou final) de uma busca. */
typedef struct {
    Candidate *cand;     /**< Vetor com capacidade = budget; dono: search_free. */
    int n;               /**< Candidatos já avaliados. */
    Archive arch;        /**< Mapa de todos os avaliados (para MAP é também o arquivo da busca). */
    int coverage;        /**< Células ocupadas do arquivo (após search_stats). */
    double qd_score;     /**< Soma dos scores das elites (após search_stats). */
    double best_score;   /**< Maior score (após search_stats). */
    double median_score; /**< Mediana dos scores (após search_stats). */
    double extinct_frac; /**< Fração de candidatos extintos (após search_stats). */
} SearchResult;

/**
 * @brief Score heurístico declarado: persistence/T × min(1, components_final/16).
 * @param m Métricas. @return Score em [0, 1].
 */
double candidate_score(const Metrics *m);

/**
 * @brief Score usado na busca e no `run`: 0 para ruído (regra com B0, activity_mean >= ARCH_ACT_MAX ou
 * density_mean >= SCORE_DENS_MAX),
 * senão candidate_score. Candidatos com score 0 continuam registrados e ocupando células do arquivo.
 * @param r Regra. @param m Métricas. @return Score em [0, 1].
 */
double rule_score(Rule r, const Metrics *m);

/**
 * @brief Célula do arquivo: X = bin(persistence/T), Y = bin(activity_mean/ARCH_ACT_MAX), clampados.
 * @param m Métricas. @return Índice em [0, ARCH_BINS²).
 */
int archive_cell(const Metrics *m);

/** @brief Esvazia o arquivo (todas as células = -1). @param a Arquivo. */
void archive_clear(Archive *a);

/**
 * @brief Oferece um candidato à sua célula; substitui só com score estritamente maior.
 * @param a Arquivo. @param all Vetor de candidatos. @param idx Índice do candidato oferecido.
 * @return 1 se inserido; 0 caso contrário.
 */
int archive_offer(Archive *a, const Candidate *all, int idx);

/**
 * @brief Executa uma busca completa (begin + step até o fim + stats).
 * @param sc Configuração. @param res Resultado (liberar com search_free).
 * @param err Mensagem de erro. @param errlen Tamanho de @p err.
 * @return 0 ok; -1 erro.
 */
int search_run(const SearchConfig *sc, SearchResult *res, char *err, size_t errlen);

/** @brief Libera os candidatos do resultado. @param res Resultado. */
void search_free(SearchResult *res);

/**
 * @brief Converte "random", "evo" ou "map" em Strategy.
 * @param s Texto. @param out Resultado (só escrito em sucesso). @return 0 ok; -1 desconhecida.
 */
int strategy_parse(const char *s, Strategy *out);

/** @brief Nome textual da estratégia. @param s Estratégia. @return "random", "evo", "map" ou "?". */
const char *strategy_name(Strategy s);

/**
 * @brief Mediana; **ordena** o vetor no lugar.
 * @param v Valores. @param n Quantidade. @return Mediana; 0 se n <= 0.
 */
double median_of(double *v, int n);

/** @brief Estado retomável da busca; um checkpoint é este estado serializado. */
typedef struct {
    SearchConfig sc;  /**< Configuração (budget pode crescer com search_extend). */
    uint64_t rng;     /**< Estado atual do gerador da busca. */
    int npop;         /**< Tamanho atual da população (EVO). */
    int *pop;         /**< Índices dos candidatos na população (capacidade sc.pop). */
    SearchResult res; /**< Candidatos avaliados até agora (res.n) e arquivo. */
    World init;       /**< Condição inicial compartilhada (recriada a partir de world_seed). */
} SearchState;

/**
 * @brief Prepara uma busca sem avaliar nada.
 * @param st Estado (liberar com search_state_free). @param sc Configuração.
 * @param err Mensagem de erro. @param errlen Tamanho de @p err.
 * @return 0 ok; -1 configuração inválida ou sem memória.
 */
int search_begin(SearchState *st, const SearchConfig *sc, char *err, size_t errlen);

/**
 * @brief Avalia o próximo candidato.
 * @param st Estado. @param err Mensagem de erro. @param errlen Tamanho de @p err.
 * @return 1 avaliou; 0 orçamento esgotado; -1 erro.
 */
int search_step(SearchState *st, char *err, size_t errlen);

/**
 * @brief Aumenta o orçamento (ex.: ao retomar); valores menores ou iguais não mudam nada.
 * @param st Estado. @param budget Novo orçamento total.
 * @return 0 ok; -1 sem memória.
 */
int search_extend(SearchState *st, int budget);

/** @brief Recalcula coverage, qd_score, best/median_score e extinct_frac. @param res Resultado. */
void search_stats(SearchResult *res);

/** @brief Libera todo o estado (seguro após erro de begin/load). @param st Estado. */
void search_state_free(SearchState *st);

/**
 * @brief Salva checkpoint binário com escrita atômica (`path.tmp` + rename).
 * Não é portátil entre arquiteturas/builds com layout de struct diferente.
 * @param st Estado. @param path Arquivo de destino (o diretório deve existir).
 * @return 0 ok; -1 erro de E/S.
 */
int search_save(const SearchState *st, const char *path);

/**
 * @brief Carrega e valida um checkpoint (arquivo tratado como não confiável).
 * @param st Estado de saída (liberar com search_state_free). @param path Arquivo.
 * @param err Mensagem de erro. @param errlen Tamanho de @p err.
 * @return 0 ok; -1 arquivo ausente, de outra versão/arquitetura, truncado ou inconsistente.
 */
int search_load(SearchState *st, const char *path, char *err, size_t errlen);

/**
 * @brief Carrega um checkpoint e devolve a regra da elite de posição @p rank.
 * @param path Checkpoint. @param rank Posição no ranking (0 = melhor score).
 * @param rule Regra de saída. @param sc Configuração usada na busca (saída).
 * @param nelites Recebe o número de elites; pode ser NULL.
 * @param err Mensagem de erro. @param errlen Tamanho de @p err.
 * @return 0 ok; -1 checkpoint inválido ou rank fora do intervalo.
 */
int checkpoint_elite(const char *path, int rank, Rule *rule, SearchConfig *sc, int *nelites, char *err, size_t errlen);

/**
 * @brief Elites em ordem decrescente de score (empate: menor id).
 * @param res Resultado. @param out Destino com ARCH_BINS² posições.
 * @return Quantidade de elites escritas.
 */
int archive_ranked(const SearchResult *res, int *out);

/* =====================================================================================
 * Exportação
 * ===================================================================================== */

/** @brief Cria o diretório e os pais (como `mkdir -p`). @param path Caminho (< 1024 bytes). @return 0 ok; -1 erro. */
int mkdir_p(const char *path);

/** @brief Escreve @p s como string JSON com escapes. @param f Arquivo. @param s Texto (não NULL). */
void json_str(FILE *f, const char *s);

/** @brief Escreve as métricas como objeto JSON. @param f Arquivo. @param m Métricas. */
void json_metrics(FILE *f, const Metrics *m);

/** @brief Escreve a configuração de simulação como objeto JSON. @param f Arquivo. @param c Configuração. */
void json_simconfig(FILE *f, const SimConfig *c);

/**
 * @brief Salva o mundo como imagem PPM (P6).
 * @param path Arquivo. @param w Mundo. @param scale Pixels por célula (>= 1).
 * @return 0 ok; -1 erro de E/S.
 */
int export_ppm(const char *path, const World *w, int scale);

/**
 * @brief Exporta run.json, candidates.jsonl, archive.json, summary.csv, archive_map.ppm e PPMs das 8 melhores elites.
 * @param dir Diretório (criado se preciso). @param sc Configuração. @param res Resultado.
 * @param cmdline Linha de comando registrada em run.json.
 * @return 0 ok; -1 erro de E/S.
 */
int export_search(const char *dir, const SearchConfig *sc, const SearchResult *res, const char *cmdline);

/* =====================================================================================
 * Renderização sem SDL (render.c)
 * ===================================================================================== */

/**
 * @brief Atualiza idade e rastro após um passo.
 * @param w Estado atual.
 * @param age Por célula: passos consecutivos viva (recém-nascida = 1, morta = 0; satura em UINT16_MAX).
 * @param trail Por célula: 255 no passo em que morre, depois decai 7/8 por passo; 0 se viva.
 */
void age_update(const World *w, uint16_t *age, uint8_t *trail);

/**
 * @brief Igual a age_update para um vetor qualquer de células (usado pelo volume 3D).
 * @param c Células (0/1). @param N Quantidade. @param age Idades. @param trail Rastros.
 */
void age_update_cells(const uint8_t *c, size_t N, uint16_t *age, uint8_t *trail);

/**
 * @brief Cor de uma célula: vivas ciano → amarelo → laranja → magenta (log2 da idade, magenta em 256+);
 * mortas: fundo azul-escuro misturado com violeta conforme o rastro.
 * @param age Idade (0 = morta). @param trail Rastro (só usado se morta). @param rgb Cor de saída.
 */
void palette_color(uint16_t age, uint8_t trail, uint8_t rgb[3]);

/** @brief Paletas disponíveis (o valor também é aceito como parâmetro `color` dos renderizadores). */
enum {
    PALETTE_MONO = 0,  /**< Branco sobre fundo escuro (sem idade). */
    PALETTE_AGE = 1,   /**< Ciano → amarelo → laranja → magenta (padrão). */
    PALETTE_FIRE = 2,  /**< Amarelo-claro → laranja → vermelho-escuro. */
    PALETTE_OCEAN = 3, /**< Ciano-claro → azul → azul-marinho. */
    PALETTE_COUNT = 4  /**< Quantidade de paletas. */
};

/**
 * @brief Cor por idade numa paleta escolhida (palette_color = PALETTE_AGE).
 * @param pal Paleta (fora do intervalo = PALETTE_AGE). @param age Idade (0 = morta). @param trail Rastro. @param rgb Saída.
 */
void palette_color_p(int pal, uint16_t age, uint8_t trail, uint8_t rgb[3]);

/** @brief Converte "mono", "idade", "fogo" ou "oceano" em PALETTE_*. @param s Texto. @param pal Saída. @return 0 ok; -1 desconhecida. */
int palette_parse(const char *s, int *pal);

/** @brief Nome da paleta ("MONO", "IDADE", "FOGO", "OCEANO"). @param pal Paleta. @return Nome ou "?". */
const char *palette_name(int pal);

/**
 * @brief Bits do caractere braille (U+2800 + bits) para o bloco 2x4 com canto em (x0, y0).
 * Pontos: coluna 0 = 1,2,3,7 (0x01,0x02,0x04,0x40); coluna 1 = 4,5,6,8 (0x08,0x10,0x20,0x80).
 * @param w Mundo. @param x0 Coluna do canto. @param y0 Linha do canto.
 * @return Bits em [0, 0xFF]; células fora da grade contam como vazias.
 */
unsigned braille_bits(const World *w, int x0, int y0);

/**
 * @brief Escreve o mundo em arte braille (UTF-8), uma linha de texto por 4 linhas da grade.
 * @param f Arquivo de saída.
 * @param w Mundo.
 * @param age Idades para colorir (ANSI 24 bits, cor da célula mais velha do bloco); NULL = monocromático, sem códigos ANSI.
 */
void braille_render(FILE *f, const World *w, const uint16_t *age);

/** @brief braille_render com paleta escolhida. @param f Saída. @param w Mundo. @param age Idades (NULL = mono). @param pal Paleta. */
void braille_render_p(FILE *f, const World *w, const uint16_t *age, int pal);

/** @brief Imagem ARGB8888 em memória onde o HUD e o mundo são desenhados (sem SDL). */
typedef struct {
    uint32_t *px; /**< w*h pixels ARGB8888 em ordem de linhas. */
    int w;        /**< Largura em pixels. */
    int h;        /**< Altura em pixels. */
} Canvas;

/** @brief Câmera 2D: qual parte do mundo aparece na tela. */
typedef struct {
    double x;    /**< Coordenada (em células) do canto superior esquerdo da tela. */
    double y;    /**< Idem, eixo y (cresce para baixo). */
    double zoom; /**< Pixels por célula, em [VIEW_ZOOM_MIN, VIEW_ZOOM_MAX]. */
} View;

/** @brief Zoom mínimo (pixels por célula). */
#define VIEW_ZOOM_MIN 0.25
/** @brief Zoom máximo (pixels por célula). */
#define VIEW_ZOOM_MAX 64.0

/**
 * @brief Bits do glifo 3x5 de um caractere (bit k = linha k/3, coluna k%3); minúsculas viram maiúsculas.
 * @param c Caractere. @param bits Saída (15 bits).
 * @return 1 se o caractere existe na fonte; 0 se foi substituído por '?'.
 */
int font_glyph(char c, uint16_t *bits);

/**
 * @brief Salva o canvas como PPM (P6, sem alfa).
 * @param cv Canvas. @param path Arquivo de destino. @return 0 ok; -1 erro de E/S.
 */
int canvas_save_ppm(const Canvas *cv, const char *path);

/** @brief Maior escala de interface aceita (pixels por ponto da fonte). */
#define UI_SCALE_MAX 8

/**
 * @brief Escala automática do HUD pelo tamanho da janela: min(cw/480, ch/320), limitada a [1, UI_SCALE_MAX].
 * Ex.: 640x400 → 1; 1000x700 → 2; 1920x1080 → 3; 2560x1440 → 4.
 * @param cw,ch Tamanho da janela em pixels. @return Escala.
 */
int ui_auto_scale(int cw, int ch);

/** @brief Largura em pixels de um texto na fonte 3x5. @param s Texto. @param scale Pixels por ponto. @return Largura. */
int text_width(const char *s, int scale);

/**
 * @brief Preenche um retângulo com mistura alfa (byte alto de @p argb), recortado ao canvas.
 * @param cv Canvas. @param x,y Canto superior esquerdo (pode ser negativo). @param w,h Tamanho. @param argb Cor.
 */
void canvas_fill_rect(Canvas *cv, int x, int y, int w, int h, uint32_t argb);

/**
 * @brief Escreve texto na fonte 3x5 (avanço de 4 pontos por caractere), recortado ao canvas.
 * @param cv Canvas. @param x,y Posição do topo esquerdo. @param s Texto ASCII. @param argb Cor. @param scale Pixels por ponto (>= 1).
 */
void canvas_text(Canvas *cv, int x, int y, const char *s, uint32_t argb, int scale);

/**
 * @brief Célula do mundo sob o pixel (px, py) da tela.
 * @param v Câmera. @param w Mundo. @param px,py Pixel. @param cx,cy Célula de saída.
 * @return 1 se dentro do mundo (no toro sempre, com wrap); 0 se fora (saídas não escritas).
 */
int view_cell(const View *v, const World *w, int px, int py, int *cx, int *cy);

/**
 * @brief Multiplica o zoom mantendo fixo o ponto do mundo sob o pixel (px, py); clampado ao intervalo válido.
 * @param v Câmera. @param px,py Pixel âncora. @param factor Fator (> 1 aproxima).
 */
void view_zoom_at(View *v, int px, int py, double factor);

/** @brief Enquadra o mundo inteiro centralizado numa tela cw x ch. @param v Câmera. @param w Mundo. @param cw,ch Tela em pixels. */
void view_fit(View *v, const World *w, int cw, int ch);

/**
 * @brief Desenha o mundo pela câmera em todo o canvas (fora do mundo = quase preto).
 * @param cv Canvas. @param w Mundo. @param age Idades (pode ser NULL). @param trail Rastros (pode ser NULL).
 * @param v Câmera. @param color Paleta (PALETTE_*; 0 = monocromático). @param grid 1 = linhas de grade com zoom >= 6.
 * Estados >= 2 (Generations, morrendo) aparecem em azul que escurece.
 */
void render_world(Canvas *cv, const World *w, const uint16_t *age, const uint8_t *trail, const View *v, int color, int grid);

/**
 * @brief Minimapa do mundo inteiro com o retângulo amarelo da área visível.
 * @param cv Canvas. @param w Mundo. @param v Câmera. @param x,y Canto do minimapa. @param size Lado maior em pixels.
 */
void render_minimap(Canvas *cv, const World *w, const View *v, int x, int y, int size);

/**
 * @brief Legenda da paleta: barra de idade 1..256 (log2) com rótulos e amostra do rastro.
 * @param cv Canvas. @param x,y Posição. @param width Largura da barra em pixels. @param scale Escala da fonte. @param pal Paleta.
 */
void render_palette_legend(Canvas *cv, int x, int y, int width, int scale, int pal);

/* =====================================================================================
 * Motor volumétrico (volume.c): base da arquitetura 3D
 * ===================================================================================== */

/** @brief Tipo de vizinhança. */
typedef enum {
    NB_MOORE2D, /**< 8 vizinhos no plano (dz = 0); com d=1 equivale ao motor 2D. */
    NB_MOORE3D, /**< 26 vizinhos (cubo 3x3x3 sem o centro). */
    NB_VN3D     /**< 6 vizinhos (faces; distância de Manhattan 1). */
} NeighborhoodKind;

/** @brief Vizinhança como lista de deslocamentos. */
typedef struct {
    int n;              /**< Número de vizinhos (8, 26 ou 6). */
    int8_t off[26][3];  /**< Deslocamentos (dx, dy, dz) de cada vizinho. */
} Neighborhood;

/** @brief Regra totalística com contagens 0..26 (bit k = k vizinhos ativos). */
typedef struct {
    uint32_t birth;   /**< Contagens que fazem uma célula morta nascer. */
    uint32_t survive; /**< Contagens que mantêm uma célula viva. */
} Rule3;

/** @brief Tamanho de buffer suficiente para qualquer Rule3 em texto. */
#define RULE3_STR_MAX 160

/** @brief Grade 3D de células binárias (d=1 representa um plano). */
typedef struct {
    int w;          /**< Largura (3..1024). */
    int h;          /**< Altura (3..1024). */
    int d;          /**< Profundidade (1..1024); w*h*d <= 2^26. */
    Boundary bound; /**< Borda aplicada nos três eixos. */
    uint8_t *c;     /**< Células em ordem ((z*h)+y)*w+x; dono: volume_init/volume_free. */
} Volume;

/** @brief Ponteiro para a célula (x, y, z), sem verificação de limites. */
static inline uint8_t *volume_at(Volume *v, int x, int y, int z) {
    return &v->c[((size_t)z * (size_t)v->h + (size_t)y) * (size_t)v->w + (size_t)x];
}
/** @brief Valor da célula (x, y, z), sem verificação de limites. @return 0 ou 1. */
static inline uint8_t volume_get(const Volume *v, int x, int y, int z) {
    return v->c[((size_t)z * (size_t)v->h + (size_t)y) * (size_t)v->w + (size_t)x];
}

/**
 * @brief Monta a lista de deslocamentos de uma vizinhança.
 * @param k Tipo. @param out Vizinhança de saída. @return 0 ok; -1 tipo inválido.
 */
int neighborhood_make(NeighborhoodKind k, Neighborhood *out);

/** @brief Converte "moore2d", "moore" ou "vn". @param s Texto. @param out Saída. @return 0 ok; -1 desconhecido. */
int neighborhood_parse(const char *s, NeighborhoodKind *out);

/** @brief Nome textual da vizinhança. @param k Tipo. @return Nome ou "?". */
const char *neighborhood_name(NeighborhoodKind k);

/**
 * @brief Lê uma regra 3D: "B<lista>/S<lista>", lista = números ou faixas N-M separados por vírgula
 * (ex.: "B5-7/S4-5,9"). Com max_count <= 9 e sem ',' ou '-', aceita a forma compacta do 2D ("B3/S23").
 * @param s Texto. @param max_count Contagem máxima válida (= número de vizinhos, 1..26).
 * @param out Regra (só escrita em sucesso). @param err Mensagem (pode ser NULL). @param errlen Tamanho.
 * @return 0 ok; -1 inválida (fora de 0..max_count, faixa invertida, repetida, sintaxe).
 */
int rule3_parse(const char *s, int max_count, Rule3 *out, char *err, size_t errlen);

/** @brief Forma canônica com faixas ("B5-7/S4-5,9"). @param r Regra. @param buf Destino. @param n Tamanho (use RULE3_STR_MAX). */
void rule3_format(Rule3 r, char *buf, size_t n);

/**
 * @brief Aloca um volume vazio.
 * @param v Volume. @param w,h Largura/altura (3..1024). @param d Profundidade (1..1024). @param b Borda.
 * @return 0 ok; -1 dimensões inválidas ou sem memória.
 */
int volume_init(Volume *v, int w, int h, int d, Boundary b);

/** @brief Libera as células. @param v Volume. */
void volume_free(Volume *v);

/**
 * @brief Sopa reproduzível num cubo central (com d=1, um quadrado — mesma sequência do world_soup).
 * @param v Volume. @param seed Semente. @param density Probabilidade de vida em [0,1]. @param frac Fração central por eixo.
 */
void volume_soup(Volume *v, uint64_t seed, double density, double frac);

/** @brief Células vivas. @param v Volume. @return Contagem. */
int volume_count(const Volume *v);

/** @brief Células diferentes entre dois volumes de mesmas dimensões. @return Distância de Hamming. */
int volume_diff(const Volume *a, const Volume *b);

/** @brief Hash FNV-1a de 64 bits. @param v Volume. @return Hash. */
uint64_t volume_hash(const Volume *v);

/**
 * @brief Um passo síncrono com vizinhança arbitrária.
 * @param r Regra. @param nb Vizinhança. @param src Estado atual. @param dst Próximo estado (mesmas dimensões, distinto).
 */
void volume_step(Rule3 r, const Neighborhood *nb, const Volume *src, Volume *dst);

/**
 * @brief Rollout 3D com as métricas online do 2D (persistência, densidade, atividade, ciclo).
 * Também calcula components_final (volume_components) e com_displacement; sensitivity = -1 (não medida).
 * @param r Regra. @param nb Vizinhança. @param steps Passos (>= 1). @param init Estado inicial.
 * @param m Métricas. @param final_out Estado final (pode ser NULL). @return 0 ok; -1 erro.
 */
int rollout3(Rule3 r, const Neighborhood *nb, int steps, const Volume *init, Metrics *m, Volume *final_out);

/**
 * @brief Componentes conexos de células vivas: 26-conexos em 3D, 8-conexos com d=1 (igual ao 2D); respeita a borda.
 * @param v Volume. @return Número de componentes; -1 sem memória.
 */
int volume_components(const Volume *v);

/**
 * @brief Centro de massa (média circular por eixo no toro, resultado em [0, dim); média simples nas outras bordas).
 * @param v Volume. @param c Saída (x, y, z). @return 1 se calculado; 0 se vazio.
 */
int volume_com(const Volume *v, double c[3]);

/**
 * @brief Distância euclidiana entre dois pontos; no toro usa a imagem mínima por eixo.
 * @param v Volume (dimensões e borda). @param p,q Pontos. @return Distância em células.
 */
double volume_com_distance(const Volume *v, const double p[3], const double q[3]);

/**
 * @brief Copia o plano z para um mundo 2D (para braille/2D).
 * @param v Volume. @param z Plano em [0, d). @param out Mundo com dimensões w x h.
 * @return 0 ok; -1 se z fora do intervalo ou dimensões diferentes.
 */
int volume_slice(const Volume *v, int z, World *out);

/**
 * @brief Projeção ao longo de z (célula viva se houver alguma viva na coluna), para braille/2D.
 * @param v Volume. @param out Mundo 2D com dimensões w x h.
 */
void volume_project(const Volume *v, World *out);

/* =====================================================================================
 * Renderização 3D por software (render3d.c)
 * ===================================================================================== */

/** @brief Distância mínima à câmera para um ponto ser desenhado. */
#define CAM3_NEAR 0.1

/** @brief Câmera orbital: gira em torno de `target` a uma distância `dist`. */
typedef struct {
    double yaw;       /**< Ângulo horizontal em radianos (0 = olhando de +Z da cena). */
    double pitch;     /**< Ângulo vertical em radianos, limitado a ±89° (cam3_clamp). */
    double dist;      /**< Distância ao alvo em células (2..5000). */
    double target[3]; /**< Ponto observado, em coordenadas da cena (centro do volume = 0,0,0). */
    double fov;       /**< Campo de visão vertical em radianos. */
} Camera3;

/** @brief Enquadra o volume inteiro (alvo no centro, ângulo oblíquo). @param c Câmera. @param v Volume. */
void cam3_fit(Camera3 *c, const Volume *v);

/** @brief Limita pitch a ±89°, dist a [2, 5000] e reduz yaw a uma volta. @param c Câmera. */
void cam3_clamp(Camera3 *c);

/**
 * @brief Base ortonormal da câmera.
 * @param c Câmera. @param eye Posição do olho. @param right,up,fwd Eixos (unitários e ortogonais).
 */
void cam3_basis(const Camera3 *c, double eye[3], double right[3], double up[3], double fwd[3]);

/**
 * @brief Projeta um ponto da cena na tela (perspectiva).
 * @param c Câmera. @param cw,ch Tamanho da tela. @param p Ponto. @param sx,sy Pixel (saída). @param depth Distância ao longo do olhar.
 * @return 1 se à frente da câmera (depth > CAM3_NEAR); 0 caso contrário (saídas não escritas).
 */
int cam3_project(const Camera3 *c, int cw, int ch, const double p[3], double *sx, double *sy, double *depth);

/**
 * @brief Converte coordenada da grade (x, y, z) em coordenada da cena (centrada; y da cena para cima).
 * @param v Volume. @param x,y,z Coordenada na grade (pode ser fracionária). @param out Ponto da cena.
 */
void cam3_world(const Volume *v, double x, double y, double z, double out[3]);

/**
 * @brief Célula da camada z sob o pixel (px, py): interseção do raio da câmera com o plano do meio da camada.
 * @param c Câmera. @param v Volume. @param cw,ch Tela. @param px,py Pixel. @param z Camada. @param cx,cy Célula (saída).
 * @return 1 se o raio atinge a camada dentro do volume; 0 caso contrário (paralelo, atrás ou fora).
 */
int cam3_pick_slice(const Camera3 *c, const Volume *v, int cw, int ch, int px, int py, int z, int *cx, int *cy);

/** @brief Linha de Bresenham recortada ao canvas. @param cv Canvas. @param x0,y0,x1,y1 Extremos. @param argb Cor. */
void canvas_line(Canvas *cv, int x0, int y0, int x1, int y1, uint32_t argb);

/**
 * @brief Desenha o volume: cada célula viva vira um cubo com faces visíveis rasterizadas (z-buffer com 1/z
 * perspectivo, brilho por face, névoa por profundidade; faces internas omitidas fora do modo fatia), mais a caixa do mundo.
 * @param cv Canvas (todo sobrescrito). @param v Volume. @param age Idades para colorir (pode ser NULL).
 * @param c Câmera. @param slice -1 = volume inteiro; k >= 0 = só o plano z=k (com contorno amarelo).
 * @param color Paleta (PALETTE_*; 0 = branco).
 * @return 0 ok; -1 sem memória (z-buffer).
 */
int render_volume(Canvas *cv, const Volume *v, const uint16_t *age, const Camera3 *c, int slice, int color);

/**
 * @brief Gizmo de orientação: eixos X (vermelho), Y (verde, para baixo na grade) e Z (azul) vistos pela câmera.
 * @param cv Canvas. @param c Câmera. @param cx,cy Origem na tela. @param len Comprimento em pixels.
 * @param scale Escala da UI (espessura das linhas e tamanho dos rótulos; >= 1).
 */
void render_axes_gizmo(Canvas *cv, const Camera3 *c, int cx, int cy, int len, int scale);

/* =====================================================================================
 * Fase 5: experimento de intervenção com controle (probe.c)
 * ===================================================================================== */

/** @brief Parâmetros do experimento de intervenção. */
typedef struct {
    SimConfig sim;   /**< Grade, borda, passos (orçamento), sopa. */
    int dist;        /**< Distância em x entre o centro da fonte (3x3) e o centro do alvo (5x5); >= 4. */
    int reps;        /**< Número de sementes (condições iniciais) testadas. */
    uint64_t seed0;  /**< Primeira semente; usa seed0, seed0+1, ... */
} ProbeConfig;

/** @brief Resultado agregado do experimento. */
typedef struct {
    int reps;                  /**< Sementes testadas. */
    int detected;              /**< Sementes em que o alvo mudou dentro do orçamento. */
    int gap;                   /**< Menor chegada fisicamente possível (cone de luz). */
    int light_cone_violations; /**< Chegadas antes de `gap` (deve ser 0; > 0 indica defeito). */
    int min_arrival;           /**< Menor chegada observada; -1 se nenhuma. */
    double median_arrival;     /**< Mediana das chegadas detectadas; -1 se nenhuma. */
} ProbeResult;

/**
 * @brief Posições da fonte e do alvo e a chegada mínima possível (cone de luz, inclui atalho pelo toro).
 * @param pc Configuração. @param sx,sy Centro da fonte. @param rx,ry Centro do alvo. @param gap Chegada mínima.
 * @return 0 ok; -1 se dist < 4 ou fonte/alvo não cabem na grade.
 */
int probe_geometry(const ProbeConfig *pc, int *sx, int *sy, int *rx, int *ry, int *gap);

/**
 * @brief Executa o experimento: por semente, controle vs sinal (3x3 invertido na fonte em t=0); mede a chegada no alvo.
 * @param r Regra. @param pc Configuração. @param res Resultado agregado.
 * @param arrivals Chegada por semente (-1 = não chegou); pode ser NULL; tamanho reps.
 * @param err Mensagem. @param errlen Tamanho. @return 0 ok; -1 configuração inválida ou sem memória.
 */
int probe_run(Rule r, const ProbeConfig *pc, ProbeResult *res, int *arrivals, char *err, size_t errlen);

/**
 * @brief 1 se a região 5x5 centrada em (rx, ry) difere entre os mundos (sem verificação de limites).
 * @param a,b Mundos de mesmas dimensões. @param rx,ry Centro da região. @return 1 difere; 0 igual.
 */
int probe_region_differs(const World *a, const World *b, int rx, int ry);

/**
 * @brief Agrega chegadas por semente: detectadas, violações do cone de luz (chegada < gap), mínima e mediana.
 * @param arrivals Chegadas (-1 = não chegou). @param reps Quantidade. @param gap Chegada mínima possível. @param res Saída.
 */
void probe_tally(const int *arrivals, int reps, int gap, ProbeResult *res);

/* =====================================================================================
 * Busca 3D (search3d.c): estado retomável e checkpoint `MC3D`
 * ===================================================================================== */

/** @brief Parâmetros de uma busca 3D. */
typedef struct {
    Strategy strat;      /**< Estratégia (random, evo, map). */
    int budget;          /**< Avaliações totais (>= 1). */
    int pop;             /**< Avaliações aleatórias iniciais e tamanho da população (EVO). */
    uint64_t seed;       /**< Semente da busca. */
    uint64_t world_seed; /**< Semente da sopa inicial (igual para todos os candidatos). */
    NeighborhoodKind nb; /**< Vizinhança (define a contagem máxima das regras). */
    int w;               /**< Largura (3..1024). */
    int h;               /**< Altura (3..1024). */
    int d;               /**< Profundidade (1..1024; >= 3 para vizinhança 3D). */
    Boundary bound;      /**< Borda. */
    int steps;           /**< Passos por rollout. */
    double density;      /**< Densidade da sopa em [0,1]. */
    double soup_frac;    /**< Fração central (por eixo) ocupada pela sopa em [0,1]. */
} SearchConfig3;

/** @brief Candidato 3D avaliado (todos registrados, inclusive os ruins). */
typedef struct {
    int id;     /**< Ordem de avaliação. */
    int parent; /**< id do pai; -1 se sorteado. */
    int gen;    /**< Geração (0 = sorteado). */
    Rule3 rule; /**< Regra. */
    Metrics m;  /**< Descritores do rollout3. */
    double score; /**< rule3_score. */
    int cell;   /**< Célula do arquivo (archive_cell). */
} Candidate3;

/** @brief Estado retomável da busca 3D; o checkpoint `MC3D` é este estado serializado. */
typedef struct {
    SearchConfig3 sc;                 /**< Configuração (budget pode crescer com search3_extend). */
    uint64_t rng;                     /**< Estado do gerador. */
    int npop;                         /**< Tamanho atual da população (EVO). */
    int *pop;                         /**< Índices da população (capacidade sc.pop). */
    Candidate3 *cand;                 /**< Candidatos (capacidade sc.budget). */
    int n;                            /**< Candidatos avaliados. */
    int elite[ARCH_BINS * ARCH_BINS]; /**< Arquivo: índice da elite por célula ou -1. */
    Neighborhood nbh;                 /**< Vizinhança montada a partir de sc.nb. */
    Volume init;                      /**< Sopa inicial compartilhada. */
} SearchState3;

/** @brief Regra 3D sorteada (bits 0..maxc em cada parte). @param maxc Contagem máxima (= nº de vizinhos). @param rng Gerador. */
Rule3 rule3_random(int maxc, uint64_t *rng);

/** @brief Mutação: inverte exatamente um dos 2*(maxc+1) bits. @param r Regra. @param maxc Contagem máxima. @param rng Gerador. */
Rule3 rule3_mutate(Rule3 r, int maxc, uint64_t *rng);

/** @brief Score 3D com os mesmos filtros de ruído do 2D (B0, atividade, densidade). @param r Regra. @param m Métricas. */
double rule3_score(Rule3 r, const Metrics *m);

/** @brief Configuração padrão: map, 200 avaliações, 16³, Moore 26, toro, 60 passos, sopa 0.35 em 50%. */
SearchConfig3 search3_default_config(void);

/** @brief Valida intervalos. @param c Configuração. @param err Mensagem. @param errlen Tamanho. @return 0 ok; -1 inválida. */
int search3_config_validate(const SearchConfig3 *c, char *err, size_t errlen);

/** @brief Prepara a busca. @param st Estado (liberar com search3_state_free). @param sc Configuração. @return 0 ok; -1 erro. */
int search3_begin(SearchState3 *st, const SearchConfig3 *sc, char *err, size_t errlen);

/** @brief Avalia o próximo candidato. @return 1 avaliou; 0 orçamento esgotado; -1 erro. */
int search3_step(SearchState3 *st, char *err, size_t errlen);

/** @brief Aumenta o orçamento (ao retomar). @param st Estado. @param budget Novo total. @return 0 ok; -1 sem memória. */
int search3_extend(SearchState3 *st, int budget);

/** @brief Libera o estado (seguro após erro). @param st Estado. */
void search3_state_free(SearchState3 *st);

/** @brief Elites em ordem decrescente de score. @param st Estado. @param out ARCH_BINS² posições. @return Quantidade. */
int search3_ranked(const SearchState3 *st, int *out);

/** @brief Salva checkpoint `MC3D` (escrita atômica). @param st Estado. @param path Arquivo. @return 0 ok; -1 erro de E/S. */
int search3_save(const SearchState3 *st, const char *path);

/**
 * @brief Carrega e valida um checkpoint `MC3D` (arquivo não confiável: cabeçalho, intervalos, índices, invariantes, bytes extras).
 * @return 0 ok; -1 inválido (mensagem em err).
 */
int search3_load(SearchState3 *st, const char *path, char *err, size_t errlen);

/**
 * @brief Regra e configuração da elite de posição @p rank (0 = melhor) de um checkpoint `MC3D`.
 * @param nelites Recebe o número de elites; pode ser NULL. @return 0 ok; -1 erro.
 */
int checkpoint3_elite(const char *path, int rank, Rule3 *rule, SearchConfig3 *sc, int *nelites, char *err, size_t errlen);

#endif
