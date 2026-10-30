# BT3 PSP — malhas de arquivo original para GU, etapa conservadora

**BUILD-ID:** `TP3-ORIGINAL-MESH-VIF-20261029`

## Conversão que realmente foi implementada

O código decompilado revela os registros originais:
- `include/sys/gfxm_e.h`: `ObjMdlFile.meshOfs` em `0x6C`,
  lista encadeada de `ObjMdlMesh`, osso em `+0x0A`,
  flag de habilitação em `+0x08`, cadeia VIF em `+0x60`;
- `src/sys/gfxm_e_c.c`: comando VIF `0x6C058000`, duas
  referências TEX0 (base e shade), material e PSM;
- `port/src/gs/gs_vu1.c`: VU1 programa 0/1 usa
  `TOPS+5` com **3 V4-32 por vértice** (posição/weight,
  normal/flags, UV).

`tools/psp_bt3_mesh.py` lê **diretamente** um modelo de
`DATA/PZS3US*.AFS` ou um arquivo do modelo extraído; valida
os links dos submodelos e termina no marcador DMA de cada VIF.
Para lotes inequivocamente padronizados, gera diretamente
`fighter_<hash>_<count>.f3p` (o mesmo formato nativo de
dados que o EBOOT aceita), **sem capturar pacotes durante uma
batalha**.

O manifesto `original_mesh_manifest.json` lista os submodelos,
seus IDs de osso, offsets originais, SHA da cadeia VIF, TEX0/PSM de
base e shade, os pacotes exportados e os tipos não suportados.
As informações de material também podem ser lidas de um
`TexFile` do jogo via `--texture-source` e `--texture-index`.

Exemplo (índices ilustrativos; não são valores conhecidos do disco):

```bash
python3 tools/psp_bt3_mesh.py \
    --source DATA/PZS3US0.AFS --index INDICE_MODELO \
    --output native_export
```

Exemplo com a tabela de texturas original:

```bash
python3 tools/psp_bt3_mesh.py \
    --source DATA/PZS3US0.AFS --index INDICE_MODELO \
    --texture-source DATA/PZS3US0.AFS --texture-index INDICE_TEXFILE \
    --output native_export
```

Copie **apenas** os `.f3p` gerados ao diretório
`PSP/GAME/BT3/native/` do PPSSPP e crie ali um arquivo vazio
`enable-fighter-packets.txt`. Não é necessário
`enable-fighter-capture.txt` (não crie esse arquivo no mesmo
teste). Reinicie o EBOOT para que a configuração de carregamento
seja reconhecida.

A rota nativa PSP GU que já processa os lutadores reconhece
os `.f3p` pelo hash e tamanho, exige coincidência **de todos os
bytes** com a fonte VU do quadro, e **só então** aplica os ossos
animados, iluminação e material TEX0 atuais. Sem coincidência,
continua usando os dados originais. Portanto os arquivos
conseguem vir do recurso original de forma offline, mas
a execução ainda usa a lógica do jogo para confirmar a
identidade e determinar ossos/efeitos.

### Correção de cache

O carregador anterior guardava cada hash num conjunto fixo de 96:
depois disso, novos pacotes nunca mais eram pesquisados; além
disso, mesmo um pacote encontrado podia ser bloqueado quando
saía do cache positivo. Agora existe um cache positivo de 16
pacotes e um cache **negativo rotativo** de 256 entradas, que
não bloqueia para sempre novos personagens/submalhas. Os pacotes
continuam sendo verificados byte a byte contra a fonte viva
para não substituir modelo, cores nem materiais incorretamente.

## Testes e limites

`tools/test_psp_bt3_mesh.py` monta modelos e um AFS sintéticos
com a estrutura binária **real** ObjMdlFile/ObjMdlMesh:
cabeçalho, offsets, osso, VIF 5Q com TEX0, V4-32 TOPS+5, conteúdo
de vértices, `F3P1` final e rejeição de encodings desconhecidos
e arquivos truncados. `test_fighter_pack_miss_cache.c` testa mais
de 1024 fontes para detectar o antigo bloqueio permanente.

**Isto não significa que todas as malhas do BT3 foram
decodificadas.** A exportação só aceita blocos VIF V4-32
**diretos, sem mascaramento, com 3 QW por vértice**.
DMA/UNPACK adicionais, formatos compactados, strips com estado
não identificado, variações de material, malhas de cenário,
pesos adicionais, hierarquia e passes incompletos **continuam
precisando de uma implementação verificada com recursos
reais**. A ferramenta reporta `unsupported_vif_or_disabled`
em vez de inventar vértices. A vinculação dos ossos usa o
`node` verdadeiro da malha e as matrizes atuais do jogo.

Não há prova de aumento de FPS no PPSSPP nem equivalência com
Tenkaichi Tag Team. Ainda existe GS/VU1 de compatibilidade.
Os testes host verificam os layouts e o PSP SDK compila o
EBOOT, **não comprovam uma batalha estável em PSP real**.

Não incluímos ISO, firmware, modelos, texturas ou arquivos
proprietários. Preservamos `fe93677` e a reconstrução GS
PSMCT32/PSMT8 que corrigiu faixas coloridas.
