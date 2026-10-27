# BT3-PSP — desempenho da abertura e de batalhas (25/10/2026)

## Evidência recebida

Na captura da abertura do Dragon Ball, PPSSPP Android mostra
**9/8 FPS e 100,2% de velocidade**. Não usar esse número para
afirmar que a batalha roda a 8–9 FPS: o filme é MPEG-2
intra-only e usa outro caminho de renderização.
A velocidade de 100,2% é ritmo de emulação, não 60 FPS
de quadros MPEG efetivamente apresentados.

O código de `port/src/plat_movie.c` já relata uma medição
anterior de ~94 ms por frame decodificado (~10,6 FPS de vídeo
sem contar outras operações). `movie_sync=1` evita dessincronia
saltando quadros intra atrasados. **Não removê-lo** para
simular um FPS maior; isso alongaria a abertura e afetaria
a sincronização do áudio ADX.

## Melhorias implementadas (preservam cores PR #23)

### 1. MPEG-2: menor custo sem mudar os pixels
`port/src/psp_movie_5551.h` e `port/src/plat_mpeg2.c`:
- Calcula uma única vez as posições de amostragem
  `x*512/480`, `y*448/256`. Antes eram feitas para
  cada um dos 122.880 pixels de cada frame de abertura.
- Reutiliza os componentes Cr/Cb do mesmo texel 4:2:0
  quando dois pixels visíveis fazem referência a ele.
- Mantém o mesmo YCbCr BT.601, a mesma saturação,
  o mesmo frame de 480x256 RGB5551 e o mesmo buffer
  de 512 pixels por linha.
- `build.py`: `plat_mpeg2.c` usa `-O3`, tal como
  `gs_core.c` e `gs_vu1.c`, mantendo `-fno-strict-aliasing`,
  sem `fast-math` e sem alterar a matemática da CPU do jogo.
- `test_movie_5551_speed.c`: 6 quadros integrais,
  incluindo neutros, extremos e pseudoaleatórios,
  comparados **byte a byte** com o conversor anterior.

Não remove o custo grande do IDCT/entropia do MPEG-2:
**não prometer 30 ou 60 FPS** sem medir a nova build em PPSSPP
ou usar uma rota de vídeo decodificada por hardware
(com suporte a formato compatível), o que exige outro projeto.

### 2. Jogo / renderização: paletas GS mais rápidas
`port/psp/psp_gu.c`:
- `clut_hash()` é chamado mesmo em muitos acertos do cache
  de texturas, e anteriormente acessava 16/256 entradas
  `Gs_VramRead()` individualmente.
- Nas quatro representações suportadas de CPSM
  `0,1,2,10`, a paleta é lida em **um retângulo 16×1
  ou 16×16** usando o leitor em blocos
  `Gs_VramReadRect()` já existente no projeto.
- Preserva a ordem original dos 16/256 texels e FNV32,
  chave de textura e regra de invalidação de GS.
- Executa a leitura antiga em 1 de cada 256 consultas;
  se o hash divergir, registra no `log.txt` e desativa
  a otimização automaticamente pelo resto da execução.
  CPSM desconhecidos continuam no caminho antigo.
- `test_clut_hash_speed.c` compara 16 e 256 cores
  sob um mapa físico de memória permutado, com
  diferentes padrões. Teste é complemento, não
  substitui a verificação runtime no GS real.
- `log.txt` informa:
  `GS fast CLUT hashing: batched=..., checked=...,
  mismatches=..., enabled=...`.

### 3. Componentes intocados
As correções das letras brancas que o usuário aprovou
(PR #22 transportada pela PR #23), `ALPHA.FIX`,
`DECAL`, GU FB16, VU1 de cenário e lutadores,
memória 40 MB/heap 29 MB, áudio, vídeo direto
e salvamento não foram revertidos.
O ramo está baseado na **PR #23**, com `main`
em `fe93677` preservada.

## Teste de verdade — PPSSPP Android

1. Guarde o EBOOT da PR #23.
2. Instale o novo EBOOT; não precisa `bt3.txt`.
3. Compare o **mesmo segundo da abertura** e o
   contador FPS/VPS, sem aceleração de áudio ou
   `frameskip` adicional no PPSSPP.
4. Entre em uma batalha e compare os mesmos lutadores,
   mapa e ações, registrando FPS e eventuais glitches.
5. Abra `PSP/GAME/BT3/log.txt`; procure:
   - `MPEG-2: decoded` / `movie A/V sync`;
   - `psp: frame cadence` e `psp: PROFILE`;
   - `GS fast CLUT hashing`;
   - `GS white sprite modulation`;
   - `VIF/VU1`.
6. Se possível, evite modo de economia de bateria
   do Android durante o teste, pois pode limitar o
   desempenho geral do PPSSPP.

A CI comprova comparação de pixels e link EBOOT.
**Somente medições PPSSPP / PSP real comprovam ganho
de FPS, estabilidade e melhora de batalha.**
