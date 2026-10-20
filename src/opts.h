/**
 * @file opts.h
 * @brief Parsing de opções de linha de comando comuns ao CLI (`microcosmos`) e ao visualizador (`mcview`).
 */
#ifndef OPTS_H
#define OPTS_H
#include "mc.h"

/**
 * @brief Converte texto decimal em inteiro dentro de um intervalo.
 * @param s Texto (não NULL). @param lo Mínimo aceito. @param hi Máximo aceito. @param out Resultado (só escrito em sucesso).
 * @return 0 ok; -1 se não numérico, com lixo no fim ou fora de [lo, hi].
 */
int parse_int(const char *s, long lo, long hi, long *out);

/**
 * @brief Converte texto decimal em uint64 (sem sinal negativo).
 * @param s Texto (não NULL). @param out Resultado (só escrito em sucesso).
 * @return 0 ok; -1 inválido.
 */
int parse_u64(const char *s, uint64_t *out);

/**
 * @brief Converte texto em double.
 * @param s Texto (não NULL). @param out Resultado (só escrito em sucesso).
 * @return 0 ok; -1 inválido.
 */
int parse_double(const char *s, double *out);

/**
 * @brief Tenta consumir uma opção de simulação em argv[*i] (--w, --h, --boundary, --steps, --density,
 * --frac, --world-seed, --no-perturb).
 * @param argc Número de argumentos. @param argv Argumentos.
 * @param i Índice atual; avança se a opção consumir um valor.
 * @param c Configuração a modificar. @param world_seed Semente da condição inicial a modificar.
 * @return 1 opção consumida; 0 não reconhecida; -1 erro (mensagem já impressa em stderr).
 */
int parse_sim_opt(int argc, char **argv, int *i, SimConfig *c, uint64_t *world_seed);

/** @brief Texto de ajuda das opções de simulação, usado nas mensagens de uso. */
#define SIM_OPTS_HELP \
    "  --w N --h N            tamanho da grade (padrão 64x64)\n" \
    "  --boundary B           torus | fixed | reflect (padrão torus)\n" \
    "  --steps N              passos por rollout (padrão 300)\n" \
    "  --density D --frac F   sopa inicial: densidade e fração central (padrão 0.5, 0.5)\n" \
    "  --world-seed N         semente da condição inicial (padrão 1)\n" \
    "  --no-perturb           não mede sensibilidade a perturbação\n"
#endif
