# LOG ALL — nativo no EBOOT.PBP

**Sem bt3.txt.** O diagnóstico foi incorporado diretamente ao executável
`TP3-NATIVE-LOGALL-20261027`. O `EBOOT.PBP` registra automaticamente o que
acontece com o renderizador quando você abre o jogo: `LOG_ALL` e relatórios
de FPS/GS/VU1/texturas. O teste de profundidade permanece no padrão
`gs_depth=2`, **não** é uma nova tentativa de corrigir o cenário preto.
Os controles `portable` também são embutidos.

## Como usar no PPSSPP Android

1. Copie o `EBOOT.PBP` para o Memory Stick em `PSP/GAME/BT3/`.
2. Mantenha os arquivos da sua própria cópia do jogo em `PSP/GAME/BT3/disc/`.
3. Abra o jogo e reproduza a tela preta por 10–20 segundos.
4. Saia normalmente e envie o `PSP/GAME/BT3/log.txt` completo.

O executável **não lê nem inclui `bt3.txt`**. Arquivos de saves e
`bt3_settings.txt` pertencem ao **jogo**, são diferentes da configuração
externa eliminada e continuam sendo preservados.

## Capturas disponíveis

Linhas `psp: LOG_ALL` contêm totais e a primeira ocorrência por
categoria de falha: validação de VU1/stage399, projeção e recorte;
triângulos aceitos/enviados; operações GS descartadas; texturas
não decodificadas; conflito e invalidação de framebuffers;
composição e efeitos nativos não implementados. Amostras de estado
incluem FBP, ZBP, TBP, PSM, TEST, PRIM e número do quadro.
O perfil tradicional também registra FPS real, tempos de GS/VU1,
decodificação de textura, GE e VBlank.

O coletor é **limitado** para evitar escrita por triângulo, mas pode
reduzir o FPS observado. Ele não vê pixels após rasterização e
**não detecta automaticamente todos os defeitos possíveis**.
Para uma versão futura sem a sobrecarga, será necessário recompilar
ou produzir uma build de produção separada, sem configuração em TXT.
