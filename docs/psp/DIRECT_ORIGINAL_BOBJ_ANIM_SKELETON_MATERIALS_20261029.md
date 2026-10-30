# Conversão direta de recursos BT3 originais — BPE, esqueleto, animações e materiais

Data: 2026-10-29. Build `TP3-BT3-BPE-SKELETON-20261029`.

## O que agora é decodificado de fato

O código decompilado do BT3 já descreve os formatos:

- `src/sys/bpe.c` — expansão BPE de arquivos originais;
- `include/battle/bobj_a.h` e `src/battle/bobj_a.c` — `BobjAnim`,
  deslocamentos de 71 trilhas, chaves de posição, quaternions
  `Quat_Pack`, quadros de animação, e `BobjBone` (id, repouso e
  encadeamento por bytes);
- `src/sys/math3d.c` — desempacotamento de quaternions em 20 bits
  por componente e reconstrução da matriz de rotação;
- `include/sys/gfxm_b_b.h` — `TexFile` e entradas `TexEntry` de
  64 bytes, incluindo TEX0, PSM, largura/altura, offset de pixels,
  offset de CLUT e parâmetros de upload.

`tools/psp_bt3_original.py` usa esses layouts para decodificar o
**arquivo original do jogo** (não pacote fictício capturado de VU1).

### Animações reais

A ferramenta percorre as 71 possíveis IDs de nós, checa as trilhas
`BobjAnim` e suas chaves, desempacota os quaternions originais e
amostra posição/rotação nos quadros originais. Com o esqueleto
`BobjBone` do mesmo modelo, emite **A3P1 nativo para o PSP**, com
matrizes locais de 3x4 floats por osso/quadro. Esta saída pode ser
carregada pela prévia PSP GU já incluída no EBOOT; o carregador
nativo agora aceita até **71 ossos**, em vez de 32.

Use arquivos próprios extraídos do seu disco. Exemplo de animação
BPE e modelo extraídos:

```bash
python3 tools/psp_bt3_original.py animation \
  --source motion.bin --bpe \
  --model character_model.bin --bones-offset 0xOFFSET_VERIFICADO \
  --output native/preview.a3p
```

`0xOFFSET_VERIFICADO` **não é um endereço universal**: deve ser o
offset dentro daquele arquivo do primeiro `BobjBone` do modelo.
O formato do contêiner de todos os modelos/costumes ainda não foi
mapeado a ponto de encontrar esse bloco automaticamente.

Também é possível **ler direto dos arquivos AFS originais**, sem
capturar VU1 nem extrair milhares de arquivos antecipadamente:

```bash
python3 tools/psp_bt3_original.py animation \
  --source DATA/PZS3US0.AFS --index INDICE_DA_ANIMACAO --bpe \
  --model DATA/PZS3US0.AFS --model-index INDICE_DO_MODELO \
  --bones-offset 0xOFFSET_VERIFICADO \
  --output native/preview.a3p
```

O usuário precisa descobrir os índices corretos dos arquivos de
modelo/animação. A ferramenta recusa índices, offsets, tamanhos e
chaves inválidos; não supõe que todo arquivo AFS seja um esqueleto.

### Materiais GS originais

```bash
python3 tools/psp_bt3_original.py materials \
  --source texture_file.bin --output native/materials.json
```

A saída contém PSM, dimensões, TEX0, offsets/tamanhos das regiões
de pixels e CLUT, e bytes atribuídos ao upload. Não é ainda um
material completo da PSP GU: os pixels GS ainda precisam ser
decodificados e vinculados aos submeshes reais. É uma tradução da
**tabela original de materiais**, não uma imagem inventada.

## Validação e limites

`tools/test_psp_bt3_original.py` cria um BPE binário legítimo,
trilhas BT3 completas e rotation-only, encadeamento `BobjBone`,
material `TexEntry` e arquivo AFS sintético, e verifica
conversão `A3P1`, matrizes, bytes e rejeição de arquivos inválidos.
Ambos workflows PSP executam o teste antes de compilar.

A3P gerado contém **poses locais**, não os estados finais de
múltiplas camadas com blending, retargeting, restrições, movimentos
secundários, relações de pai e pesos de skinning. É possível usá-lo
para a prévia de malha `preview.m3p` já existente (com
`enable-preview.txt`), **não** para substituir
automaticamente os lutadores da batalha. Sem o mapa completo de
`submesh → bone IDs/weights → material`, fazer essa substituição
corromperia a geometria.

**Ainda não existe um decodificador completo do contêiner de malhas
proprietárias de todos os personagens.** A ponte opcional com pacotes
VU1 F3P1 continua como referência do jogo e o renderizador legado
continua no combate. Esta versão não garante FPS ou imagem igual
ao Tag Team. A melhoria gráfica anterior (PSMCT32/PSMT8 e
correções `fe93677`) é preservada.

Nenhuma ISO, arquivo de personagem ou pacote comercial foi incluído.
