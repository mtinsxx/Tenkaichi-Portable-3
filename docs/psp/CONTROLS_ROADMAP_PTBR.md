# Tenkaichi Portable 3 — controles PSP/PPSSPP (perfil experimental)

## Objetivo e seguranca de regressao

O jogo continua com o perfil `legacy` como **padrao**: o mapeamento anterior nao e alterado.
O perfil opcional `portable` busca comandos confortaveis em um PSP fisico ou no PPSSPP Android.
Todos os comandos continuam sendo enviados como **entradas normais do controle de PS2**:
sem cheats, golpes automaticos ou fusoes desbloqueadas artificialmente.

Em `ms0:/PSP/GAME/BT3/bt3.txt`:

```ini
controls=portable
```

Para voltar ao funcionamento anterior, use `controls=legacy` (ou remova a linha) e reinicie o EBOOT.

## Mapeamento de combate (portable)

| PSP | Sinal de PS2 | Funcao esperada |
|---|---|---|
| Analogico | Analogico esquerdo | Movimento e direcao dos golpes |
| Direcional cima/baixo | R1 / R2 | Voar para cima / para baixo |
| Direcional esquerda/direita | Direcional esquerdo/direito | Comandos de deslocamento |
| Quadrado | Quadrado | Ataque |
| Triangulo | Triangulo | Ki Blast |
| X | X | Dash |
| Circulo | Circulo | Defesa |
| L | L2 | Carregar Ki |
| R | L1 | Procurar/travar o adversario |
| **L + direcional cima/baixo** | L2 + direcao original do PS2 | Golpes especiais e Ultimate (com Triangulo etc.) |
| **L + R + direcional cima/baixo** | L2 + R1/R2 | Voo vertical enquanto carrega Ki, sem forcar lock-on |
| START | START | Pausar |

Segurar L e apertar cima/baixo em combate **nao voa**: envia direcao real para golpes
como L2 + cima + Triangulo e L2 + baixo + Triangulo. Para voar enquanto carrega
Ki, use L + R + cima/baixo. Esse desvio preserva as receitas de ataque do BT3.

## SELECT: transformacoes, fusoes, troca e botoes ausentes

Ao iniciar um gesto com SELECT, o port espera ate **3 amostras de controle** antes
de enviar R3 se nenhum outro botao vier. Um toque rapido em SELECT gera um pulso R3
quando SELECT e solto. Se o atalho for iniciado nesse intervalo, nao dispara
um R3 isolado primeiro. Uma vez escolhido o atalho, ele permanece fixo ate soltar
SELECT: mudar os outros botoes nao o converte acidentalmente em transformacao.

| Gesto no PSP | Botao(s) de PS2 enviado(s) | Finalidade |
|---|---|---|
| **SELECT sozinho (toque ou segurado)** | R3 | Transformacao (segundo personagem/requisitos) |
| **SELECT + ↑/↓/←/→** | R3 + direcao original do PS2 | Escolher transformacao ou fusao conforme a Skill List |
| **SELECT + L + direcional** | R3 + direcao original, sem L2 | Variante ergonomica do comando direcional de fusao |
| **SELECT + L** | R3 | Acesso mais confortavel ao R3 (nao executa fusao sozinho) |
| **SELECT + Quadrado** | L3 + R3 | Trocar personagem de equipe, se permitido |
| SELECT + Triangulo | L3 | Clique do analogico esquerdo isolado |
| SELECT + Circulo | SELECT original do PS2 | Recuperar o SELECT sem enviar R3 |
| SELECT + R | SELECT original do PS2 | Alternativa para SELECT original |
| SELECT + R + direcional | Analogico direito do PS2 | Segundo analogico, inclusive diagonais |
| SELECT + L + R | R3 + L2 | Acesso avancado a essa combinacao, sem direcional |
| SELECT + START | SELECT original + START | Preserva a combinacao de sistema/menu |

**Fusao nao significa apertar SELECT+L aleatoriamente.** No BT3, a fusao e uma
transformacao especial: depende do personagem, parceiro compativel, modo de equipe,
requisitos, recursos e da direcao mostrada na lista de golpes. Os atalhos apenas
facilitam o acesso a R3 + direcao. Nao ha logica que force ou simule a fusao.

### Gestos e timing

- **Pressione a combinacao proximamente** (especialmente SELECT+Quadrado, SELECT+R
  e SELECT+L+direcao). O intervalo de protecao e de 3 amostras, nao um sistema de
  reconhecimento de gestos com tolerancia ilimitada.
- Para iniciar um novo comando especial, **solte SELECT primeiro**.
- Segurar SELECT por mais de 3 amostras ja comeca R3; mudar para um novo atalho
  muito depois disso pode ter resultado inesperado. Esse caso ainda precisa de
  teste em partidas reais, inclusive transformacoes por hold/release.
- Em **PPSSPP com tela de toque**, alguns gestos de 3 botoes podem ser incomodos
  ou depender do layout dos controles virtuais. O mapeamento nao obriga plugins
  ou macros no PPSSPP, mas talvez valha simplificar o overlay mais tarde.

## Menu de pausa e outras telas

O novo mapeamento **so entra quando o jogo esta efetivamente em combate**.
O estado de batalha 3 nao basta: o port tambem verifica os sinalizadores reais
`BATTLE_FLAG_PAUSE`, `BATTLE_FLAG_PAUSE_MENU`, `BATTLE_FLAG_DEMO`,
`BATTLE_FLAG_READY` e `BATTLE_FLAG_LOADING`, alem de
`Battle_GetWork()->running`.

Assim, no menu de pausa / Skill List, o **direcional volta a navegar para
cima e baixo**, e SELECT volta ao comportamento anterior. Quando o jogador
retoma a luta segurando SELECT, e necessario soltá-lo antes de iniciar novo
gesto R3, para nao transformar sem querer.

Em selecao de personagem, opcoes e menus normais, o mapeamento legado e mantido.
Se outros overlays nao usarem esses sinalizadores conforme esperado, deverao
ser analisados no PPSSPP.

## Estado dos testes e limites

- O codigo `port/psp/psp_controls.h` e portavel C99 e tem regressao em
  `port/psp/test_psp_controls.c`. Testa TODAS as 4096 combinacoes fisicas de
  botoes do perfil legado, o perfil portable fora de luta e sequencias de
  SELECT/R3, direcao, equipe, fusao, menu de pausa e camera.
- `.github/workflows/psp.yml` executa o teste no host antes do EBOOT.
- **Esses testes medem os sinais enviados para o PS2, nao a jogabilidade.**
  A construcao do EBOOT e os combos no PPSSPP/PSP continuam pendentes enquanto
  o GitHub Actions estiver bloqueado para a conta.
- Controles customizados dentro do proprio BT3 podem alterar a interpretacao
  dos botoes; e preciso validar esse caso em jogo.
- E impossivel representar todas as combinacoes simultaneas de um DualShock
  com menos botoes fisicos sem algum modificador; o foco e cobrir todos os
  comandos importantes sem conflito e oferecer rollback.

## Proximas validacoes

1. Compilar EBOOT PSP e confirmar abertura, menus, batalha, pausa e retorno a luta.
2. Validar Ki Blast, ataque, dash, defesa, carga de Ki, subida, descida, lock-on,
   Blast 1/2, Ultimate e comandos direcionais.
3. Testar transformacoes por toque/hold e fusoes validas usando a Skill List,
   a troca de equipe, camera e todos os comandos restantes em PPSSPP Android.
4. Depois que isso estiver estavel, criar ilustracao original de controles do
   PSP em 480x272, sem as imagens herdadas do Vita.

## Creditos

Tenkaichi Portable 3: adaptacao e manutencao PSP por mtinsxx.
Projetos-base e autores: arielxflores (Tenkaichi3Decomp-Vita), z3xox e
colaboradores (Tenkaichi3Decomp), autores da decompilacao do BT3, Spike
(desenvolvedora original) e pspdev. Veja `README.md` e `THIRD_PARTY.md`.
Esses creditos nao eliminam eventuais restricoes dos direitos autorais do jogo.
