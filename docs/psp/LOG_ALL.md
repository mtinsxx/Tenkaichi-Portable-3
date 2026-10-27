# PSP renderizador — log_all=1 (diagnóstico completo dos caminhos instrumentados)

O port ainda não consegue determinar automaticamente a causa de qualquer pixel preto. Este modo registra **todos os eventos observados nas categorias instrumentadas** com contagem e uma amostra inicial por categoria, a cada ~2 segundos. Não substitui uma captura de comandos da GPU nem garante descobrir todos os defeitos.

## Ativação

No Memory Stick do PPSSPP, edite `PSP/GAME/BT3/bt3.txt`:

```ini
log_all=1
report=1
log_flush=0
gs_depth=2
```

Reinicie o EBOOT e reproduza o defeito por 10–20 segundos. Feche o jogo normalmente para preservar a última descarga do arquivo `PSP/GAME/BT3/log.txt`. Envie esse arquivo, mais uma imagem do cenário preto. `log_all=0` desativa o diagnóstico e evita seu custo durante jogo normal. **Não desative depth nem mude texturas/modelos** neste teste.

## O que aparece no log

Linhas começando por `psp: LOG_ALL` incluem:
- `stage-*`: VU1 program 399 rejeitado (argumentos, CLIP/W invalidado, projeção fora do domínio, clipper sem suporte, memória do fan), fallback ao interpretador, aceito, trivialmente invisível e triângulos enviados;
- `draw-skipped-*`: GS color framebuffer 16 bits não suportado, coincidência FRAME/ZBUF, PSM especial, falha na decodificação ou no destino;
- `fb-texture-*`: framebuffer em uso como textura, geração VRAM modificada, ausência de pixels renderizados, autoalias, WORK sobrescrito, outros impedimentos e leitura válida;
- `fb-work-surface-replaced`: duas imagens lógicas disputaram o mesmo WORK físico;
- `native-effect-not-implemented` / `framebuffer-upload-not-implemented`: callbacks reais alcançados, mas ainda não implementados.

Cada categoria registra `count`, `first-frame`, `last-frame`, a primeira amostra com `fbp`, `zbp`, `tbp`, `texpsm`, `test`, `prim`, `ctx`, `detail`. `detail` é o índice do vértice/triângulo em falhas de VU1, o ID do efeito ou o buffer envolvido, conforme a categoria. Os registros de FPS real, tempos de renderização, textura, GS, GE/VBlank e VU1 do relatório tradicional permanecem ativos.

## Custos e limites

- Sem uma linha por desenho — contadores em RAM e um único `Psp_LogFlush` ao fim de cada intervalo.
- O novo modo adiciona trabalho por chamada, especialmente com 30 mil triângulos por quadro; FPS do modo diagnóstico pode ser inferior ao modo normal.
- Não intercepta pixels depois da rasterização, não faz screenshot da GE e não prova que o framebuffer final está correto.
- A instrumentação **não altera** cores, z-buffer, sprites, texturas, animações ou os modelos dos lutadores.
