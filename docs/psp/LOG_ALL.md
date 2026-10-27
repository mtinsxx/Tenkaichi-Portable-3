# LOG ALL — nativo no EBOOT.PBP

**Sem bt3.txt.** O diagnóstico foi incorporado diretamente ao executável
`TP3-NATIVE-LOGALL-20261027`. O `EBOOT.PBP` registra automaticamente o que
acontece com o renderizador quando você abre o jogo: `LOG_ALL` e relatórios
de FPS/GS/VU1/texturas. O teste de profundidade permanece no padrão
`gs_depth=2`, **não** é uma nova tentativa de corrigir o cenário preto.
Os controles `portable` também são embutidos.

## Como usar no PPSSPP Android

1. Copie o `EBOOT.PBP` para o Memory Stick em `PSP/GAME/BT3/`.
2. Mantenha os arquivos da sua própria cópia do jogo em `PSP/GAME/BT3/disc/`.
3. Abra o jogo e reproduza a tela preta por 10–20 segundos.
4. Saia normalmente e envie o `PSP/GAME/BT3/log.txt` completo.

O executável **não lê nem inclui `bt3.txt`**. Arquivos de saves e
`bt3_settings.txt` pertencem ao **jogo**, são diferentes da configuração
externa eliminada e continuam sendo preservados.

## Capturas disponíveis

Linhas `psp: LOG_ALL` contêm totais e a primeira ocorrência por
categoria de falha: validação de VU1/stage399, projeção e recorte;
triângulos aceitos/enviados; operações GS descartadas; texturas
não decodificadas; conflito e invalidação de framebuffers;
composição e efeitos nativos não implementados. Amostras de estado
incluem FBP, ZBP, TBP, PSM, TEST, PRIM e número do quadro.
O perfil tradicional também registra FPS real, tempos de GS/VU1,
decodificação de textura, GE e VBlank.

O coletor é **limitado** para evitar escrita por triângulo, mas pode
reduzir o FPS observado. Ele não vê pixels após rasterização e
**não detecta automaticamente todos os defeitos possíveis**.
Para uma versão futura sem a sobrecarga, será necessário recompilar
ou produzir uma build de produção separada, sem configuração em TXT.

## Pixel-surface verification after GPU completion (2026-10-27)

The existing native PSP `LOG_ALL` now logs `psp: LOG_ALL PIXELS post-GE` during sampled fight frames. After the *existing* `sceGuSync()` (no extra stall), it samples 192 physical RGB5551 pixels from each surface: screen drawbuffer, WORK0 (FBP336) and WORK1 (FBP368). The log records the number of RGB-nonblack pixels, first-192 hash, logical owners and stage/fighter submission counts. This **does not** assert visual correctness: a sparse grid can miss thin geometry and the hashes cannot tell which GS primitive wrote a pixel. It does locate whether the missing scene is visibly stored in screen vs auxiliary VRAM, and requires no `bt3.txt` or PSP settings edits. Normal geometry, depth test and textures stay untouched.

## Battleground LOG_ALL file-gap fix (2026-10-27)

An uploaded 499,813-byte PSP log had a contiguous 415,098-byte NUL-filled region between the menu profile and the four-frame battle, with no `LOG_ALL PIXELS post-GE` entries. This is a damaged diagnostic capture; it does **not** show that the actual GPU produced no pixels. The code now serializes concurrent calls to `Psp_Log` and `Psp_LogFlush` through one PSP semaphore, reducing potential corruption from shared stdio buffering (concurrent writes remain only a suspected cause). A second `LOG_ALL GPU-PROBE last-post-GE` line in the regular ~2-second report repeats the latest *already sampled* screen/WORK0/WORK1 RGB counts and hashes, without any extra GPU stall. Find the new `TP3-GE-PIXELS-LOGGUARD-20261027` build ID before comparing. This change does not modify depth, stage transforms, alpha, textures or the rendering outputs and is not yet a fix for black backgrounds.
