# Conversão de recursos para PSP — primeira integração executável (29/10/2026)

## O que realmente funciona nesta versão
O projeto já tinha `tools/psp_native_assets.py` e um leitor experimental de
índices `T8P1` (PSMT8). Esta fase acrescenta:
- Conversão offline de texturas **GS expandidas** PSMCT32/24/16/16S para
  os formatos GU nativos 5551 (alfa somente 0 ou opaco) e 8888 (alfa parcial).
  Usa a mesma escala de alfa do PSP do renderizador atual.
- Formato `N4P1` (52 bytes de cabeçalho + texels já convertidos),
  indexado por TBP/TBW/PSM, geração real de páginas GS, TEXA e dimensões.
- Leitor integrado em `texture_get()`, **antes** da decodificação GS.
  Verifica cabeçalho, FNV do arquivo e 17 pixels contra os dados atuais
  da memória GS. Caso falte, esteja corrompido ou seja uma textura dinâmica,
  volta automaticamente ao decodificador original.
- Ferramenta `bundle` que converte um manifesto de recursos de uma vez,
  emite `native_manifest.json` e por último `enable.txt`.
- Código e testes para `M3P1` (malhas) e `A3P1` (animação) já existentes
  permanecem disponíveis, mas **o executável ainda não usa esses
  intermediários diretamente na GPU**.

## Como preparar seus próprios recursos

Não incluímos ISO, assets, meshes ou texturas do jogo neste repositório.
Você precisa fornecer bytes já extraídos e identificar o recurso
correspondente no GS. O arquivo de entrada `expanded.rgba` para
`color` NÃO é um PNG comum: é um bloco linear **R,G,B,A com alfa nas
unidades GS**, ou seja, o resultado de `Gs_Expand()` para a imagem
original e TEXA correspondentes. Converter pixels arbitrários de PNG
sem esses cuidados produziria cores e transparências diferentes.

Exemplo de `assets.json`, com valores de TBP/TBW/geração/TEXA
apenas ILUSTRATIVOS (substitua pelos observados na sua execução):

```json
{
  "assets": [
    {
      "type": "color", "source": "expanded.rgba",
      "width": 128, "height": 128,
      "tbp": 336, "tbw": 8, "psm": 0, "gen": 305419896,
      "texa": 0
    },
    {
      "type": "t8", "source": "indices.raw",
      "width": 128, "height": 128,
      "tbp": 368, "tbw": 8, "gen": 305419896
    },
    {"type": "mesh", "source": "mesh.json", "output": "mesh.m3p"},
    {"type": "animation", "source": "anim.json", "output": "anim.a3p"}
  ]
}
```

Executar no PC ou ambiente Python que tenha acesso aos arquivos extraídos:

```bash
python3 tools/psp_native_assets.py bundle --manifest assets.json --output native
```

Copiar o conteúdo de `native/` para
`ms0:/PSP/GAME/BT3/native/` do cartão/PPSSPP. O leitor considera
`enable.txt` para ativação (não usa `bt3.txt`).

Para um único recurso:
```bash
python3 tools/psp_native_assets.py color --source expanded.rgba \
  --width 128 --height 128 --tbp 336 --tbw 8 --psm 0 \
  --gen 0x12345678 --texa 0
```

## Limites e validação

- `PSMT8` pré-convertido permanece separado e utiliza seu próprio
  índice `T8P1` e CLUT da memória GS ao vivo.
- `N4P1` só é usado para texturas de cor com tamanho original igual
  ao tamanho GU, sem variantes especiais de fontes brancas.
- Recursos dinâmicos/retornos do framebuffer PSMCT32→PSMT8 não podem
  virar pacotes estáticos confiáveis. A ponte de memória GS permanece.
- Uma geração ausente ou incorreta rejeita o pacote; não a invente.
- **Não há conversão automática dos arquivos AFS originais, nem
  carregamento nativo de malhas/animações, nem desempenho Tag Team
  comprovado nesta versão.** Continuamos precisando da engenharia
  reversa dos formatos reais e de instrumentação no jogo.
- Este PR não remove GS/VU1 nem dependências sem substituição funcional.
  `fe93677` (branco das fontes) e a ponte PSMCT32/PSMT8 ficam intactos.

Build-ID: `TP3-OFFLINE-GU-COLOR-20261029`. Os logs indicam quantos
pacotes nativos foram carregados/recusados. Os testes automatizados
confirmam empacotamento e leitura do cabeçalho, não a conversão de
uma ISO inteira ou FPS real do PSP.
