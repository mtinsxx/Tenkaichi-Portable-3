# BT3 PSP — plano de estabilização da auditoria de 25/10/2026

**Base protegida:** `stable/psp-fe93677-working-menus` = `fe93677`. Esta branch é apenas de teste; **não fazer merge sem validar no PPSSPP ou PSP**.
**Escopo:** port nativo MIPS Allegrex do BT3 SLUS-21678, com dados do jogo mantidos fora do repositório.
**Interpretação dos resultados:** o CI só comprova compilação e geração do EBOOT. Um EBOOT compilado não comprova boot, menus, salvamento, gráficos, FPS ou conclusão do jogo.

## Inventário P01–P22

| ID | Estado nesta branch | Evidência / critério pendente |
|---|---|---|
| P01 | PARCIAL: 104 símbolos coletados em `expected_stubs.txt`; novas ausências passam a bloquear o link | Inspecionar callers; substituir cada função necessária e retirar o símbolo da lista |
| P02 | PENDENTE: r2l via libclang ainda opcional | Build estrito com ferramenta disponível, teste de expressões com efeitos colaterais e comparação de execução |
| P03 | DIAGNÓSTICO: contadores de ZTEST/ZWRITE; sem alteração do resultado | Implementar profundidade por tipo de passe; comparar 3D, HUD e menus com PS2 |
| P04 | PENDENTE | Mapear FB16, depth-as-texture e outros passes atualmente ignorados |
| P05 | PENDENTE | Gerenciar snapshots de múltiplos alvos sob 2 MiB de VRAM |
| P06 | DIAGNÓSTICO: contagem de ALPHA.C=FIX por faixa | Somente então converter corretamente fator FIX GS→GU e comparar efeitos |
| P07 | DIAGNÓSTICO: contagem de candidatos CLD0/CBP0 | Testar e portar regra condicional de última CLUT (Vita), com chave cache correta |
| P08 | PENDENTE: já há contadores CLAMP e TFX | Traduzir HIGHLIGHT, REGION_CLAMP e REGION_REPEAT por cena |
| P09 | PENDENTE | Medir VU1 e substituir os microprogramas Allegrex um a um com testes equivalentes |
| P10 | PENDENTE | Medir pico de memória, texturas e ADX; desenvolver streaming sem underruns |
| P11 | CÓDIGO ALTERADO | SELECT+Quadrado/Círculo não enviam Quadrado/Círculo junto com L3/R3; teste real de combos pendente |
| P12 | CÓDIGO ALTERADO | `report=` agora controla métricas; `heap=` inválido é ignorado explicitamente (29 MiB fixos) |
| P13 | CÓDIGO ALTERADO | `demochara1/2` lê ID decimal 0..255; validação dos personagens reais pendente |
| P14 | CÓDIGO ALTERADO | `sceIoPread` lê exatamente quando disponível; erros reais retornam falha; tabela AFS é limitada pelo arquivo |
| P15 | CÓDIGO ALTERADO | Caminhos inválidos/truncados são rejeitados, sem indexar fora do buffer |
| P16 | PARCIAL: mkdir, diretórios e retorno de read/write/flush validados | Testar carga, gravação, reinício, Memory Stick desativado, cartão cheio; format ainda virtual |
| P17 | PARCIAL: mudanças de gain, bound e FIFO protegidas pelo mutex existente | Testar latência, threads, chamadas reentrantes e esgotamento de FIFO |
| P18 | PENDENTE | Testar opening/ending e sincronismo áudio-vídeo em hardware |
| P19 | PENDENTE | Validar `wide=0` e eventual letterboxing sem regressões |
| P20 | PARCIAL: builds PSP existentes e inventário de símbolos | CI também precisa de testes host, PPSSPP, ASAN onde aplicável e matriz comparativa |
| P21 | PENDENTE | Cobrir personagens, transformações, colisão, IA, saves e campanhas com jogo real |
| P22 | RESTRIÇÃO: PSP 64 MiB | Não declarar PSP-1000, dois controles, fidelidade GS total ou 30 FPS sem evidência |

## Antes de qualquer merge

1. Compilar EBOOT com os workflows PSP e ler o log do link (não confundir o workflow geral de PC/Vita).
2. Executar **a mesma ISO do usuário** em PPSSPP com a opção Memory Stick **ativa**, comparando esta branch ao commit `fe93677`; nunca enviar ISO, ELFs ou assets do jogo ao GitHub.
3. Coletar `log.txt`, imagem do menu e uma batalha (incluindo SELECT/L3/R3, ataques e pausas), áudio e operações salvar/carregar.
4. Testar arquivos AFS danificados/encurtados numa cópia de teste, verificando erro explícito e ausência de corrupção silenciosa.
5. Manter mudanças gráficas de resultado e VU1 em branches separadas com benchmarks antes/depois, com controle de regressões no menu.
6. Rodar no PSP real de 64 MiB antes de chamar versão estável.

O sistema ainda **não está totalmente convertido/estável**. O resultado desta branch é uma primeira etapa segura de infraestrutura e instrumentação, não a conclusão P01–P22.
