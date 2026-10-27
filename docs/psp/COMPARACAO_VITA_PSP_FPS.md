# BT3 — comparação de desempenho entre o port PS Vita e o PSP (24/10/2026)

## Fontes inspecionadas

- Vita: [arielxflores/Tenkaichi3Decomp-Vita](https://github.com/arielxflores/Tenkaichi3Decomp-Vita), branch `vita`.
- PSP: [mtinsxx/BT3-PSP](https://github.com/mtinsxx/BT3-PSP), base da build `d0413485`.
- Arquivos: `port/src/gs/gs_vu1.c`, `port/src/gs/gs_core.c`, `port/vita/build.py`, `port/psp/build.py`, `port/src/port_vufast.c`, `port/vita/vita_vu0_inline.h`, `port/vita/vita_gfx.c`, `port/psp/psp_gu.c`.

## O que foi medido, antes da adaptação

O `log(6).txt` da build PSP de diagnóstico `cf9f331` mostra, na tela lenta:

- `GSchain` aproximadamente **2000–2244 ms** por janela de ~2 s, enquanto `GUfinishSync` normalmente é **0–1 ms** e `gameCPU` é **3–9 ms**.
- O cache atinge ~8 MB de RAM, `alloc-failed` chega a **848**, `left out` a **6438**, e a execução ~**3 FPS**.
- Os tempos `GSchain` incluem todo processamento no VIF/GS, inclusive partes de espera, e não identificam por si sós o custo do interpretador VU1.
- MPEG-2: **94 ms** de média por quadro decodificado, além dos **8 ms** de conversão do vídeo.
- A correção `d0413485` reduz a pressão de memória; seu ganho ainda não foi validado com novo `log.txt`.

## Comparação e decisões

| Área | Port Vita | PSP anterior | Adaptação / limite |
|---|---|---|---|
| VIF UNPACK | `gs_vu1.c` usa via `PORT_VITA` um caminho direto para vetores V1–V4 de 32 bits, com máscara e modo neutros | Sempre usava decodificação genérica por componente | **Portado** um caminho direto bit a bit equivalente para `PORT_PSP`, com teste de alcance e fallback ao interpretador para os outros formatos |
| UNPACK lazy | Vita permite adiar cópia de vetores apontando para buffers originais, via `gVitaLazyUnpack` | Não tem estabilidade comprovada do ponteiro para cada transferência de DMA | **Não portado**: risco de ler dados alterados antes do uso |
| VU0/Matrizes | Vita contém `port_vufast.c` e `vita_vu0_inline.h`, otimizados para ARM/semântica de FP | PSP compila código de referência com regras do PS2 | **Não copiado**: stand-ins contêm assembly/semântica específicas; exige versão Allegrex testada |
| Compilador GS | Vita compila `gs_core.c` e `gs_vu1.c` com `-O3` | PSP compilava ambos com `-O2` | Candidato a mudança isolada; `-O3` é permitido pelo GCC do PSPSDK, mas precisa de compilação e testes |
| Renderização | Vita usa GPU Vita via VitaGL e buffers diferentes/assíncronos | PSP usa `sceGu`, 2 MB VRAM e UMA framebuffers | **Não transplantar** VitaGL ou tarefas multithread sem revisão específica |
| Frame skipping | Vita implementa `Vita_Pace` / `Vita_SkipWanted` | PSP renderiza quadros síncronos | Skipping pode ajudar fluidez aparente, mas **não reduz CPU gasto no VIF/VU1** se esses comandos continuarem sendo executados |
| Texturas | Vita tem decodificação por blocos e cache | PSP recebeu versão por blocos, cache em RAM e recuperação quando `malloc` falha | Preservar melhorias e medir `left out` / alocações em novo teste |

## Instrumentação da adaptação de VIF

O `GsVu1_Unpack` no PSP agora registra:
- `fast-unpack`: total de vetores decodificados pelo caminho rápido equivalente ao Vita.
- `generic-unpack`: total restante, usando a referência original.
- `VU1-interpreter`: número de chamadas ao interpretador; uma em cada 16 chamadas é cronometrada (estimativa de custo).

Os números serão enviados ao `log.txt` em `Psp_GuStats`.

## Próximas decisões baseadas em números

1. Se `fast-unpack` for maioria, mas `estimated-VU1` dominar, otimizar o interpretador VU1 — o caminho VIF já não é o principal custo.
2. Se `generic-unpack` continuar alto, analisar formatos, mascaramento e modos reais nos pacotes antes de criar mais atalhos.
3. Se `alloc-failed` e `left out` continuarem subindo, voltar ao orçamento de RAM / texturas; copiar VitaGL não resolve.
4. Validar visualmente menu, personagens, texto, abertura e cores **em PPSSPP e, quando possível, hardware** antes de integrar na branch principal.

**Aviso**: compilação pelo GitHub Actions atesta somente que o EBOOT foi gerado. FPS e fidelidade visual precisam de teste real; não há promessa de 30/60 FPS.
