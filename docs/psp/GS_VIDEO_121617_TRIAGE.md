# Investigação do renderizador — gravação PPSSPP de 25/10/2026 (12:16)

**Branch experimental:** `fix/psp-gs-indexed-hi-20261025`, derivada
da PR #10 (correção da avaliação de argumentos na batalha).
**Não mesclar na main**; o EBOOT `fe93677` permanece a referência.

## O que a nova gravação mostra

- Primeiros segundos: cenário 3D com gramado e céu. Texturas do
  terreno legíveis, mas personagens não aparecem na mesma cena.
- Durante a inicialização da batalha: tela cinza com faixas e texto
  `BT3 for the PSP / Reading the game's data tables ...` **ainda
  visível**, em parte junto da interface de combate.
- No final: o modelo de Goku aparece sobre fundo preto em pelo menos
  um quadro. Isso comprova que *alguns* polígonos/personagens chegam
  ao desenho, não que o frame final esteja correto.
- Contador PPSSPP mostra a velocidade do emulador e quantidade de
  quadros variáveis, não uma medição isolada de custo de texturas.
- O trecho observado nesta gravação **não reproduz a tela de erro
  `Read Word at 0x994`** da gravação anterior. Não comprova ainda
  estabilidade da batalha completa.

## Ligação com as antigas correções gráficas

1. O backend `port/psp/psp_gu.c` já corrigia TFX/ABE, conversão de
   UV em texels, sprites com cor flat, ALPHA.TEST e CLUT.
   Essas mudanças permanecem intocadas.
2. O backend fazia `return` sem desenhar **qualquer passe que
   usasse textura `PSMT8H (0x1B)`, `PSMT4HL (0x24)` ou
   `PSMT4HH (0x2C)`**. Não eram formatos desconhecidos do
   núcleo GS: `vram_rw` já decodifica os bits corretamente.
3. O leitor rápido PSP `Gs_VramReadIdx8` em `gs_core.c` só
   aceitava `0x13` e `0x14`; retornava 0 nos formatos high-index.
   O chamador em `psp_gu.c` **não verificava esse retorno**,
   podendo ler índices sem inicialização caso esses passes fossem
   permitidos simplesmente removendo o `return`.

## Correção experimental nesta branch

- `port/psp/gs_hi_tex.h` especifica os bits dos três modos high-index
  com o mesmo layout de bloco GS usado pelo PSMCT32.
- `Gs_VramReadIdx8` do PSP decodifica agora 8H/4HL/4HH direto
  dos blocos 8x8, preservando a máscara de nibble correta.
- `psp_gu.c` rejeita uma decodificação mal-sucedida **antes**
  de usar o scratch, em vez de aceitar bytes arbitrários.
- `gs_hi_tex=1` libera esses três modos, sem reativar passes
  depth-as-texture nem mudar o default. Com `gs_hi_tex=0`,
  o comportamento anterior é preservado.
- `test_gs_hi_tex.c` testa as máscaras de 8 bits, low 4 e high 4,
  vários padrões e os 256 valores de byte, rodando no CI do PSP.

**A correção do leitor é real; não sabemos ainda se os três formatos
foram usados na cena gravada.** Ganho visual depende de teste real.

## Comparação PPSSPP — mudar uma variável de cada vez

Em `ms0:/PSP/GAME/BT3/bt3.txt`:

```ini
report=1
gs_fix=0
gs_clut=0
gs_depth=0
gs_hi_tex=0
```

1. Boot com `gs_hi_tex=0`, mesma dupla e cenário do vídeo.
   Registrar céu, personagens, fundo cinza, HUD e 1 minuto de jogo.
2. Trocar **somente** `gs_hi_tex=1`; fechar/reabrir EBOOT
   e comparar os mesmos quadros e contadores.
3. Coletar `log.txt` com `psp: GS high-index textures`
   (`permitted`, `blocked`, `decode-failed`). Se os dois
   testes mostrarem `permitted=blocked=0`, esses formatos não
   explicam o problema da cena registrada; voltar ao estudo dos
   destinos de framebuffer.
   Também coletar `psp: GS menu passes`:
   `skipped_fb16`, `skipped_depth`,
   `skipped_special_texture`, `missing_texture`,
   `no_target`. Comparar a taxa real de `GS FPS`.
4. Coletar `psp: fb-alias`: `valid`, `fallback`,
   `reupload`, `unrendered`, `self`,
   `scratch-owner-changes`. Frequentes trocas de scratch
   podem justificar uma estratégia de preservação de alvos.
5. Se ficar pior ou travar com `gs_hi_tex=1`, restaurar 0.
   Teste real no PSP 64 MiB antes de aceitar.

## Problemas que esta branch NÃO resolve

- Múltiplos PS2 `FBP` auxiliares disputam **um único framebuffer
  de trabalho** (512x272x16 bits): não há snapshots separados.
- Passes com framebuffer em PSM16 (P04) e uso de depth como textura
  ainda são ignorados pelo backend PSP.
- `GsGpu_FbUpload` está sem operação efetiva no PSP, o que pode
  afetar a composição de cenas que leem de volta uma imagem.
- A referência GS/VU1 do PS2 não é equivalente ao PSP GE; muitas
  texturas/cenas exigem testes de imagem e configuração por passe.

Não confundir evidência de compilação, testes C e imagem PPSSPP.
Não acrescentar assets/ELF proprietários ao repositório.
