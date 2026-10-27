# Investigação dos 104 símbolos pendentes no PSP

**Origem:** build PSP da base `fe93677` e da branch `fix/psp-audit-phase1-20261025`. O número de símbolos de link pendentes **não** equivale ao número de erros de gameplay.

## Prova de origem: `psp-nm -A -u` por arquivo-objeto

- **102/104** não aparecem como dependência indefinida de um arquivo C compilado; aparecem **apenas como relocations de tabelas** em `port/data/cod_1C3400.data.s` e `port/data/cod_1F44C0.rodata.s`. No primeiro arquivo há 92 ocorrências de relocação e no segundo 11; um símbolo aparece duas vezes no segundo, totalizando 103 ocorrências de 102 símbolos distintos.
- **1/104**: `sceCdSearchFile`, invocado em `src/sys/file.c:194` pelo identificador de disco. Implementado para o uso concreto de presença dos executáveis no PSP em `port/psp/psp_file.c`, com checagem real de existência dos arquivos. Não é implementação genérica de todos os campos `sceCdlFILE`.
- **1/104**: `ObjSeam_TransformVtx`, invocado em `src/sys/gfxm_d_c.c:88` quando o modelo tem seção de seam. O port não tinha essa rotina VU0. Acrescentada tradução para C por instrução no `src/port/vu0_a.c`, com símbolo de link do jogo via `port/src/vu0_names.h`; derivada da implementação original `Ref_ObjSeam_TransformVtx` do repositório AGPL [z3xox/Tenkaichi3Decomp](https://github.com/z3xox/Tenkaichi3Decomp/blob/main/src/port/vu0_a.c). O hook exclusivo do renderizador desktop `Port_SeamExact` não existe no PSP e foi omitido. A rotina deve ser testada em modelos com seam antes de qualquer merge.

## As 102 referências de tabelas

Elas aparecem em **15 grupos de endereços**, com 103 ocorrências de relocação para 102 símbolos distintos, como abaixo. O fato de serem dados não demonstra que seus ponteiros são inacessíveis em execução; poderiam ser lidos por dispatch indireto.

| Tabela / objeto | Ponteiros não resolvidos |
|---|---:|
| `D_002D3FF8` (`cod_1C3400.data.s`) | 14 |
| `D_002D9800` | 14 |
| `D_002DE590` | 9 |
| `D_002DEA78` | 9 |
| `D_002E2AB0` | 9 |
| `D_002E6F68` | 14 |
| `D_002E7788` | 8 |
| `D_002E77A8` | 8 |
| `D_002E9D50` | 2 |
| `D_002EA510` | 1 |
| `D_002EA528` | 1 |
| `D_002EAC98` | 1 |
| `D_002EB080` | 1 |
| `D_002EB190` | 1 |
| `D_002FAA08` (`cod_1F44C0.rodata.s`) | 11 |
| **TOTAL (ocorrências; uma repetida)** | **103** |

Dessas 102 referências, 95 são rótulos `func_XXXXXXXX`, e 7 são membros nomeados das famílias de I/O `dvCi*` (3), `mfCi*` (2) e `htCi*` (2).

Os nomes `func_...` sozinhos **não determinam contratos de parâmetros nem o comportamento da implementação original**. Não inventar retornos zero/1; não apagar as tabelas nem zerar seus ponteiros para forçar 0 stubs no log: isso quebraria futuras chamadas indiretas. O build continua criando `Port_Missing` para endereços ainda não portados, mas agora há um mapa automático **símbolo → arquivo-objeto** para facilitar a identificação.

## Próximas condições de aceite

1. Compilar a versão com a tradução de seam e confirmar que **apenas as 102 referências das tabelas** permanecem sem resolução.
2. Testar no PPSSPP modelos que usam seção `seamOfs`, incluindo roupas com capa e personagens afetados, comparando pele, sombra, desenho de triângulos e `log.txt`; a tradução foi comparada em outras builds, mas **não** nesta branch.
3. Investigar referência dos 15 grupos de ponteiros nas rotinas de inicialização e I/O. Confirmar quais as rotinas ficam inacessíveis com o shim PSP atual, e quais são chamadas indiretamente. Só então portar entradas necessárias ou suprimir seções demonstradamente mortas.
4. Rodar menus, combate, transformações, vídeos, gravação/reload; ausência de `Port_Missing` no uso esperado é critério para uma build funcional, mas **não garante 100% do conteúdo sem cobertura de teste**.
5. Seguir com o port de funções reais sem redistribuir conteúdo do jogo. Manter `main` e `stable/psp-fe93677-working-menus` intocadas.

**Estado:** investigação técnica e implementação experimental; não se deve anunciar as 104 funções como resolvidas ou o jogo como 100% convertido antes dos testes correspondentes.
