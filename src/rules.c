/**
 * @file rules.c
 * @brief representação, validação e mutação de regras B/S.
 */
#include "mc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Lê uma parte da regra ("B..." ou "S...") e avança o cursor.
 * @param p Cursor no texto (avança). @param mask Máscara de saída. @param tag 'B' ou 'S'.
 * @param s Texto completo (para a mensagem). @param err Mensagem de erro. @param n Tamanho de @p err.
 * @return 0 ok; -1 inválida.
 */
static int parse_set(const char **p, uint16_t *mask, char tag, const char *s, char *err, size_t n) {
    if (toupper((unsigned char)**p) != tag) {
        snprintf(err, n, "regra \"%s\": esperado '%c'", s, tag);
        return -1;
    }
    (*p)++;
    *mask = 0;
    while (**p >= '0' && **p <= '9') {
        int d = **p - '0';
        if (d > 8) {
            snprintf(err, n, "regra \"%s\": contagem %d fora de 0..8", s, d);
            return -1;
        }
        if (*mask & (1u << d)) {
            snprintf(err, n, "regra \"%s\": dígito %d repetido em %c", s, d, tag);
            return -1;
        }
        *mask |= (uint16_t)(1u << d);
        (*p)++;
    }
    return 0;
}

int rule_parse(const char *s, Rule *out, char *err, size_t n) {
    char dummy[1];
    if (!err || n == 0) { err = dummy; n = sizeof dummy; }
    if (!s || !out) { snprintf(err, n, "argumento nulo"); return -1; }
    const char *p = s;
    Rule r;
    if (parse_set(&p, &r.birth, 'B', s, err, n)) return -1;
    if (*p != '/') { snprintf(err, n, "regra \"%s\": esperado '/' após a parte B", s); return -1; }
    p++;
    if (parse_set(&p, &r.survive, 'S', s, err, n)) return -1;
    if (*p) { snprintf(err, n, "regra \"%s\": caractere inesperado '%c'", s, *p); return -1; }
    *out = r;
    return 0;
}

int rule_parse_gen(const char *s, Rule *out, int *states, char *err, size_t n) {
    char dummy[1], base[64];
    if (!err || n == 0) { err = dummy; n = sizeof dummy; }
    if (!s || !out || !states) { snprintf(err, n, "argumento nulo"); return -1; }
    const char *c = strrchr(s, '/');
    int st = 2;
    if (c && (c[1] == 'C' || c[1] == 'c')) { /* sufixo /C<n> */
        char *end;
        long v = strtol(c + 2, &end, 10);
        if (c[2] < '0' || c[2] > '9' || *end || v < 2 || v > 255) {
            snprintf(err, n, "regra \"%s\": número de estados /C deve estar em 2..255", s);
            return -1;
        }
        size_t len = (size_t)(c - s);
        if (len >= sizeof base) { snprintf(err, n, "regra \"%s\": longa demais", s); return -1; }
        memcpy(base, s, len);
        base[len] = '\0';
        st = (int)v;
        s = base;
    }
    if (rule_parse(s, out, err, n)) return -1;
    *states = st;
    return 0;
}

void rule_format_gen(Rule r, int states, char *buf, size_t n) {
    char b[RULE_STR_MAX];
    rule_format(r, b, sizeof b);
    if (states > 2) snprintf(buf, n, "%s/C%d", b, states);
    else snprintf(buf, n, "%s", b);
}

void rule_format(Rule r, char *buf, size_t n) {
    char tmp[RULE_STR_MAX];
    int k = 0;
    tmp[k++] = 'B';
    for (int d = 0; d <= 8; d++) if (r.birth & (1u << d)) tmp[k++] = (char)('0' + d);
    tmp[k++] = '/';
    tmp[k++] = 'S';
    for (int d = 0; d <= 8; d++) if (r.survive & (1u << d)) tmp[k++] = (char)('0' + d);
    tmp[k] = '\0';
    snprintf(buf, n, "%s", tmp);
}

int rule_equal(Rule a, Rule b) {
    return (a.birth & RULE_MASK) == (b.birth & RULE_MASK) && (a.survive & RULE_MASK) == (b.survive & RULE_MASK);
}

Rule rule_random(uint64_t *rng) {
    uint64_t x = rng_next(rng);
    Rule r = { (uint16_t)(x & RULE_MASK), (uint16_t)((x >> 9) & RULE_MASK) };
    return r;
}

Rule rule_mutate(Rule r, uint64_t *rng) {
    uint32_t bit = rng_below(rng, 18);
    if (bit < 9) r.birth ^= (uint16_t)(1u << bit);
    else r.survive ^= (uint16_t)(1u << (bit - 9));
    return r;
}
