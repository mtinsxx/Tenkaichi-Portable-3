# BT3-PSP — auditoria de arquitetura e estabilidade (25/10/2026)

**Escopo real:** inventário *estático* do código da branch
`polish/psp-startup-vram-20261025`, com proteção de
`main = fe936774e7d3b94130e8f3f21c34fbd945108c57`.
Não há dados/ISO privados do jogo no repositório.
Compilar, mesmo com 387 objetos e zero erros, **não é** provar
boot, jogabilidade, performance ou fidelidade à GS do PS2.

## Pergunta: tudo está comprimido / convertido para PSP?

**Não.** O executável é compilado para **MIPS Allegrex nativo**
(`port/psp/build.py`: `psp-gcc`, saída EBOOT), e não executa
diretamente código EE do PS2. Porém, preserva muito da *semântica*
do PS2: estruturas 32-bit, programa C descompilado, tabelas do
jogo, regras da FPU, DMA/VIF/GIF, VRAM GS virtual e microprogramas VU1.
Parte desses sistemas é traduzida ao PSP, parte interpretada e parte
ainda não implementada. Comprimir texturas não corrige esses limites.
Não há base para afirmar estabilidade completa do jogo.

## Inventário por subsistema

| Componente | PSP nativo / equivalente | Compatibilidade PS2 ainda necessária | Risco / situação |
|---|---|---|---|
| Código de jogo / CPU | `psp-gcc` para Allegrex, C decompilado de `src/` | Mesmo layout de ponteiros/estruturas e matemática, semântica de ordem dos argumentos | **Alto**: `compile_ee` registra `r2l unavailable` e continua em builds sem libclang; expressões com efeitos colaterais podem alterar resultado |
| Memória principal | `psp_mem.c` aloca e valida, `MEMSIZE=1` | Heap original do jogo aproximadamente 29 MB; GS emulado 4 MB, SPU emulado 2 MB | **Alto**: só PSPs com 64 MB; PSP-1000 32 MB não suportado; cache/texturas/estágios competem pelo resto |
| VU0 / matemática | Referências de `src/port/vu0_a.c` e `vu0_b.c` compiladas para PSP | Emula aritmética e precisão do PS2 | **Médio/alto** para CPU; remover ou trocar por float rápido exige testes de equivalência |
| VU1 / geometria | `gs_vu1.c` compila para PSP e recebe VIF rápido em alguns formatos | `BT3_VU_INTERP=1` **interpreta microprogramas originais** por instrução | **Crítico** potencial para FPS. `GsGpu_DrawVu0/4/6` em `psp_gu.c` são *no-ops*, então desligar intérprete NÃO é aceleração segura |
| DMA / VIF / GIF | Frontend portátil `gs_core.c` / `gs_vu1.c` e leitores PSP | Interpreta listas do formato PS2 e registra GS | **Médio/alto** custo CPU; medições atuais não separam todos os casos |
| GS / renderização | `sceGu` 480x272, RGB5551, cache swizzled e opção `tex_limit` | Regras de GS, múltiplos FBP, FB16, depth-as-texture, CLAMP/TFX específicos | **Crítico** para tela cinza/sumiço: `GsGpu_Draw` descarta passes PSM16/FBP=ZBP e alguns formatos; só um work buffer para vários alvos virtuais |
| GS readback / pós-efeitos | Chamada `GsGpu_FbUpload` ainda faz `flush()` | Não copia imagem de volta ao FBP que os fades esperam; `GsGpu_Native` é vazio | **Alto**: antes, comentário alegava cópia que não era feita; agora o `log.txt` registra o uso destes no-ops |
| Texturas | `Gs_VramReadRect/Idx8`, RGB5551/8888, GS cache, LOD opt-in/default da branch experimental | Dados originais GS, CLUT, texturas indexadas e alfa | **Parcial**: a redução de resolução atua apenas nas cópias dos triângulos 3D; fonte GS ainda é decodificada na resolução original. Nenhum ganho de FPS comprovado |
| Arquivos, AFS, CD/DVD | `psp_file.c` lê membros AFS diretamente via Memory Stick | Reproduz API ADXF/CRI e `sceCdStatus`/tipo de disco | **Parcial**: CRI/DVD de PS2 substituídos por respostas ou no-ops controlados; teste de mídia corrompida/latência ainda necessário |
| SPU2 / som | PSP audio output 44,1 kHz, mixer SDL shim, ADX | `snd_se.c` simula comandos de `sceSifCallRpc` e RAM SPU | **Parcial**: chamadas RPC conhecidas traduzidas, outras respostas neutras; FIFO limitada pode descartar frames. Agora registra overflow e protege API de ponteiros nulos |
| Funções ausentes | 2 chamadas diretas implementadas na PR #7 | **102 símbolos** antigos em ponteiros de tabelas PS2, apontando para `Port_MissingAt` se executados | **Indeterminado**: não remover nem assumir que nunca serão usados; contar e identificar chamados reais antes de portar |
| Vídeo MPEG | Decodificador software e caminho PSP RGB5551/sincronização | MPEG2 PS2 e áudio do jogo | **Risco de lentidão**, dependente de tempo de decodificação e validação PPSSPP/hardware |
| Controles/saves | Botões PSP mapeados e Memory Stick para arquivos | Funcionalidade equivalente a duas portas / cartões PS2 | **Parcial**: mapear L2/R2/L3/R3 via SELECT, não garante todos combos e campanhas |

### Evidências críticas no código

1. `port/psp/build.py`, `compile_ee`: se `clang.cindex` falha,
   cai no C original e compila sem o reordenamento obrigatório
   `ptr32.r2l`. A PR #10 corrigiu **seis casos** de parâmetros
   `chr`/`kind` usados antes de getters, mas não auditou todas
   as expressões do jogo. O crash `Read Word at 0x994`
   foi compatível com leitura `BtlMemberChr.active` por `chr=NULL`.
2. `port/psp/psp_gfx.c`, `Psp_GsGetenv("BT3_VU_INTERP")` devolve
   `"1"`: `GsVu1_Call` executa o intérprete. `psp_gu.c`
   define `GsGpu_DrawVu0/4/6` sem emitir geometria: nunca mudar
   o retorno para `NULL` até implementar caminho nativo.
3. `port/psp/psp_gu.c`, `GsGpu_Draw`: ignora render passes
   para certos GS frame formats/endereços de depth e pode cair
   para scratch/texture cache indevidamente. `target_get`
   mantém **um WORK** para vários FBP; antigos targets podem
   perder pixels quando trocam de dono.
4. `GsGpu_FbUpload` faz `flush()`, mas **não implementa**
   `sceGuCopyImage`/memcpy/readback do framebuffer destino;
   `GsGpu_Native` também não executa efeitos. Agora ambos
   registram contadores em `psp: architecture gaps`.
5. `port/psp/psp_mem.c` mantém heap fixo e cópia das tabelas
   originais, `port/src/gs/gs_core.c` mantém VRAM GS, enquanto
   `port/psp/psp_gu.c` precisa compactar VRAM GE em 2 MB e
   usa memória principal para cache. Isso não se resolve apenas
   reduzindo `tex_limit`.
6. `port/psp/expected_stubs.txt` conserva 102 callbacks PS2
   em tabelas de dados. O checker e linker os monitoram; não há
   evidência de que todos são necessários nem de que nenhum é usado.
7. `port/psp/psp_audio.c` buffer tem 8.192 frames por stream e
   24 streams; antes a fila ignorava o excesso e reportava sucesso.
   Agora conta perdas de frames e devolve `false` na entrada parcial.
   Se os chamadores ignorarem `false`, o evento ficará no log.
8. `port/src/gs/gs_vu1.c` monitora instruções desconhecidas
   `sUnknown`; se alguma aparecer em jogo, a implementação VU1
   não é completa mesmo que o EBOOT rode.

## Correções pequenas executadas nesta auditoria

- `Psp_GsGetenv`: não lê `name[4]` antes de validar a string;
  retorno do intérprete VU1 permanece deliberadamente `1`.
- `psp_audio.c`: chamadas de stream verificam `NULL` e
  `sMutexReady`, com contagem de overflow da fila e sem
  informar sucesso para dados não enfileirados completamente.
- `Psp_VBlank`: reporta `psp: audio FIFO saturated` sem
  emitir milhares de linhas da thread de áudio.
- `psp_gu.c`: reporta `psp: architecture gaps:
  unimplemented-fb-readbacks=..., native-effects=...`,
  sem alterar pixels do jogo.
- `test_arch_safety.py`: detecta regressões nas verificações e
  na proteção de chamadas PS2 não implementadas.

Todas essas mudanças ficam numa branch de auditoria, sem
alterar `main`, sem `NULL` artificial em efeitos nem prometer
que framebuffer/GS/VU1 já estão portados.

## Ordem técnica recomendada para conclusão

1. **P0: correção de semântica de CPU**. Conseguir build `r2l`
   estrito reproduzível e inspecionar todas as expressões com
   parâmetros de saída. O fallback sem libclang precisa ser
   eliminado dos builds usados para validar jogabilidade;
   corrigir casos específicos quando houver evidência.
2. **P0: precisão de GS/passes**. Usar screenshots das mesmas
   cenas e logs `skipped_fb16`, `skipped_depth`,
   `unimplemented-fb-readbacks`, `scratch-owner-changes`.
   Implementar alvos/readback com orçamento de memória definido;
   evitar ligar às cegas `gs_depth` ou `gs_fix`.
3. **P1: geometria VU1**. Medir `estimated-VU1` e quais dos
   nove programas dominam a execução. Substituir **um programa
   por vez** por C/Allegrex testado contra a saída do intérprete,
   conservando fallback, sem copiar shaders Vita.
4. **P1: memória/performance**. Medir pico de heap/cache, texture
   decode, número de evictions, custo de GSchain e espera da GPU.
   `tex_limit=256` sozinho não elimina a carga de interpretar
   PS2 VU e as leituras GS originais.
5. **P1: áudio/arquivos**. Testar música/vozes, underruns,
   FIFO saturada, abertura de AFS, save/load e Memory Stick
   desativado. Portar APIs adicionais **somente se chamadas**.
6. **P2: UX e testes**. Validar abertura, menu, HUD,
   seleção, transformação, ataques de Ki, pausa, campanha,
   loading e vídeos, comparando PPSSPP com a versão
   `fe93677`, depois hardware PSP 64 MB.

## Evidência necessária para afirmar estável

- EBOOT compila em CI; suite de testes de semântica passa.
- Log real de ~2 min de luta, sem crash, com `frame cadence`,
  `PROFILE textures`, `VIF/VU1`, `architecture gaps`,
  `audio FIFO saturated` e `fb-alias`.
- Vídeo lado a lado do mesmo mapa e jogadores na mesma câmera.
- Exercitar cenários de economia de RAM, efeitos, áudio,
  salvamento, load, transformação, longas sessões.
- Repetir no PSP real 64 MB, não afirmar PSP-1000.

**Veredito:** port nativo Allegrex *parcial*, com compatibilidade
PS2 ainda necessária para funcionamento. Algumas funções PS2 estão
corretamente substituídas; outras ainda são intérpretes ou
no-ops e representam trabalho técnico pendente. Nenhuma
compilação isolada pode certificar 100% da estabilidade.
