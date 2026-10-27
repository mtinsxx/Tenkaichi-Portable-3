# PSP — investigação das letras cinzas no menu do cartão de memória

Captura e gravação enviadas pelo usuário em 25/10/2026:
- A tela azul de MEMORY CARD slot 1 aparece, com moldura branca e
  caracteres visivelmente cinzas.
- Outras telas mostram texto/UI claro sobre fundo muito claro e
  inconsistências gráficas. Não são apenas falhas VU1 de lutadores.
- A captura não mostra os registradores GS de cada desenho; a causa
  exata desta fonte ainda precisa de validação dinâmica.

## Defeito matemático confirmado no código PSP

O PS2 GS combina fontes/destinos por
`(A - B) * C / 128 + D`; ao usar `ALPHA.C=FIX`,
o registrador `ALPHA.FIX=0x80` significa cobertura integral.

O PSP GU usa `GU_FIX` entre 0 e 255, de forma que
`FIX=0x80` do PS2 **deve ser enviado como 0xFF** no PSP.
O backend tinha `Psp_GsFixToGu()` correto, mas
`gPspGsFixScale=0` por padrão desde a PR #9.
Isso enviava 0x80 ao GU (aproximadamente 50% de cobertura),
podendo converter texto branco em cinza ao combinar com
fundos escuros ou reduzir intensidade de alguns efeitos.

A branch agora ativa **`gPspGsFixScale=1` por padrão**, sem
precisar do arquivo opcional `bt3.txt`. As outras experiências
(`gs_clut`, `gs_depth`, `gs_hi_tex`) permanecem desativadas.

## Testes

`port/psp/test_gs_text_blend.c` compara toda faixa FIX 0..255,
várias combinações de cores GS frente à conversão PSP real,
além de verificar:
- texto branco sobre fundo azul escuro com FIX 0x80 permanece branco;
- FIX=0x40 deve significar aproximadamente 50%, e não 25%;
- FIX=0 significa zero e valores acima de 0x80 saturam.

Os dois workflows PSP executam esse teste antes do EBOOT.

## Como diagnosticar na própria tela

O `log.txt` do port agora tem a linha:

```
psp: GS sprite FIX: draws=... full-coverage=... (FIX>=0x80) converted=1 special-TFX=...
```

Se `draws>0` no menu em questão e as letras ficarem brancas,
a conversão de FIX era parte da falha visual.
Se `draws=0`, **não atribuir as letras cinzas a ALPHA.FIX**:
é preciso investigar outros estados da fonte (TFX/TCC, CLUT,
`PRIM.ABE`, cor RGBAQ, qual destino de framebuffer é usado).
`special-TFX>0` indica que sprites usaram `TFX=2/3`, cuja
tradução atual com `GU_TFX_BLEND` não reproduz completamente a
regra do GS; deve ser tratado em alteração futura separada.

A linha preexistente `psp: GS menu passes` também é útil para
checar descarte de FB16/depth e de texturas especiais.

**A correção de escala é real e testada, mas o desaparecimento
do cinza ou dos glitches da gravação ainda NÃO está comprovado.**
Não ativar CLUT/depth experimentais nem corrigir brilho forçando
todos os glifos a 255: isso quebraria cores legítimas.
O EBOOT da PR #19 permanece disponível para comparação A/B,
e a `main` estável `fe93677` permanece inalterada.

Não precisa criar `bt3.txt` para testar. Se a imagem ficar pior,
volte ao EBOOT anterior; o modo fix pode ser desativado
opcionalmente com `gs_fix=0` no arquivo de configuração,
mas não é obrigatório criar esse arquivo para fazer o teste normal.
