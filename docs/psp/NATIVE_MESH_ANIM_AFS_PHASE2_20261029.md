# BT3 PSP — M3P1/A3P1 na PSP GU e extração AFS (teste 29/10/2026)

## O que está implementado

Esta é uma **integração executável e opcional** para confirmar que o
PSP pode carregar os dados de malhas M3P1 e matrizes A3P1 gerados pelo
conversor existente e enviar triângulos diretamente à PSP GU.

- `psp_native_mesh_assets.h`: valida integralmente contagens,
  tamanhos, índices, coordenadas finitas e frames/bones de M3P1/A3P1;
  matrizes A3P1 de 3×4 floats fazem a transformação rígida no Allegrex.
- `psp_native_mesh_preview.c`: um módulo nativo novo, compilado com o
  EBOOT, carrega `preview.m3p` e `preview.a3p` quando ativado
  explicitamente. Monta os triângulos indexados como vértices PSP GU
  (cor 8888 + UV + XYZ) e chama `sceGuDrawArray(GU_TRIANGLES,...)`.
  Usa buffers duráveis até o `sceGuSync` normal do frame.
- `tools/psp_afs_extract.py`: lê os diretórios
  `DATA/*.AFS` do jogo do usuário, valida os limites, produz
  `00000.bin`, `00001.bin` etc. e `afs_index.json`.
  **Não inclui/compartilha recursos proprietários e não decodifica
  automaticamente os formatos internos dos personagens**.
- Testes em C e Python + PSP CI. Nenhum arquivo do jogo foi adicionado.

## Como testar um modelo próprio na PSP GU

Gere, a partir de um JSON de malha compatível com
`tools/psp_native_assets.py mesh`, um arquivo `preview.m3p`.
As posições do teste devem estar no intervalo aproximado `[-1,+1]`.

Para a animação, opcionalmente use
`tools/psp_native_assets.py animation --source anim.json --output preview.a3p`.
Este teste usa **somente a matriz do osso 0**, rigidamente, sem
skin weights de personagens.

Copie o arquivo M3P1 e, se houver, A3P1 para:
`PSP/GAME/BT3/native/`.
Crie `enable-preview.txt` na mesma pasta **somente quando quiser
ver a malha de teste**. Ela aparece em uma pequena região superior
direita, independente do combate.

Sem essa opção, o renderizador do jogo **não muda**. Não é preciso
`bt3.txt`.

## Extração dos arquivos do jogo

```bash
python3 tools/psp_afs_extract.py --source DATA/PZS3US0.AFS --output extracted/pzs3us0
```

Para entradas específicas, acrescente `--indices 0 7 24`.
O nome exato dos arquivos AFS deve corresponder ao disco do usuário.

A extração não basta para identificar modelo, esqueleto, coordenadas de
UV, pesos, animação, material e formato de cada arquivo. Esses
`*.bin` continuam precisando de decodificadores específicos antes da
conversão para M3P1/A3P1. Não há malhas ou dados proprietários no build.

## Limitações — crucial

**Não é a conversão completa do BT3.** O suporte demonstra loading e
GPU nativa, mas **não substitui** os modelos do combate nem liga
automaticamente ossos, animações e efeitos originais aos pacotes.
O formato M3P1 original não inclui pesos de skinning, materiais,
skeleton IDs, hierarquia óssea nem texturas vinculadas; não se pode
inventá-los. Também não significa FPS equivalente ao Tenkaichi Tag Team.

A ponte GS PSMCT32/PSMT8, cores de interface `fe93677`, stage VU1 e
compilação PSP anteriores ficam preservados.
As próximas etapas técnicas dependem de documentação/engenharia reversa
dos *arquivos originais de modelo e animação*, com testes de
equivalência antes de substituir o interpretador VU1.

Build ID: `TP3-MESH-ANIM-GU-AFS-20261029`.
