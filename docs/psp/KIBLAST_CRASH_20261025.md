# PSP — batalha trava em BtlKiBlast_MapReact (vídeo PPSSPP, 25/10/2026)

## Sintomas demonstrados na gravação do usuário

A gravação mostra a cena 3D do estágio em alguns momentos, mas
personagens ausentes, grandes faixas cinzas/texturas corrompidas, HUD
com detritos gráficos, a mensagem `Ready!`, em seguida `Fight!`, e
por fim o diagnóstico roxo do PPSSPP:

- `Game crashed / Invalid Memory Access`;
- `Access: Read Word at 00000994 (sz: 4)`;
- `PC: 088b90e0`, função `BtlKiBlast_MapReact`;
- thread `bt3_game`;
- stack também mostra `task_update.isra.0` e `EftDet_NoneVsFighter`.

**Conexão concreta com o código:** `BtlKiBlast_MapReact` chama
`BtlMember_HasAbility(chr, 0x4B)`. A estrutura
`include/battle/btl_char_member.h` define
`BtlMemberChr.active` exatamente em **offset 0x994**.
Portanto, ler o endereço `0x00000994` é consistente com a rotina
de habilidade recebendo `chr == NULL`. Não prova *qual* chamada
repassou o ponteiro inválido sem observar os registradores MIPS.

## Causa provável: avaliação de argumentos com saídas ainda não preenchidas

A implementação anterior em `src/battle/btl_tech_b.c` tinha chamadas como:

```c
BtlTechBChr *chr;
s32 kind;
return BtlKiBlast_MapReact(
    chr, BtlKiBlast_GetOfHit(hit, &chr, &kind)->react, kind);
```

`chr` e `kind` são lidos como argumentos de `MapReact` na
**mesma expressão** em que `GetOfHit` preenche seus endereços.
Em C, a ordem de avaliação dos argumentos não é garantida.
O port PSP tenta traduzir a ordem de avaliação PS2 via `ptr32.r2l`,
mas a compilação anteriormente observada registrou
`r2l unavailable: No module named 'clang'; compiling as it is`.
Isso torna a expressão insegura e pode explicar o acesso a 0x994.

O mesmo padrão ocorria em seis getters de ataque de Ki:
`GetKiCost`, `GetKiCostOfHit`, `GetSpeed`, `GetSpeedOfHit`,
`GetReact` e `GetReactOfHit`.

## O que a correção faz

Todos os seis avaliam explicitamente o getter e guardam a
`BtlKiBlastData *rec` **antes** da chamada final que consome
`chr`, `kind` e o campo do registro.

- Não há retornos falsos, `if (chr == NULL) return 0`, ponteiro
  mágico ou bypass de lógica de combate.
- Mantém a regra de original de reação, dano de Ki e velocidade.
- `port/psp/test_kiblast_eval.py` verifica estaticamente que os seis
  locais críticos continuam avaliando a saída primeiro.
- Workflows PSP executam esse teste antes de compilar o EBOOT.

## Critérios antes de aprovar ou integrar

1. Conferir a saída dos testes estáticos e do EBOOT PSP no CI do
   commit desta PR.
2. Comparar com a versão já testada, **mantendo Memory Stick ativo**;
   para isolar a falha, repetir mesma dupla/personagens e mesmo
   estágio do vídeo, sem alterar outras opções.
3. A cena deve ultrapassar `Fight!` sem `Read Word at 00000994`
   nem novo travamento, e permitir ataques de Ki e reação a golpes.
4. Compartilhar `ms0:/PSP/GAME/BT3/log.txt` e a tela de qualquer
   falha. Ainda pode haver outros casos em C que dependem da
   ordem de avaliação original (ativar validação r2l estrita mais
   amplamente é tarefa de outra etapa).
5. Tratar o sumiço dos modelos/texturas corrompidas **separadamente**
   (GS, VU1, VRAM/endereços); consertar esse crash não comprova
   qualidade gráfica nem aumento de FPS.

O build não executa o jogo: **correção de causa provável, não
solução de gameplay demonstrada**. A main/estável `fe93677`
permanece intocada. O repositório não inclui dados de jogo.
