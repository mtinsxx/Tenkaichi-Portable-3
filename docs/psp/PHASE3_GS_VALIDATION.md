# Fase 3 — validação visual do GS no PSP (P03 / P06 / P07)

**Base:** branch `fix/psp-necessary-only-20261025` (PR #8); a versão funcional
`fe93677` continua intacta. Esta fase consolida os experimentos de
`fix/psp-gs-experiments-20261025` (PR #5), agora no caminho que inclui a
auditoria dos 102 callbacks antigos.

## O que existe no código

O backend `port/psp/psp_gu.c` ganhou três alternativas **opt-in**. Todas
começam desativadas para manter pixels da configuração atual.

| Entrada em `bt3.txt` | Padrão | O que muda |
|---|---:|---|
| `gs_fix=0` | 0 | Com 1, mapeia `GS ALPHA.FIX` de 0..128 para `GU_FIX` 0..255, saturando valores maiores. Mantém a regra de fator complementar. |
| `gs_clut=0` | 0 | Com 1, quando TEX0 referencia textura indexada e `CLD=0`/`CBP=0`, usa os campos de paleta da última CLUT registrada somente para decodificar e identificar o cache, sem escrever em `gGs.tex0`. |
| `gs_depth=0` | 0 | Com 1, testa GU depth por lote a partir de `TEST.ZTE/ZTST` e `ZBUF.ZMSK`. Não implementa passes especiais depth-as-texture nem vários alvos completos de GS. |

**Importante:** `alpha_fix=1` (correção anterior do AREF/máscara de
teste alfa) é **diferente de** `gs_fix=1` (fator de mistura BLEND FIX).
Não desligue `alpha_fix` inadvertidamente ao testar a nova opção.

### Novo teste determinístico no CI

`port/psp/test_gs_experiments.c` inclui
`port/psp/gs_experiments.h`, as mesmas funções usadas no renderizador:

- Verifica valores de FIX 0, 127, 128, 255 e todo intervalo de 0..255.
- Verifica todas as cinco variantes de textura indexada
  (PSMT8, PSMT4, PSMT8H, PSMT4HL, PSMT4HH).
- Garante que `TEX0` inalterado é devolvido com `CLD!=0`,
  `CBP!=0`, paleta anterior ausente ou textura não indexada.
- Garante que a troca da CLUT modifica exclusivamente bits 37–60,
  mantendo os campos de textura base e sinalizadores originais.

**Isso testa a matemática e as chaves, não a imagem produzida pelo GE.**
O CI comprova código compilável e testes unitários, não que uma batalha
esteja visualmente correta.

## Testar no PPSSPP Android

Use o EBOOT experimental do workflow da **PR desta fase**, mantendo uma
cópia do EBOOT funcional. Mantenha a função Memory Stick ligada e os
arquivos do disco do próprio usuário na pasta já usada pelo BT3.

Em `ms0:/PSP/GAME/BT3/bt3.txt`, teste **um parâmetro por execução**:

```ini
report=1
alpha_fix=1
gs_fix=0
gs_clut=0
gs_depth=0
```

1. Com todos os três `gs_*=0`: menu, seleção, Goku x Vegeta,
   barra de vida, mapa, golpes, transparências e uma transformação.
   Fotografar/registrar o FPS da mesma cena e copiar `log.txt`.
2. Mudar **só** `gs_fix=1`: comparar explosões, aura, letras,
   gradientes, brilho e contorno dos personagens.
3. Voltar `gs_fix=0` e mudar **só** `gs_clut=1`: conferir se as paletas
   de roupas/rostos/HUD permanecem corretas e se sumiram texturas.
4. Voltar `gs_clut=0` e mudar **só** `gs_depth=1`: conferir
   sobreposição entre lutadores e cenário, barras de vida e efeitos.
   Desativar se houver z-fighting ou desaparecimento de polígonos.
5. Somente se os testes isolados estiverem satisfatórios, comparar
   combinações; documentar qualquer regressão com tela e `log.txt`.
6. Repetir as cenas no PSP 64 MiB antes de promover qualquer flag.

## Ainda aberto

P03 é parcial; P04 (FB16, passes de depth-as-texture), P05 (vários
framebuffers), P08 (CLAMP/TFX) e P09 (VU1) permanecem em aberto.
Não declarar os gráficos concluídos ou equivalentes ao PS2 sem vídeo
comparativo. Não enviar ISO, ELF proprietário ou assets do jogo.
