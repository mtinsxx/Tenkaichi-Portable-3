# Tenkaichi Portable 3 — migração real para um jogo nativo de PSP

**Objetivo**: conservar integralmente as regras de jogabilidade de Budokai
Tenkaichi 3, mas executar desenho, animações e recursos já convertidos no
hardware da PSP. Inspirar-se na *arquitetura*, não copiar recursos/código do
Tenkaichi Tag Team. Sem exigir `bt3.txt`, pacotes proprietários no repositório,
ou um "modo desempenho".

## O que significa realmente remover as dependências do PS2

O projeto **ainda não é** um jogo PSP reconstruído. Uma versão totalmente
nativa precisa substituir o interpretador de CPU/VIF/VU, a camada de comandos
GIF/GS e as operações da memória gráfica virtual por implementações
equivalentes de gameplay, animação e renderização PSP.

Remover arquivos `gs_core.c`, `gs_vu1.c` ou os bytes de GS virtual
sem fornecer substitutos quebra leitura de textura, profundidade,
paletas, UI, ataques e modos. O código do jogo original ainda está
associado a uma **ELF de PS2** presente no dispositivo do usuário;
remover a origem sem recompilar a lógica não cria um jogo PSP.

O projeto não embute executáveis, ISOs, modelos ou texturas comerciais.

## Fronteira do port (implementada em etapas)

1. **Entrada/semântica de jogo**: dados originais e execução PS2
   continuam na camada de compatibilidade até existirem sistemas
   nativos de combate/menu/IA/estado com testes equivalentes.
2. **Conversão de recursos**: modelos, materiais, skinning, BPE,
   texturas e animações identificados por conteúdo e convertidos
   para a PSP *antes* de uso. A conversão não pode adivinhar
   estruturas do jogo não demonstradas por testes sobre recursos
   reais do usuário.
3. **Comandos PSP**: `port/psp/psp_gu.c` já envia passes nativos de
   cenário, personagens e HUD diretamente à GE via `sceGu`. O caminho
   T8/T4 mantém índices nativos quando os formatos são comprovadamente
   traduzíveis. O novo `psp_draw_material.h` elimina decodificações
   repetidas de TEX0/CLAMP/frame/Z de cada texel do espelho,
   preparando materiais imutáveis uma vez por desenho.
4. **Feedback especial**: quando BT3 reinterpreta os mesmos bytes
   PS2 PSMCT32 como PSMT8, ou trata profundidade/alpha como textura,
   **RGB5551 não contém os mesmos bits**. Essa operação só pode
   abandonar o GS de software após substituir o efeito por um
   algoritmo PSP que reproduza suas entradas e saídas reais.
5. **GPU PSP**: somente 2 MiB de VRAM física; buffers 512x272, texturas
   swizzled e snapshots 5551 precisam de versionamento. Nunca
   sobrescrever vértices enquanto a GE ainda tem seus ponteiros.

## Faltas bloqueantes identificadas nos logs de PPSSPP

- O perfil anterior de batalha mostrou ~0,4 FPS, >1 segundo em
  rasterização do espelho GS, muitas falhas de aliasing de framebuffer
  e efeitos nativos ainda sem implementação. **Nenhuma dessas falhas
  é resolvida apenas apagando o código de PS2.**
- PSMCT32/PSMT8, canais de alpha e modos especiais de profundidade
  precisam de tradução de efeitos com provas contra o original.
- O programa VU1 `stage399` ainda executa interpretação. VU1 nativo
  para personagens e cenário já existe, mas não substitui todos os casos.
- Há padrões de UI, blend, efeitos e vídeos MPEG-2 com tempos distintos;
  cada um exige medição independente.

## Critérios objetivos para chamar uma fase de nativa

- Cada categoria de comando do jogo possui implementação PSP com saída
  visual equivalente, incluindo paletas, Z/alpha, partículas e HUD.
- Um único quadro mantém geometria, ordem, textura e profundidade corretas.
- Menus, seleção, introdução e batalhas são exercitados em PPSSPP e
  PSP real (se disponível), não apenas compilados.
- FPS, latência de áudio, memória e consumo de VRAM são medidos antes
  e depois; sem saltos de performance declarados sem resultado.
- Testes CI preservam matemática, texturas, máscaras e controles;
  nenhum `EBOOT.PBP` é rotulado como "port completo" apenas porque
  o build teve sucesso.

## Nesta PR

- `PspDrawMaterial_Decode` e `PspDrawPixelState_Decode` materializam
  os registradores de textura e framebuffer **uma única vez por desenho**
  no caminho de espelhamento que ainda é necessário.
- A CPU PSP usa campos pré-decodificados para amostragem,
  textura/CLUT e testes, mantendo `vram_rw` com os bytes originais.
- O caminho de referência não-PSP fica separado; sem excluir compatibilidade
  que o jogo ainda precisa.
- Testes host validam os campos e 3,2 milhões de coordenadas de
  wrapping contra os cálculos GS originais.
- A GPU/driver não é contornada: PSP GU é a interface adequada à GE.

**Status**: etapa intermediária da migração. Não confundir com
reescrita completa do jogo, nem com correção verificada dos gráficos.
