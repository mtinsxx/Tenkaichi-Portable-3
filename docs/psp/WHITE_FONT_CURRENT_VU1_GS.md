# Integração da correção visual confirmada na versão atual

## Origem comprovada pelo usuário
- PR #22, branch `fix/psp-font-white-modulate-fe93677`, criada
  diretamente a partir de `fe93677`, recebeu a correção de RGB
  dos sprites de fonte branca. O usuário confirmou que **melhorou**.
- Nesta branch, o ponto de partida é a versão experimental mais recente
  disponível dessa sequência: PR #21
  (`fix/psp-gs-decal-text-brightness-20261025`).
- A PR #22 **não foi mesclada em main**; esta mudança porta somente
  a alteração verificada, evitando substituir os arquivos do renderizador
  atual pelos de uma versão mais antiga.

## Integração específica
1. `gs_white_modulate.h` e `test_gs_white_modulate.c` são cópias
   integrais da PR #22, sem alterar a fórmula
   `min(255, (textureRGB * 255) >> 7)`.
2. O caminho só vale para sprite com textura, `TFX=MODULATE` e
   **RGBAQ flat do segundo vértice exatamente `0x80FFFFFF`**,
   como na versão visualmente melhorada.
3. O cache atual mantém a chave de LOD, CLUT e páginas do GS,
   e adiciona `white_sprite`, impedindo misturar a cópia original
   com o RGBA pré-modulado. O alfa por texel é preservado.
4. O sprite corrigido usa `GU_TFX_REPLACE` e não a modulação
   inferior do PSP; todos os outros desenhos mantêm
   `Psp_GsTexFuncToGu` do renderizador atual, inclusive `DECAL`.
5. Framebuffers-as-texture não recebem bake e a VU1, a GU dos
   cenários, o framebuffer 16 bits, o heap e o áudio não foram alterados.
6. `white_sprite=1` está habilitado no EBOOT sem exigir
   `bt3.txt`. Opcionalmente `white_sprite=0` para A/B.

## Validação
- Os **dois** workflows PSP compilam e executam as 65.536
  comparações de cor/alfa do mesmo teste que passou na PR #22,
  além das verificações anteriores de VU1/GS, e geram EBOOT.
- O `log.txt` terá:
  `psp: GS white sprite modulation: baked-draws=... cache-hits=...
  created=... enabled=1`.
- Teste recomendado: na mesma tela MEMORY CARD slot 1,
  comparar o EBOOT PR #21 (texto cinza) com este (código da PR #22
  aplicado), verificar letras, fundo azul e moldura.
- Ganho visual na PR #22 foi **relatado**, mas o resultado do
  EBOOT integrado à PR #21 ainda depende do teste no PPSSPP Android.
  Não é uma promessa de FPS, nem resolve automaticamente os
  framebuffers GS ou todas as geometrias VU1.
- Guardar os EBOOTs anteriores e não mesclar na `main`
  (`fe93677`) sem testes reais. Nenhum dado do jogo incluído.
