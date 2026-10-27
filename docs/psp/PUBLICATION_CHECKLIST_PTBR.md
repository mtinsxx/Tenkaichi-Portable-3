# BT3-PSP — checklist antes de tornar o repositório público

> Documento de preparação. **Não muda a visibilidade do repositório, não impede forks nem limita a execução do código por terceiros.**
>
> Estado verificado em 26/10/2026, tendo como base o commit `d236e2f` da branch `main`. Esta é uma **auditoria preliminar**, e não uma certificação de segurança ou de direitos autorais.

## O que foi verificado

- O repositório estava **privado** no momento da análise.
- A árvore da `main` continha 1.024 arquivos regulares. Não apareceram arquivos com nomes típicos de credenciais, ISO, arquivos de disco do jogo ou `EBOOT.PBP` **dentro da árvore atual**. Não foi realizada leitura automatizada integral dos 1.024 conteúdos.
- O `.gitignore` contém exclusões para `*.iso`, `/disc/`, `/assets/` e diretórios de compilação. **Gitignore não remove arquivos já commitados nem limpa o histórico.**
- Existem `LICENSE` (AGPLv3), `LICENSE-MIT` e `THIRD_PARTY.md`. O README informa que o código decompilado em `src/` e `include/` pertence aos titulares originais, exigindo avaliação própria de direitos antes de publicar.
- Existem imagens no projeto (por exemplo, capturas em `docs/screenshots/` e imagens do port Vita). Não foi confirmada a origem/licença de cada imagem.
- Foram identificados três workflows (`psp.yml`, `psp-build.yml`, `release.yml`), incluindo compilação de PSP, publicações por tags e uploads de artefatos. `psp.yml` tem permissão `contents: write` no escopo de todo o workflow; convém restringi-la ao mínimo necessário antes de aceitar PRs públicos.
- Existiam releases `psp-v0.1.18` e `psp-v0.1.19` com `EBOOT.PBP` anexado. **Tornar público pode expor essas versões e o histórico/logs do Actions**.
- Em inspeção adicional foram lidos **62 arquivos de scripts/configurações** (Python, shell, YAML e equivalentes) da `main` para um conjunto limitado de padrões de credenciais; **não houve correspondências** nesse grupo. Isso não cobre todas as fontes, blobs históricos, imagens, logs ou segredos de configuração do GitHub.
- Em uma amostra de **228 commits** acessíveis na `main`, **214 commits** tinham um endereço de e-mail com domínio de caixa postal pessoal no campo de autoria. O endereço não é reproduzido neste relatório. **Bloqueador de privacidade antes de publicar:** rever o histórico e decidir como lidar com os metadados antigos. Alterar privacidade do perfil do GitHub não modifica os commits já criados.
- A auditoria local de histórico foi preparada em `scripts/pre_publication_audit.py`, com workflow manual `.github/workflows/pre-publication-audit.yml`; a execução pelo GitHub Actions ainda depende de resolver o bloqueio de cobrança. Ela não valida automaticamente direitos autorais nem todos os logs/referências remotas.
- **Não foi concluída uma auditoria de segredos em todo o conteúdo e em todos os commits antigos.** A busca de código do GitHub não retornou nem termos sabidamente presentes nos arquivos; por isso resultados vazios dessa busca não são prova de ausência de segredos.

## Antes de publicar: itens obrigatórios de revisão manual

- [ ] Revisar o histórico completo de commits, branches, tags, releases e logs do Actions para tokens, senhas, URLs privadas, e-mails pessoais, caminhos locais e dados confidenciais. **Há um endereço pessoal presente no histórico; tratar antes de publicar.**
- [ ] Inspecionar o conteúdo de arquivos e mídias, não só seus nomes. Conferir especialmente `port/vita/sce_sys/`, `docs/screenshots/`, `port/data/`, fontes e outros materiais possivelmente derivados do jogo.
- [ ] Confirmar que não há ISO, executáveis originais do jogo, assets protegidos nem segredos em **qualquer commit**. Se houver segredo real, revogá-lo/rotacioná-lo; apagar apenas o arquivo atual não o invalida.
- [ ] Revisar cuidadosamente direitos de redistribuição do código decompilado, imagens e binários. As licenças dos componentes de port não necessariamente autorizam redistribuir conteúdo do jogo.
- [ ] Conferir os autores e e-mails de commit, preferindo o endereço `users.noreply.github.com` para commits futuros. O histórico anterior pode expor endereços já registrados.
- [ ] Avaliar as releases antigas, os artefatos e os logs das execuções anteriores. **Não excluir releases ou histórico sem fazer backup e decidir conscientemente.**
- [ ] Conferir `Settings → Security / Advanced Security` e ativar proteção contra exposição de segredos, conforme disponível para a conta.
- [x] Preparada a separação de permissões nos YAMLs: builds PSP e desktop apenas com `contents: read` e jobs de publicação `contents: write` somente em tags; builder PSP duplicado passa a manual. **Falta conferir as opções de segurança em Settings → Actions → General**, inclusive aprovação de contribuições externas, pois essa conexão não altera configurações administrativas. Os workflows modificados não puderam ser compilados/testados na nuvem por bloqueio do GitHub Actions.
- [ ] Configurar proteção/regras para `main`. A API do GitHub retornou **HTTP 403** para rulesets no plano GitHub Free enquanto o repositório estiver privado (é necessário Pro ou repositório público). Foi adicionado `.github/CODEOWNERS`, porém este arquivo só solicita revisão: **não protege `main` sem regras da plataforma**. Após eventual publicação, ativar branch ruleset e revisões obrigatórias.
- [ ] **Issues ainda estão habilitadas** (`has_issues: true`): desativar manualmente em `Settings → General → Features → Issues` se não desejar críticas/comentários por esse canal. **Discussions já estão desabilitadas** (`has_discussions: false`). O arquivo `.github/ISSUE_TEMPLATE/config.yml` oculta a opção de issue em branco na interface, mas não desliga Issues ou a API.
- [ ] Se necessário, usar `Settings → Moderation options → Interaction limits` para restringir temporariamente comentários e PRs de desconhecidos. Limites de interação não impedem leitura nem fork.
- [ ] Decidir explicitamente se deseja manter as releases e os uploads automáticos de `EBOOT.PBP`. Eles facilitam testes de terceiros.
- [ ] Fazer backup privado de branches, tags e releases relevantes antes da mudança.

## Créditos e condições de uso

O trabalho de adaptação/manutenção PSP é associado ao repositório de [mtinsxx](https://github.com/mtinsxx), com contribuições identificáveis pelo histórico. O port deriva de [Tenkaichi3Decomp-Vita](https://github.com/arielxflores/Tenkaichi3Decomp-Vita), de [Tenkaichi3Decomp](https://github.com/z3xox/Tenkaichi3Decomp) e do projeto original de decompilação. Preservar os créditos e os avisos de licença existentes; **não alegar autoria exclusiva do jogo nem de códigos de terceiros**.

Um projeto público com código distribuído sob termos de software livre permite que outras pessoas obtenham e, conforme as licenças aplicáveis, compilem e redistribuam cópias. Não há configuração do GitHub para permitir leitura pública do código e simultaneamente impedir o clone, a compilação ou testes independentes. Remover releases e desativar a geração de artefatos só reduz a facilidade de acesso a builds prontos — não impede testes com código público.

## Política de testes e interações

Este repositório é um **port não oficial, experimental, em desenvolvimento**. Builds de desenvolvimento, quando fornecidas, não são versões estáveis nem garantia de desempenho. O mantenedor pode optar por não prestar suporte a builds de terceiros, por limitar comentários ou por não aceitar contribuições externas. Essas decisões não revogam as permissões legais de uso concedidas pelas licenças aplicáveis.

**Se o objetivo é impedir que desconhecidos tenham acesso e testem o código, a opção efetiva é mantê-lo privado e conceder acesso somente a colaboradores escolhidos.**


## Implementações de preparação (26/10/2026)

- `.github/CODEOWNERS` solicita revisão do mantenedor; não bloqueia pushes.
- `CONTRIBUTING.md` descreve política de interações respeitosas, ausência de suporte obrigatório e créditos.
- `SECURITY.md` orienta a não divulgar possíveis credenciais em issues.
- `scripts/pre_publication_audit.py` examina commits e blobs de refs obtidas no clone sem imprimir valores secretos; o workflow correspondente **só roda manualmente**, quando o GitHub liberar Actions.
- `.github/workflows/psp.yml`: permissões comuns somente leitura; publicação do EBOOT isolada num job de tag. 
- `.github/workflows/release.yml`: builds desktop/Vita não disparam mais em todo push da `main`, e publicação com escrita separada por tag.
- `.github/workflows/psp-build.yml`: runner secundário PSP fica manual para não duplicar consumo.
- **Não houve alteração em visibilidade, configurações administrativas, histórico anterior, releases, arquivos de jogo, binários nem direitos de terceiros.** O repositório NÃO está liberado para publicação com base apenas nestes controles.

- [ ] Reavaliar forks. No momento `allow_forking` está ativado; forks públicos de um projeto público não podem ser proibidos por controles normais do GitHub. O projeto continua privado até decisão consciente do proprietário.
