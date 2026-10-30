# BT3 PSP: conversão automática de modelos durante carregamento (30/10/2026)

Esta etapa elimina a necessidade de **procurar arquivos AFS, descobrir
índices, instalar pacotes F3P1 ou criar enable-fighter-packets.txt**
para experimentar a preparação nativa de geometria de lutadores.

## Como funciona no EBOOT

O BT3 já carrega seus próprios modelos pelo fluxo
`BtlRes_LoadFile → BtlObjMdl_BindFiles`. O EBOOT intercepta o
ponto **depois que o arquivo foi carregado, antes que
`MdlTex_RebaseChain` reescreva os endereços TEX0**:

1. Lê a lista original de `ObjMdlMesh` do modelo em memória.
2. Identifica o ID real do osso (node) e somente comandos VIF
   `V4-32` literais, com `TOPS+5` e 3 quadwords por vértice.
3. Verifica intervalos, contagem, coordenadas, normais, UV, pesos
   de dois ossos e validade dos números antes de aceitar o lote.
4. Pré-prepara os lotes reconhecidos em um cache circular de até
   32 lotes **em RAM**. Nada é salvo no cartão e nenhum arquivo
   de personagem é adicionado ao EBOOT.
5. Na batalha, se a geometria de um lote pré-preparado for
   **idêntica byte a byte** à do jogo, ela pode ser usada
   pelo renderizador PSP GU que já faz o skinning nativo.
   **As matrizes dos ossos, animadores, luz, TEX0 e efeitos
   continuam sendo fornecidos pelo BT3 original a cada quadro.**
6. Dados desconhecidos ou divergentes usam imediatamente o
   caminho anterior, sem remover bonecos nem inventar triângulos.

O cache permanece limitado; o jogo não precisa de um PC nem de
conversão manual. Os pacotes externos F3P1 permanecem somente
para testes especializados opt-in e não são necessários para
esta função. A aplicação continua lendo o disco original do
usuário em `PSP/GAME/BT3/disc`, como já fazia.

## Medição

O log da nova versão contém:

- `automatic original BT3 model native prewarm`: modelos
  inspecionados e lotes originais pré-preparados;
- `auto native BT3 model assets`: lotes em cache e acertos
  contra os dados vivos da batalha;
- estatísticas já existentes de `native fighter VU1` e do
  espelhamento GS, para comparar o tempo real de CPU/PPSSPP.

## Limites

**Não foram convertidas todas as malhas e animações do jogo.**
Esta otimização reconhece um subconjunto de VIF para lutadores,
não decodifica todas as variações compactadas de PS2, não
remove o trabalho de enviar VIF ao motor original, nem
substitui o VU1 stage399, os render targets GS ou o
sistema de partículas. **Não há confirmação de FPS superior
ao build anterior**, muito menos de desempenho igual ao
Tenkaichi Tag Team. O cache de RAM e suas comparações podem
até custar CPU; precisamos analisar o `log.txt` de teste.

Não requer `bt3.txt` nem mudanças nos controles. Preserva
as melhorias anteriores de cor, HUD, framebuffer e
feedback PSMCT32/PSMT8.

BUILD-ID: `TP3-AUTO-FIGHTER-MODELS-20261030`.
