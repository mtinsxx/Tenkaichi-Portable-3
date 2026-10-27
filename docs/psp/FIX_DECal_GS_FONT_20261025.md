# PSP GS — fonte cinza no menu do Memory Card: TEX0.TFX=DECAL

## Evidência visual
Em 25/10/2026 o usuário comparou a tela
**MEMORY CARD slot 1 / Loading data** do EBOOT PSP no PPSSPP Android
com um vídeo de BT3 no PS2. Molduras e fundo aparecem, mas as letras
no port ficaram acinzentadas; no original estão significativamente
mais claras. A PR #20 (`ALPHA.FIX`) **não corrigiu** a tela.
A captura por si só não revela o TEX0 de cada glifo, então não
atribuir falsamente toda a diferença ao bug a seguir antes do teste.

## Bug real verificado no código (independente das capturas)

Referência original `port/src/gs/gs_core.c`, `shade()`:
para `TEX0.TFX=DECAL (1)`, **RGB final = RGB da textura**.
`TEX0.TCC=RGB` mantém o alfa do vértice;
`TCC=RGBA` usa alfa da textura.

Na rotina PSP `psp_gu.c`, `TFX=1` era convertido
para `GU_TFX_DECAL`. Contudo, o PSPSDK documenta:
- `GU_TFX_DECAL, GU_TCC_RGBA`:
  `RGB = fragment*(1-textureAlpha) + texture*textureAlpha`;
- `GU_TFX_REPLACE, GU_TCC_RGBA`:
  `RGB = texture`, `Alpha = textureAlpha`;
- `GU_TFX_REPLACE, GU_TCC_RGB`:
  `RGB = texture`, `Alpha = fragmentAlpha`.

Logo `GU_TFX_REPLACE` é o mapeamento correto de **ambos**
os modos de componente do DECAL de PS2. Com texturas de fontes
semitransparentes, o antigo DECAL podia reduzir o branco usando
a cor cinza do vértice **antes** da mistura da fonte com o fundo.

## O que mudou

- `gs_experiments.h`: helper puro `Psp_GsTexFuncToGu`,
  encaminhando `TFX=0` à modulação, `TFX=1` à
  substituição do texel e mantendo os demais modos
  experimentais no caminho legado.
- `psp_gu.c`: o estado de textura usa o helper.
  Não altera a cor de todos os textos ou força RGB=255.
- `psp_gu.c`: acrescenta contador agregado a cada relatório:
  `psp: GS sprite DECAL: RGBA=... RGB=...`.
- `test_gs_decal.c`: percorre texturas, cores e alfa, testa
  igualdade de RGB e da composição posterior com o fundo;
  verifica o erro do `GU_TFX_DECAL` para tinta branca
  semitransparente.

## Como validar no Android sem bt3.txt

1. Guarde o EBOOT da PR #20 (ainda cinza).
2. Instale o EBOOT desta PR e abra **o mesmo** menu Memory Card.
3. Compare branco das letras, moldura e fundo azul. Se
   `GS sprite DECAL: RGBA` for maior que zero no log desse
   menu, a correção foi exercitada com texturas e alfa.
4. Se permanecer cinza e `RGBA=0`, buscar outro caminho:
   `GS sprite FIX`, modulação, CLUT/CSA, resultado do
   `Flash_ClipGetAlpha` ou os modos especiais TFX 2 e 3,
   em vez de afirmar que DECAL explicou o problema.
5. Não comparar FPS em cenas diferentes; esta troca de função
   de textura não deve criar trabalho CPU adicional por pixel.

A PR é **experimental**. CI valida a fórmula, mas não é uma
captura real do menu no PPSSPP. Não modifica a `main`
funcional `fe93677`, heap, VU1 ou o sistema de arquivos,
nem adiciona dados do jogo.
