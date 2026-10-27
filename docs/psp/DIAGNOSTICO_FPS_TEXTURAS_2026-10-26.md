# BT3-PSP — Diagnóstico: FPS baixo em todo o jogo + "texturas desse jeito" (26/10/2026)

> **Revisão de base (26/10, à noite):** a primeira versão deste documento foi escrita
> contra a `main` (`fe93677`) e produziu um EBOOT sem a pilha de PRs draft que é a
> "versão atual" de teste (#3→#23 + #26, `perf/psp-frame-rate-movie-clut-20261025`,
> `a09d315`) — daí os "bugs de versão antiga" relatados. Este trabalho foi
> **refeito sobre a ponta do PR #26**. O que muda em relação ao texto abaixo:
> (1) a pilha **já tinha** o experimento booleano `gs_depth` (default `0`); ele foi
> estendido — não duplicado — com o modo `2` (só-alvo-de-tela) e os contadores de Z;
> (2) a análise de VU1 100% interpretado e a lista de passes descartados (FB16 etc.)
> valem pra `main`: na versão atual já existem VU1 nativo de lutadores/cenário
> (`native_fighter=1`, `native_stage4=1`), FB16 (`gs_fb16=1`) e `tex_limit=256` por
> padrão, então remeça o log antes de ranquear gargalos; (3) o diagnóstico central
> **permanece**: com `gs_depth=0` (o padrão em ambas) nenhum depth test é aplicado.


Diagnóstico a partir das 3 screenshots do PPSSPP enviadas pelo usuário
(`Screenshot_20261025_220656`, `_220659`, `_220723`) e de auditoria do código
na base `fe93677` (`port/psp/psp_gu.c`, `port/psp/psp_gfx.c`, `port/psp/psp_main.c`,
`port/src/gs/gs_core.c`, `port/src/gs/gs_gpu.c`, `port/src/gs/gs_vu1.c`,
`port/psp/build.py`), cruzando com as medições já registradas em
[`COMPARACAO_VITA_PSP_FPS.md`](COMPARACAO_VITA_PSP_FPS.md).

**Resumo em uma frase:** as texturas em si estão majoritariamente *certas* — o que
está errado é a **oclusão** (o renderer do PSP não faz nenhum teste de profundidade,
então o modelo se desenha "do avesso", por ordem de submissão) e o **cenário que não
chega à tela** (passes descartados / um único buffer auxiliar); e o FPS é 1–3 quadros
apresentados por segundo porque o custo está quase todo na **CPU interpretando
VIF/VU1/GS instrução por instrução**, não na GPU do PSP.

---

## 1. Como ler as screenshots (e o contador verde do PPSSPP)

O contador do PPSSPP é `actual_fps / flips (vps%)` (`UI/DebugOverlay.cpp`,
`DrawFPS`): o **primeiro número é quantos quadros foram realmente apresentados
(flip) por segundo**; a porcentagem é *vblanks virtuais do emulador* ÷ 59,94.
Nas três fotos:

| Screenshot | Contador | Leitura |
|---|---|---|
| `_220656` (intro, Goku de perfil) | `2/1 (779.6%)` | **2 quadros apresentados/s** |
| `_220659` (close, Goku "do avesso") | `3/1 (862.8%)` | **3 quadros apresentados/s** |
| `_220723` (batalha, HUD visível) | `1/1 (100.1%)` | **1 quadro apresentado/s** |

Os `779%/862%` **não** significam "rápido": como o port não espera o vblank do
display (o relógio do jogo é o `sceKernelDelayThread` de `Psp_VBlankWait`, e
`swap_vsync=0` por padrão), o relógio virtual do PPSSPP avança desacoplado do
flip — a % é inútil pra este homebrew. O número que importa são os **1–3 FPS
reais**, consistentes com o `log(6).txt` anterior (~3 FPS, `GSchain`
2000–2244 ms por janela de ~2 s).

**O que está certo nas fotos** — e isso elimina várias hipóteses:

- O HUD (retratos, barras, timer) desenha perfeito → caminho de sprites 2D,
  CLUT, alpha e UV de sprites funcionam.
- A textura do kimono do Goku (cores, emblema, faixa, botas) está mapeada no
  lugar certo → decodificação de textura de personagem, swizzle e coordenadas
  ST/UV dos triângulos funcionam.
- O oponente aparece bem pequeno ao fundo, com perspectiva correta → a
  projeção dos microprogramas VU1 (interpretados) está produzindo posição e
  escala corretas.

Ou seja: **não é "textura corrompida"**. São três problemas distintos:

1. **Oclusão** — faces de trás pintando sobre as da frente (as 3 fotos; extremo na `_220659`).
2. **Cenário preto** — stage simplesmente ausente nas 3 fotos.
3. **Velocidade** — 1–3 FPS apresentados em qualquer tela.

---

## 2. Causa nº 1 das "texturas desse jeito": NÃO há teste de profundidade (Z)

**Certeza: confirmado no código.**

Em `port/psp/psp_gu.c`, `GsGpu_Draw()` calcula `n.ztest/zfunc/zwrite` a partir
dos registradores `TEST`/`ZBUF` do GS — e `state_apply()` simplesmente os
**ignora**:

```c
/* estado anterior */
if (first) {
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(0xFFFF);   /* != 0 => escrita de Z desligada (pspsdk) */
}
```

Isso foi uma baseline deliberada ("tests are deliberately disabled while we
isolate remaining GS visibility issues"), mas é ela que produz exatamente o
sintoma das fotos:

- O PS2 GS não faz backface culling; o jogo manda os triângulos dos dois lados
  do modelo e **quem decide o que aparece é o teste de profundidade**. Sem
  ele, vale a ordem de submissão ("painter's order"): a manga de trás sobre o
  peito, o interior do kimono sobre o tronco, o cabelo sobre o rosto — o
  "avesso" da foto `_220659`.
- Os caminhos de referência fazem o teste: o rasterizador de software em
  `gs_core.c: pixel()` compara Z pixel a pixel (`GEQUAL`/`GREATER` contra a
  página `ZBP`), e o backend GPU em `gs_gpu.c` (`pipeline_get`/`pipeline_create`)
  liga `enable_depth_test` sempre, com compare derivado de `TEST.ZTST` e
  escrita derivada de `ZBUF.ZMSK`. O PSP é o único dos três que não testa.
- O HUD não sofre porque sprites 2D chegam já ordenados e quase sempre com
  `ZTE=0/ALWAYS` — de novo, bate com as fotos.

Detalhes que tornam "só ligar o teste" insuficiente (e que o teste A/B abaixo
foi desenhado pra revelar):

- **Um único buffer de profundidade** (`DEPTH_OFF`, 512×272×2) é compartilhado
  entre o framebuffer da tela (double-buffer) e o alvo auxiliar `WORK`. No PS2
  cada alvo tem seu `ZBP`; aqui, passes no `WORK` poluiriam a profundidade da
  cena principal.
- A limpeza de Z do jogo no PS2 é *desenhar no framebuffer que é a memória do
  Z* — passes que hoje são descartados (`fbp == zbp`, §3). O backend do PSP
  compensa limpando cor+profundidade no `GsGpu_FrameEnd()` da tela, só.
- O PSP tem profundidade de **16 bits**; o GS usa Z de 24 bits. A conversão
  atual (`(z >> 8) / 257 * 65535`) colapsa valores próximos — o contorno preto
  dos lutadores é desenhado quase na mesma profundidade do corpo (comentário
  em `gs_core.c: draw_triangle`), então com `ZTST=GREATER` ele é o primeiro
  candidato a "salpicar". Isso afeta o detalhe fino, não a oclusão grossa.

### Correção introduzida junto com este diagnóstico (teste A/B, padrão inalterado)

Nova opção `depth=` → **incorporada ao setting que já existe na pilha (revisão de 26/10, à noite): `gs_depth=`** em `ms0:/PSP/GAME/BT3/bt3.txt`, estendido a três valores sem mudar o significado antigo de `=1`:

| Valor | Comportamento |
|---|---|
| `gs_depth=0` (padrão) | Baseline: nenhum depth test — ordem de submissão decide visibilidade |
| `gs_depth=1` | Experimento original da pilha (PR #5/#9): `TEST.ZTE/ZTST` + `ZBUF.ZMSK` em **todos** os targets |
| `gs_depth=2` **(novo)** | O mesmo mapeamento, **só no target apresentado**: passes WORK não são testados/escritos, pra não poluir o Z da cena principal |

E contadores novos no `log.txt` a cada ~2 s (`report`):

```
psp: GS depth: mode=0 triangles=... (work=..., ZTE=..., Zwrite=..., flat-Z=...) ZTST=a/b/c/d z-range=xxxxxxxx..xxxxxxxx sprites=... (ZTE=...)
```

**Protocolo de validação:** build nova → `report=1` + `gs_depth=0` (foto+log) →
`gs_depth=1` (foto+log) → `gs_depth=2` (foto+log), mesma luta/câmera. Se a hipótese
está certa, em `gs_depth=2` o Goku para de se atravessar sem que passes WORK poluam o Z. Se `z-range` vier
`min==max` ou quase (Z constante), o problema não é o backend e sim o Z que
chega do interpretador VU1 — os contadores `flat-Z`/`ZTST` dizem isso.
*Nota de honestidade: sem dados do disco neste ambiente, o modo novo foi
conferido por sintaxe contra os headers reais do pspsdk, não rodando o jogo.
Por isso o padrão continua sendo o comportamento já testado.*

---

## 3. Causa nº 2: o cenário preto (passes descartados + 1 buffer auxiliar)

**Certeza: múltiplos mecanismos confirmados no código; qual deles domina no
stage das fotos exige o `log.txt` daquela sessão** (contadores já existem:
`screen/work`, `skipped_fb16`, `skipped_depth`, `skipped_special_texture`,
`missing_texture`, `fb-alias`).

`GsGpu_Draw()` retorna sem desenhar (`left out`) quando:

1. `FRAME` é uma vista **16 bits** de um framebuffer (`sDiagSkip16`);
2. o passe desenha **na memória do Z** (`fbp == zbp`, `sDiagSkipDepth`) — é
   assim que o jogo limpa Z e constrói os dados de efeito no PS2;
3. a textura é o **Z lido como textura** / PSM especiais (`0x1B/0x24/0x2C/0x3x`,
   `sDiagSkipPsm`) — tint/fog de profundidade, sombras, glow;
4. **não há memória pra textura** (`texture_get() == NULL`, `sDiagSkipTexMem`) —
   e no log antigo `alloc-failed` chegou a **848** e `left out` a **6438**;
5. não há alvo disponível (raro).

Somam-se duas limitações estruturais da VRAM de 2 MB:

- **Só 2 alvos físicos**: tela (double-buffer 2×272 KB) + **um** `WORK`
  (272 KB) + 1 Z (272 KB) = 1088 KB; sobram **960 KB** pra texturas residentes
  em VRAM. Todos os FBPs do GS diferentes de `gGsMainFbp` caem no mesmo
  `WORK` (`target_color()`), sobrescrevendo uns aos outros no mesmo quadro;
  quando o jogo depois amostra "o framebuffer X como textura", o conteúdo já
  pode ser de outro passe (mitigado parcialmente pelo controle
  `sWorkOwnerFbp`/`rendered`, que então faz fallback pra decodificar textura).
- O céu/fundo de stage em BT3 passa justamente por esse caminho de efeitos
  (work buffers + leitura de Z). Sem profundidade (§2), um sprite
  fullscreen escuro de efeito também pode simplesmente **cobrir** o stage já
  desenhado — nas fotos os lutadores aparecem porque são desenhados *depois*
  desse passe. Os contadores `drawMain/drawWork` + `fb-alias fallback` do
  próximo log distinguem "stage descartado" de "stage coberto".

Para além disso, `texture_get()` recusa texturas acima de 512×512 (corta em
512) e só conhece wrap global (`GU_CLAMP`/`GU_REPEAT`): o `CLAMP` regional
(modos 2/3 do GS) é ignorado. Isso deforma bordas de atlas em alguns sprites,
mas **não** explica modelo do avesso nem stage inteiro preto.

---

## 4. FPS: onde o tempo vai (é CPU, não é GPU)

Medição anterior ([`COMPARACAO_VITA_PSP_FPS.md`](COMPARACAO_VITA_PSP_FPS.md),
janela de ~2 s na tela lenta): `GSchain ≈ 2000–2244 ms` (≈100% do tempo de
parede dentro da cadeia GS), `GUfinishSync ≈ 0–1 ms`, `gameCPU ≈ 3–9 ms`.
Ou seja, a GE do PSP termina quase instantaneamente o pouco que recebe; a
CPU (Allegrex 333 MHz, um núcleo) é quem não dá conta de *produzir* quadros.
Ordem provável dos custos, do código:

### 4.1 Interpretador VU1 — o custo nº 1

`Psp_GsGetenv()` força `BT3_VU_INTERP=1`: todo microprograma VU1 (modelos
dos lutadores, stage, efeitos) roda instrução por instrução, escalar, com
semântica de float do PS2, dentro de `gs_vu1.c`. Nos ports de PC/Vita os
programas conhecidos (127 instruções dos lutadores, 399, 451, 94/104, 99/101,
124) são desviados para shaders (`GsGpu_DrawVu0/4/6`); no PSP essas funções
são stubs vazios porque a GE não tem vertex shader programável equivalente —
resta o interpretador integral. `port_vufast.c` (equivalentes rápidos das
funções nativas VU0) também é explicitamente **excluído** do build do PSP
(`build.py`: assembly ARM/x86 inline). É o que o README e
[`development.md`](development.md) já apontam ("the first thing to replace
for speed"). Os contadores `VIF/VU1: estimated-VU1` do `Psp_GuStats`
(cronometragem 1×16 chamadas) quantificam por tela.

### 4.2 Churn de texturas sob pressão de memória

- VRAM de texturas (960 KB) é alocador *bump* (`tex_alloc` só anda pra frente);
  despejar entrada de VRAM só esquece o descritor, não devolve bytes. Encheu,
  vai pra RAM (cache máx. 10 MB, `MAX_RAM_TEX_BYTES`).
- Cada *miss* decodifica a textura inteira pixel a pixel (`Gs_VramReadRect`/
  `ReadIdx8` + `Gs_Expand` + swizzle) com um `malloc` de `tw*th*4` (512×512
  = 1 MB temporário) alocado e liberado **por textura**.
- Sob pressão, `tex_evict_one()` faz `flush + sceGuFinish + sceGuSync`
  (drenar a GE inteira) **por textura removida**, em loop até o `malloc`
  passar — e queda de `malloc` significa redesenhar/re-decodificar a mesma
  textura na próxima chamada. Com `alloc-failed=848` e pico de RAM ~8 MB no
  log antigo, é aqui que nascem os *spikes* de centenas de ms que derrubam
  uma tela já lenta pra 1–3 FPS.
- Toda consulta reconstrói a chave com hash de conteúdo das páginas GS +
  hash do CLUT; na amostragem existente, `key+CLUT+lookup` extrapolado (×32)
  também entra na conta em telas com milhares de draws.

### 4.3 Fragmentação e sincronização por quadro

- `GsGpu_Draw` chega **por primitiva** (um triângulo por vez no caminho
  interpretado) e qualquer mudança de estado fecha o batch
  (`sceKernelDcacheWritebackRange` + `sceGuDrawArray` por batch).
  *Corrigido neste diagnóstico*: em `gs_depth=0`, toggles só de `ZTE/ZMSK`
  fechavam batch sem mudar nada na tela (os campos eram ignorados) — agora
  são normalizados pra zero nesse modo. Ganho pequeno, imagem idêntica.
- `GsGpu_FrameEnd()` faz `sceGuFinish + sceGuSync` todo quadro (CPU espera a
  GE ficar ociosa): medido ~0–1 ms, então **não** é o gargalo — é só estrutura.
- Frameskip (`gVitaGsSkip`) **não está ligado** no PSP (ninguém chama
  `Vita_SkipWanted`); e mesmo ligado, pular a *apresentação* não economiza o
  custo dominante, que é interpretar VIF/VU1 pra gerar os comandos — só
  ajuda se pular a cadeia GS inteira daquele quadro (o caminho do Vita),
  com risco visual conhecido.

### 4.4 O que NÃO é o gargalo

- Fill-rate/GE do PSP (`GUfinishSync ≈ 0–1 ms`). A falta de Z aumenta
  overdraw, mas a GE ainda está ociosa perto da CPU.
- Lógica do jogo (`gameCPU ≈ 3–9 ms`/2 s).
- Movie MPEG-2 (≈94 ms/quadro + 8 ms de conversão) pesa só em abertura/ending.
- Flags já estão razoáveis: jogo `-O2`, `gs_core.c`/`gs_vu1.c` `-O3`,
  333 MHz sem overclock — ganho real aqui exige caminho nativo pros
  microprogramas mais usados (HLE do programa 127 = lutadores primeiro),
  não troca de `-O`.

---

## 5. Resposta direta às duas perguntas

**"Por que FPS baixo em todo o jogo?"** Porque cada quadro custa 300–1000 ms
de CPU interpretando VU1/GS + decodificando textura onde faltou memória, e
isso vale pra menu, intro e luta — a GPU está praticamente ociosa. Nas
screenshots o FPS *real* é o primeiro número verde: **2, 3 e 1**.

**"Por que as texturas estão desse jeito?"** As texturas não estão
corrompidas; quem está faltando é profundidade: sem Z, o modelo pinta na
ordem que chega e aparece "do avesso". O cenário preto é conjunto de passes
que hoje não são desenhados (vistas 16 bits, Z-como-textura, efeitos em work
buffer único e textura sem memória), possivelmente cobertos ainda por
efeitos fullscreen que no PS2 perderiam no teste de profundidade.

## 6. Próximo passo pedido ao usuário (5 minutos)

1. Pegar o EBOOT deste build; em `ms0:/PSP/GAME/BT3/bt3.txt`:
   `report=1` e `gs_depth=2`.
2. Reproduzir ~20 s da mesma luta da foto `_220723` e da intro `_220656`,
   tirar as mesmas screenshots.
3. Enviar os dois `log.txt` (basta as linhas `psp: GS depth:`, `psp: GS menu
   passes:`, `psp: PROFILE`, `psp: VIF/VU1:`, `psp: texture cache:`) + fotos.
4. Se `gs_depth=2` resolver o Goku, repetir com `gs_depth=1` pra comparar
   efeitos/sombras; se o stage continuar preto nos dois, os contadores
   `skipped_*`/`drawWork` apontam qual passe falta implementar em seguida.

Isso separa, com dado medido, "ligar Z de vez (e tratar 16 bits/CLEAR do
jogo)" de "Z chegando errado do VU1" — antes de qualquer reescrita maior
(HLE de VU1 ou segundo buffer de profundidade/alvos).
