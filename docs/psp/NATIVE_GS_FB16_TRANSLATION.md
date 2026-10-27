# PSP GS — saída nativa de cor PSMCT16

**Branch experimental:** `port/psp-gs-fb16-20261025`,
derivada da PR #16 (tradução VU1 do cenário). `main`
permanece em `fe93677`.

## Implementação de um subconjunto real do GS do PS2

`port/psp/psp_gu.c` antes descartava **todos** os alvos
de framebuffer GS cujo `Gs_PsmBits(FRAME.PSM)==16`.
O PSP GU suporta `GU_PSM_5551`; GS `PSMCT16 (0x02)` tem
os mesmos campos R5 G5 B5 A1. Para esse caso de saída
direta de cor, o renderer agora usa o buffer PSP que já
havia sido configurado em GU 5551 (screen ou WORK).

A nova rota só é aceita se:

- `FRAME.PSM == 0x02` (PSMCT16; **não** PSMCT16S),
- o endereço de framebuffer não é o mesmo endereço de
  profundidade (`FBP != ZBP`),
- ambos os endereços GS estão no intervalo de 512 páginas,
- a opção `gs_fb16` está ativa.

Uma PR separada permite testar isso independentemente da
PR #16. A opção `gs_fb16=1` está ativa neste **EBOOT
experimental** e pode ser desativada em `bt3.txt`, se
existir. Não é exigido nenhum arquivo de configuração.

`psp_gs_fb16.h` contém as guardas e o mapeamento 5551.
`test_gs_fb16.c` verifica todos os 64 valores PSM e as
256 intensidades de cor e alfa, inclusive a proibição de
depth alias e PSMCT16S.

## Limitações ainda não resolvidas

**Isto só converte o destino de desenho para cor 16 bits;
não converte toda a semântica do GS.** As páginas virtuais do
PS2 continuam usando os endereços originais, enquanto os
destinos de tela e WORK são linearizados no PSP.

- PSMCT16S tem endereço/swizzle diferente e continua
  bloqueado.
- ZBUF / depth-as-texture e FBP==ZBP continuam bloqueados.
- Framebuffer auxiliar WORK único ainda perde imagens
  de múltiplos alvos PS2 quando o dono muda.
- `GsGpu_FbUpload` **continua sem cópia real** de pixels;
  fades e outros efeitos que leem de volta o framebuffer
  podem produzir faixa cinza ou imagem errada.
- A cor RGBA5A1 não garante igualdade de blending,
  alpha test, textura, clamping ou mixagem com o PS2.
- Ainda é necessário testar em PPSSPP e PSP real.
  Não afirmar que o glitch desapareceu com CI verde.

## Como comparar sem bt3.txt

Instale primeiro o EBOOT experimental da PR #16 e teste
uma batalha; guarde o mesmo teste em vídeo e `log.txt`.
Depois instale o EBOOT da PR desta fase e faça **o mesmo**
início de combate (mapa, personagens, câmera e ações).
Ambos incluem o mesmo caminho nativo VU1 de cenário;
a diferença é apenas o suporte a cor FB16.

O log registra:

```
psp: GS PSMCT16 colour output: native=... legacy-skipped=... option=1
```

Se `native=0`, o jogo não passou pelo novo caminho.
Compare `native`, `skipped_fb16`, `fb-alias`,
`skipped_depth` e FPS real por quadro. Se houver
regressão, restaure o EBOOT anterior; um
`bt3.txt` opcional com `gs_fb16=0` também desliga
a nova rota.

Esse mecanismo **não substitui todos os efeitos gráficos**,
apenas traduz um primeiro formato de saída direto do GS
para a GPU nativa PSP.
