# BT3 PSP — correção estrutural do espelhamento GS e geometria inválida (30/10/2026)

Build ID: `TP3-GS-CPU-MIRROR-FAST-20261030`.

## Problema confirmado no código

A ponte GS de 4 MiB que preserva o feedback de framebuffer
PSMCT32 → PSMT8 executa rasterização por CPU para os produtores
observados, incluindo triângulos e sprites do cenário, HUD e
efeitos. Mesmo depois do SCISSOR, cada pixel candidato fora de
um triângulo disparava duas divisões de precisão dupla para
interpolação barycentric antes de constatar que não pertencia à
geometria. Além disso, até os desenhos opacos liam o endereço
swizzled do framebuffer de destino, apesar de seu conteúdo não
ser necessário sem GS ALPHA blending.

A geometria nativa do stage também validava ST e Q antes da
divisão em perspectiva, mas não rejeitava uma textura
**infinita após a multiplicação por 1/W**.

## Alterações executáveis

1. **GS raster CPU, triângulos:** cálculo das MESMAS duas funções
   `edge()` por pixel e verificação do sinal das duas primeiras
   barycentric weights ANTES das divisões. Se qualquer peso é
   negativo, o teste final original sempre rejeitaria o pixel:
   nenhuma textura/CLUT/profundidade é processada para ele.
   Pixéis cobertos mantêm EXATAMENTE os mesmos double weights,
   tolerância do terceiro peso, depth rounded, alpha, stencil,
   máscaras de GS VRAM, cores e textura.
2. **Leitura do framebuffer:** só executa
   `vram_rw(...,read)` de destino caso `PRIM.ABE` indique que
   o resultado usa os bytes do destino no blending PS2.
   `FRAME.FBMSK` permanece intacto, inclusive leituras
   adicionais quando a máscara é parcial.
3. **GS no-op e robustez:** `FRAME.FBMSK=0xffffffff` junto com
   `ZBUF.ZMSK=1` significa que o desenho não altera nenhum
   byte de memória GS. O mirror pode ignorar todo esse desenho;
   não altera a renderização da GPU nativa. A interpolação
   inválida ou coordenadas fora da área real do GS não são
   convertidas indefinidamente para `int` do Allegrex.
4. **Stage GU:** o caminho nativo rejeita ST/Q infinitos
   após a divisão por W. Em caso inseguro, o jogo recorre ao
   interpretador original, em vez de enviar UV infinito à
   GPU. Isso é uma **proteção contra uma classe de defeito**,
   não prova que todos os polígonos enormes ficaram corretos.
5. **Logs:** `GS CPU mirror optimization:` mostra candidatos
   rejeitados cedo, leituras de destino omitidas, draws
   efetivamente sem escrita e limites inválidos.

## Validação

`port/psp/test_gs_mirror_fast.c` compara o teste original dos
pesos com a nova rejeição em centenas de triângulos aleatórios,
ambos sentidos, bordas e os pesos/depth exatos, e verifica
máscaras de no-op e coordenadas inválidas. O teste existente
`test_stage4.c` agora inclui ST finito que transborda APÓS
a divisão pelo W. Ambos workflows PSP executam testes host
antes do build com PSPSDK.

## O que não podemos afirmar ainda

- Não tivemos o `log.txt` da gameplay nesta iteração para
  determinar milissegundos economizados. Um número alto de
  `early-off-triangle` indica trabalho evitado, não FPS medido.
- VU1 stage399, efeitos program6/451, uploads/GL texel e
  custos de framebuffer GPU/CPU continuam existindo.
- Esta alteração não elimina o trabalho restante do jogo
  nem promete 30/60 FPS ou desempenho de Tenkaichi Tag Team.
- Sem rodar no PPSSPP com o mesmo cenário, não é possível
  afirmar que os defeitos gráficos já foram resolvidos.
- A reconstrução GS exata que corrigiu texturas, HUD/fontes
  corrigidos em `fe93677`, modelo automático, telas
  e comandos originais ficam preservados.

Teste o mesmo combate e envie `PSP/GAME/BT3/log.txt`.
Priorizar a próxima remoção de stage399/GS dependerá dos
registros reais `PROFILE GS draws`, `PROFILE VU1 programs`,
`GS CPU mirror optimization` e `PROFILE interval`.
