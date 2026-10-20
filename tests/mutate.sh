#!/usr/bin/env bash
# mutate.sh — mutation testing manual: aplica uma mutação realista por vez numa cópia de src/
# e verifica se a suíte de testes falha. Mutante sobrevivente deve ser explicado no README.
set -u
cd "$(dirname "$0")/.."
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
# arquivo|expressão sed (cada uma deve alterar o arquivo)
# Removido por ser EQUIVALENTE: tirar o descarte de faces de costas em render3d.c (o z-buffer esconde as
# faces traseiras de um cubo convexo; a imagem é idêntica, só fica mais lento).
MUTANTS=(
  "rules.c|s/if (d > 8)/if (d > 9)/"
  "rules.c|s/if (\*mask \& (1u << d))/if (0)/"
  "rules.c|s/if (\*p) { snprintf/if (0) { snprintf/"
  "rules.c|s/rng_below(rng, 18)/rng_below(rng, 9)/"
  "rules.c|s/r.survive ^= (uint16_t)(1u << (bit - 9))/r.survive ^= (uint16_t)(1u << (bit - 8))/"
  "sim.c|s/(i == 1 \&\& j == 1)/(0)/"
  "sim.c|s/if (cur == 0) next = (uint8_t)((r.birth >> n) \& 1u);/if (cur == 0) next = (uint8_t)((r.survive >> n) \& 1u);/"
  "sim.c|s/if (persisting \&\& alive > 0 \&\& changed > 0)/if (persisting \&\& changed > 0)/"
  "sim.c|s/m->cycle_period = t - s;/m->cycle_period = t - s + 1;/"
  "sim.c|s/dens_sum += (double)(T - t) \* alive \/ N;/;/"
  "sim.c|s/if (changed == 0) {/if (changed == 0 \&\& alive == 0) {/"
  "sim.c|s/world_perturb_center(\&b);/;/"
  "mc.h|s/return ((i % n) + n) % n;/return i < 0 ? 0 : n - 1;/"
  "mc.h|s/return i < 0 ? 0 : n - 1;/return -1;/"
  "metrics.c|s/if (dx > w->w - dx) dx = w->w - dx;/;/"
  "metrics.c|s/for (int dy = -1; dy <= 1; dy++)/for (int dy = 0; dy <= 1; dy++)/"
  "metrics.c|s/if (m < 0) m += n;/;/"
  "search.c|s/if (e < 0 || all\[idx\].score > all\[e\].score)/if (e < 0 || all[idx].score >= all[e].score)/"
  "search.c|s/double c = m->components_final \/ 16.0;/double c = m->components_final \/ 8.0;/"
  "search.c|s/return bin(m->activity_mean \/ ARCH_ACT_MAX) \* ARCH_BINS + bin(p);/return bin(p) * ARCH_BINS + bin(m->activity_mean \/ ARCH_ACT_MAX);/"
  "search.c|s/if (b >= ARCH_BINS) b = ARCH_BINS - 1;/;/"
  "search.c|s/c->gen = parent < 0 ? 0 : C\[parent\].gen + 1;/c->gen = 0;/"
  "search.c|s/n % 2 ? v\[n \/ 2\]/n % 2 == 0 ? v[n \/ 2]/"
  "search.c|s/if (r.birth \& 1u) return 0.0;/;/"
  "search.c|s/if (m->activity_mean >= ARCH_ACT_MAX) return 0.0;/;/"
  "search.c|s/if (m->density_mean >= SCORE_DENS_MAX) return 0.0;/;/"
  "render.c|s/{ 0x01, 0x08 }, { 0x02, 0x10 }/{ 0x01, 0x02 }, { 0x08, 0x10 }/"
  "render.c|s/trail\[i\] = age\[i\] ? 255 : (uint8_t)(trail\[i\] \* 7 \/ 8);/trail[i] = age[i] ? 255 : 0;/"
  "render.c|s/if (age\[i\] < UINT16_MAX) age\[i\]++;/age[i]++;/"
  "render.c|s/(int)(0xA0 | (b >> 6))/(int)(0xA0 | (b >> 5))/"
  "search.c|s/    st->rng = rng;/    ;/"
  "search.c|s/fgetc(f) == EOF/1/"
  "search.c|s/ok = c->id == i \&\& c->parent >= -1 \&\& c->parent < i/ok = c->id == i/"
  "render.c|s/double wx = v->x + px \/ v->zoom, wy/double wx = v->x, wy/"
  "render.c|s/x = ((x % w->w) + w->w) % w->w;/x = x % w->w;/"
  "render.c|s/out |= ((s \* a + t \* (255 - a)) \/ 255) << sh;/out |= s << sh;/"
  "render.c|s/(4 \* n - 1) \* scale/4 * n * scale/"
  "render.c|s/v->x = (w->w - cw \/ v->zoom) \/ 2.0;/v->x = 0;/"
  "volume.c|s/int keep = (k == NB_MOORE2D \&\& dz == 0)/int keep = (k == NB_MOORE2D)/"
  "volume.c|s/if (a > maxc || b > maxc || b < a)/if (a > maxc || b < a)/"
  "volume.c|s/while (b + 1 <= 26 \&\& (m \& (1u << (b + 1)))) b++;/;/"
  "volume.c|s/if (nx >= 0 \&\& ny >= 0 \&\& nz >= 0)/if (nx >= 0 \&\& ny >= 0)/"
  "volume.c|s/for (int z = 0; z < v->d \&\& !any; z++)/for (int z = 0; z < 1 \&\& !any; z++)/"
  "volume.c|s/neighborhood_make(v->d == 1 ? NB_MOORE2D : NB_MOORE3D, \&nb);/neighborhood_make(NB_VN3D, \&nb);/"
  "volume.c|s/if (v->bound == BOUND_TORUS \&\& d > dims\[a\] - d) d = dims\[a\] - d;/;/"
  "render3d.c|s/\*sy = ch \/ 2.0 - foc/*sy = ch \/ 2.0 + foc/"
  "render3d.c|s/if (z <= CAM3_NEAR) return 0;/if (z <= -1e9) return 0;/"
  "render3d.c|s/if (z >= zb\[q\]) continue;/;/"
  "render3d.c|s/int gx = (int)floor(sx + v->w \/ 2.0), gy = (int)floor(-sy + v->h \/ 2.0);/int gx = (int)floor(sx + v->w \/ 2.0), gy = (int)floor(sy + v->h \/ 2.0);/"
  "render3d.c|s/if (slice >= 0 \&\& z != slice) continue;/;/"
  "render3d.c|s/if (c->pitch > lim) c->pitch = lim;/;/"
  "render3d.c|s/cross3(fwd, world_up, right);/cross3(world_up, fwd, right);/"
  "probe.c|s/\*gap = d - 3;/*gap = d - 2;/"
  "probe.c|s/if (c->bound == BOUND_TORUS \&\& c->w - d < d) d = c->w - d;/;/"
  "probe.c|s/for (int y = ry - 2; y <= ry + 2; y++)/for (int y = ry; y <= ry; y++)/"
  "probe.c|s/if (a < gap) res->light_cone_violations++;/;/"
  "render.c|s/if (s > UI_SCALE_MAX) s = UI_SCALE_MAX;/;/"
  "render.c|s/int a = cw \/ 480, b = ch \/ 320/int a = cw \/ 320, b = ch \/ 320/"
  "sim.c|s/else next = cur + 1 >= states ? 0 : (uint8_t)(cur + 1);/else next = 0;/"
  "sim.c|s/n += world_get(src, xs\[i\], ys\[j\]) == 1;/n += world_get(src, xs[i], ys[j]) != 0;/"
  "rules.c|s/v < 2 || v > 255/v < 1 || v > 255/"
  "search3d.c|s/if (bit <= (uint32_t)maxc) r.birth ^= 1u << bit;/if (bit < (uint32_t)maxc) r.birth ^= 1u << bit;/"
  "search3d.c|s/if (e < 0 || c->score > C\[e\].score) st->elite\[c->cell\] = i;/if (e < 0) st->elite[c->cell] = i;/"
  "search3d.c|s/    st->rng = rng;/    ;/"
  "search3d.c|s/fgetc(f) == EOF/1/"
  "search3d.c|s/if (m->density_mean >= SCORE_DENS_MAX) return 0.0;/;/"
  "search3d.c|s/!(c->rule.birth \& ~m) \&\& !(c->rule.survive \& ~m)/1/"
  "world.c|s/int ox = (w->w - fw) \/ 2/int ox = 0/"
)
killed=0; survived=0; broken=0
for m in "${MUTANTS[@]}"; do
  f=${m%%|*}; expr=${m#*|}
  rm -rf "$WORK/src" "$WORK/tests"; cp -r src tests "$WORK/"
  sed -i "$expr" "$WORK/src/$f"
  if cmp -s "src/$f" "$WORK/src/$f"; then echo "NÃO APLICADA: $f: $expr"; broken=$((broken+1)); continue; fi
  if ! gcc -std=c11 -D_DEFAULT_SOURCE -O1 -w -fsanitize=address,undefined -fno-sanitize-recover=all "$WORK"/src/{rules,world,sim,metrics,search,export,render,volume,render3d,probe,search3d}.c "$WORK/tests/test_main.c" -lm -o "$WORK/t" 2>/dev/null; then
    echo "NÃO COMPILA: $f: $expr"; broken=$((broken+1)); continue; fi
  if timeout 60 "$WORK/t" >/dev/null 2>&1; then echo "SOBREVIVEU: $f: $expr"; survived=$((survived+1))
  else killed=$((killed+1)); fi
done
echo "mutantes: total=${#MUTANTS[@]} detectados=$killed sobreviventes=$survived inválidos=$broken"
