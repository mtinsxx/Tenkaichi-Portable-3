# PSP — corrigir somente o que for necessário

Branch: `fix/psp-necessary-only-20261025`, derivada da investigação da PR #7.
Base que já funcionava: `fe93677`, **não modificada**.

## Decisão baseada no código

O relatório `SYMBOLS_104_AUDIT.md` identificou os 104 símbolos originais:
- `sceCdSearchFile` e `ObjSeam_TransformVtx` são as duas funções
  diretamente importadas pelo código compilado do jogo. As duas foram
  implementadas na PR #7 e produziram um build PSP com 102 stubs.
- As **102 referências restantes** estão em duas tabelas antigas do
  middleware do PS2, não em chamadas diretas de C:
  `port/data/cod_1C3400.data.s` e `port/data/cod_1F44C0.rodata.s`.
  São **103 relocations de 102 símbolos distintos**, em 15 grupos.
  Uma tabela pode ser invocada indiretamente, portanto isso não prova
  que as funções sejam mortas.

**Nesta etapa não removemos e não inventamos nenhuma das 102 funções.**
O build mantém as referências e stubs explícitos para detectar uma
chamada que realmente precise ser portada.

## Proteções adicionadas

- `port/psp/check_missing_tables.py` roda antes do link. Ele verifica
  que cada símbolo pendente continua somente nas duas tabelas originais.
- O linker já verifica que qualquer **nova chamada direta** a um símbolo
  ainda não implementado aborta a compilação, além de detectar mudanças
  na lista `expected_stubs.txt`.
- O stub de diagnóstico passa o endereço de retorno MIPS para
  `Port_MissingAt`. Se alguma função antiga for chamada, **a execução
  pára com o nome da função e o endereço do chamador em `log.txt`**.
  Não tenta retornar sucesso falso.
- O workflow PSP gera o EBOOT e um artefato separado
  `BT3-PSP-SYMBOL-DIAGNOSTICS` com `bt3.map` e as listas de símbolos
  para localizar o endereço da função que tentou chamar o stub.

## Como decidir se uma das 102 precisa ser portada

1. Instalar o EBOOT experimental preservando uma cópia do EBOOT funcional.
   Ativar **Memory Stick** no PPSSPP Android, pois o jogo carrega seus
   arquivos do diretório `disc/`.
2. Verificar inicialização, menu, seleção, batalha, transformações,
   efeitos, áudio, filme, carregar/salvar. Um build no GitHub não verifica
   execução do jogo.
3. Se houver tela de erro `missing PS2 callback`, fornecer
   `ms0:/PSP/GAME/BT3/log.txt` e a captura; identificar a função pelo
   nome e o chamador via `bt3.map` do mesmo build.
4. Implementar **somente a função comprovadamente usada**, respeitando
   seus argumentos e resultado, numa branch própria, e repetir os
   testes. Nunca apagar ponteiros nem substituí-los por retorno 0.
5. Antes de integrar qualquer mudança, comparar com `fe93677`.
   Validar no PSP real de 64 MiB antes de declarar estabilidade.

## Critério objetivo

Sem evidência de que uma callback antiga seja executada, **nenhum novo
código de emulação deve ser adicionado**. A existência de 102 símbolos
em tabelas não é sinônimo de 102 bugs de gameplay nem de 102 funções
obrigatórias para o PSP.

Esta análise é estática. Ainda não foi demonstrada por PPSSPP/PSP real
a inacessibilidade das tabelas; nenhuma das 102 é declarada corrigida.
