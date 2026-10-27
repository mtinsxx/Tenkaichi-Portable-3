# PSP — polimento da inicialização e diagnósticos (25/10/2026)

Base: `perf/psp-texture-lod-20261025` (PR #12). Não alterar
`main` / `fe93677`. O build experimental já adota
`tex_limit=256` mesmo sem `bt3.txt`; esta fase NÃO muda
esse padrão, nem lógica de combate, controles, texturas ou FPS.

## Defeito observável e causa verificável no código

Capturas do usuário mostram faixas cinzas e, sobre o HUD,
caracteres antigos da inicialização `BT3 for the PSP / Reading
the game's data tables from ...`.

O startup imprimia esse texto com `pspDebugScreenPrintf`, que
grava pixels de texto na VRAM. Antes da correção, `Psp_GuInit`
zerava o framebuffer auxiliar e mandava `sceGuClear` somente
no buffer de desenho `FB0`; **o buffer de exibição `FB1`
não recebia limpeza explícita antes de se tornar visível**.
A tela usa dois buffers 512×272 em formato RGB5551 (278.528
bytes cada). O framebuffer auxiliar e o depth buffer usam
espaços separados dentro dos 2 MiB da VRAM PSP.

## Ajustes feitos

1. Após a leitura e o mapeamento dos arquivos do usuário,
   `__wrap_main` apaga o texto da tela de inicialização,
   antes de entregar a tela ao GU.
2. `Psp_GuInit` zera **uma única vez** os dois framebuffers
   e o target auxiliar antes de `sceGuInit`, impedindo que
   bytes antigos da tela de debug apareçam por simples troca
   de buffer. **Não zera buffers durante o combate** e
   mantém o código de `GsGpu_FrameEnd` igual.
3. Guardas de compilação evitam sobreposição das regiões
   framebuffer/depth/work/textures ou exceder a VRAM.
4. Quando o ELF está ausente, a mensagem orienta a habilitar
   Memory Stick e confirmar `BT3/disc/`, sem exigir o
   arquivo opcional `bt3.txt`.
5. `sceKernelStartThread` agora reporta o retorno real da
   operação se falhar, separado do erro de criação da thread.
6. `port/psp/test_boot_polish.py` protege o ordenamento
   das operações e a preservação da tela de erro fatal;
   executado antes do EBOOT no CI.

## Limites

A limpeza inicial remove pixels antigos carregados do
PSP debug-screen, mas não implementa FB16, depth-as-texture,
preservação de múltiplos targets auxiliares do PS2 ou o
redirecionamento correto de todos os passes de framebuffer.
**As faixas cinzas podem continuar** mesmo se o texto
de inicialização desaparecer. Não anunciar conserto completo
do renderizador a partir de compilação.

## Como testar sem `bt3.txt`

Baixe o EBOOT do workflow desta PR, preservando o EBOOT
funcional antigo. No PPSSPP, mantenha Memory Stick ativado
e os dados de seu próprio jogo em `PSP/GAME/BT3/disc/`
como antes.

Compare os mesmos menus e início de luta:
- O texto antigo de carregamento deixa de aparecer por trás
  do HUD?
- Há faixas cinzas, personagens faltando ou novos travamentos?
- A velocidade permanece comparável à versão anterior?
- `PSP/GAME/BT3/log.txt` contém a linha
  `psp: startup VRAM clean` e, em caso de falha, a
  mensagem de erro correta?

Se não houver melhora visual, não repetir alterações
cosméticas. O próximo diagnóstico deverá rastrear
`fb-alias`, `scratch-owner-changes` e os passes GS
descartados. Nenhum arquivo ou asset proprietário do
jogo foi adicionado à branch.
