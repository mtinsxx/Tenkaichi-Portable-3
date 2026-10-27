# Tradução nativa VU1 — primeiro lote da geometria do cenário (programa nº 4)

Base: `port/psp-native-vif-subword-20261025` (PR #15).
A `main` e a versão funcional `fe93677` **não foram alteradas**.
Não há ISO, assets ou microcódigo proprietário distribuídos nesta PR.

## Antes e depois

**Antes:** `GsVu1_Call()` interpretava todas as instruções do
microprograma de geometria de cenário de 399 instruções, inclusive
transformação de cada vértice com matrizes da VU1 e os testes de clip.

**Neste experimento:** para lotes do programa 4 inteiramente dentro
do volume de recorte e com pouca variação de `1/W`, o processador
**MIPS Allegrex** calcula matrizes de tela/clip, STQ, cores e
profundidade em C nativo. O resultado vai ao **sceGu** através do
`GsGpu_Draw(3)` que já usa cache de textura, estados GS, alpha,
scissor e target do jogo. O lote precisa passar inteiro pelas
validações antes de qualquer registro GS ou pixel mudar.

Qualquer lote com vértice fora do clip, NaN/Inf, W inválido,
`count` incorreto, profundidade fora de faixa ou variação
relevante de perspectiva **volta ao interpretador original**.
Programa 0 (lutadores), 1, 2, 6, 7 e 8 permanecem interpretados.
A geometria da luta e os passes GS não são omitidos.

## Segurança e custo

- `port/psp/psp_stage4.h`: cálculos e guardas de entrada sem
  dependência de GPU/Vita.
- `port/psp/psp_gu.c`: `Psp_Stage4Prepare` converte o lote
  em uma área estática de 1024 vértices (sem malloc dentro do
  desenho). `GsGpu_DrawVu4` envia a sequência de triângulos ao
  rasterizador PSP existente, sem criar um framebuffer novo.
- `port/src/gs/gs_vu1.c`: somente o programa nº 4 tenta o
  caminho PSP; outras rotas mantêm `BT3_VU_INTERP=1`.
- `port/psp/test_stage4.c`: verifica matrizes, cores, STQ, W,
  coordenadas inválidas e fallback em host C.
- `log.txt`: nova linha `psp: native stage VU1 program4`
  registra `batches`, `vertices`, `conservative-fallbacks`.
- `native_stage4=1` é o default experimental (sem necessidade
  de `bt3.txt`). Para voltar à rota interpretada em teste
  A/B, `native_stage4=0` num `bt3.txt` opcional.
- O método envia triângulos ao pipeline existente, mas o PSP
  não oferece a mesma interpolação de perspectiva do PS2.
  Por isso, só é aceito quando a variação de 1/W no lote
  está dentro de 0,5%.

### Testes necessários

1. CI: testes C de matemática e compilação de EBOOT PSP.
2. PPSSPP Android: usar os mesmos lutadores, cenário e câmera;
   comparar a versão do PR #15 com esta no início da luta,
   animações e transições.
3. Ler `log.txt`: se `batches=0`, nenhum lote passou os
   critérios conservadores, então **não ocorreu tradução
   efetiva naquele cenário e não há ganho de FPS atribuível a ela**.
4. Se o cenário ficar diferente, inverter para o interpretador
   `native_stage4=0` ou reinstalar o EBOOT anterior.
5. Antes de promover como estável, comparar com resultados
   da VU1 microprograma em PSP/PPSSPP e testar PSP 64 MB real.

## Pendências reais — não declarar port concluído

- VU1 dos **personagens**: exige bone skinning, iluminação,
  transformações, clip por triângulo e efeitos em camadas;
  ainda não foi substituída.
- GS: framebuffer em FB16, profundidade como textura,
  efeitos de pós-processamento e compartilhamento do work
  buffer ainda não foram traduzidos de forma completa.
- `GsGpu_FbUpload` continua sem cópia funcional do
  framebuffer. Não tentar corrigir com cópias não verificadas:
  os downloads da tela cinza exigem um trace GS.
- CPU: o fallback do `ptr32.r2l` continua arriscado se
  não houver libclang; requer build reprodutível e testes.

O objetivo é **equivalência primeiro**, e apenas depois
otimização/qualidade. CI de código não comprova fps nem
gráficos corretos no PPSSPP.
