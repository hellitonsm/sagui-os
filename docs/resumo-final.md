# SaguiOS: resumo final

Um sistema operacional para PCs x86 de 32 bits, escrito do zero por uma IA (Claude, da Anthropic) para o projeto do Eli Rigobeli, seguindo o `prd.md`. Tudo o que vai dentro da imagem foi escrito neste projeto: carregador de boot, kernel, drivers, biblioteca dos programas (`libsagui`), shell, programas gráficos, fonte e desenhos.

## O que foi feito

| Fase | Entrega | Histórias |
| --- | --- | --- |
| 1. Boot | Carregador próprio em dois estágios (MBR + estágio 2), A20, mapa E820, cópia para acima de 1 MB em *unreal mode*, soma de verificação, modo protegido, kernel em C | H01 a H04 |
| 2. Kernel e terminal | GDT/IDT/TSS, tela de erro, PIC, PIT a 100 Hz, CMOS, bitmap de páginas, `kmalloc`, teclado ABNT2 com teclas mortas, SGFS, terminal provisório | H05 a H09 |
| 3. Processos e programas | Paginação (um espaço por processo), escalonador round-robin preemptivo, anel 3, `int 0x80` com validação de ponteiros, ELF, `libsagui`, arquivos graváveis na RAM, pipes, shell `sagsh` com 18 comandos | H10 a H16 |
| 4. Gráfico e mouse | VESA 1024x768x32 com buffer duplo, fonte 8x16 própria com acentos, terminal gráfico, touchpad PS/2 | H17 a H19 |
| 5. Janelas e programas | Área de trabalho, gerenciador de janelas no kernel, kit gráfico, Arquivos, Terminal, Editor, Calculadora, Sagui Pega Banana, Sobre | H20 a H28 |
| 6. Pendrive de dados | PCI, driver EHCI, enumeração USB com hubs e conexão a quente, Bulk-Only/SCSI, FAT32 com leitura e gravação, notificação e ícone do pendrive | H29 a H34 |
| Bônus | Bip pelo alto-falante do PC (fila de notas tocada pelo relógio); **Sagui 3D**, jogo em primeira pessoa 2,5D com três fases (B1 e B2 cancelados pelo Eli) | B3, B4 |

## Números

| Item | Valor |
| --- | --- |
| Imagem (bytes úteis) | **613.888 bytes = 600 KB (41,6% de 1,44 MB)**, com o Sagui 3D e as três fases incluídos. O arquivo `.img` tem 1 MB porque é completado com zeros (D004). |
| Linhas de código | boot 903, kernel 9.614, userland 6.317 (total 16.834), mais 3.591 de scripts de build e testes |
| Programas | 31 em `/bin` (shell, 22 comandos e 8 programas gráficos) e 6 de teste em `/testes` |
| Testes automáticos | 37, um por história (mais um de dois controladores USB), todos no QEMU (IDE e pendrive USB quando a história mexe no boot) |
| Commits | ~55, um por história, mais as entregas e confirmações de cada fase |
| Decisões registradas | 30, em `docs/decisoes.md` (o raciocínio de cada parte central) |
| Erros registrados | 19, em `docs/erros.md` (sintoma, causa, correção) |
| Capturas de tela | mais de 70, em `docs/capturas/` |

## Tempo de cada fase (trabalho do agente)

Do início da fase até a entrega para teste, pelos horários dos commits em 03/11/2026:

| Fase | Tempo | Ciclos de correção depois do teste no G470 |
| --- | --- | --- |
| 1. Boot | ~15 min | 1: o mapa de memória do G470 (8 GB, 21 regiões) empurrava a saudação para fora da tela (H04-fix) |
| 2. Kernel e terminal | ~30 min | 0 |
| 3. Processos e programas | ~40 min | 0 (dois pedidos do Eli, as setas para os lados e o texto em segundo plano, entraram na fase 4) |
| 4. Gráfico e mouse | ~20 min + ~45 min da correção | 1: a inicialização do touchpad travava o teclado (H19-fix) |
| 5. Janelas e programas | ~47 min | 0 (um detalhe visual no jogo, corrigido no começo da fase 6) |
| 6. Pendrive de dados | ~40 min | 2: o EHCI do G470 é de 64 bits e pede estruturas maiores; a FAT32 depois da partição do SaguiOS não era vista; os dois controladores do G470 compartilhavam o mesmo anel de transferências (erros 17 a 19) |

## O que funcionou no G470

Confirmado pelo Eli, com fotos em `testes_eli/`:

- **Fase 1:** boot pelo pendrive com o carregador próprio; A20 já ligada pela BIOS; VESA 1024x768x32 (modo 0x118) disponível.
- **Fase 2:** saudação, interrupções, memória (8135 MB no total, 3017 MB abaixo de 4 GB, 892 MB gerenciados), teclado ABNT2 (`/`, `?`, `ç`), terminal.
- **Fase 3:** shell no anel 3, multitarefa (`contador &` com `ps`), histórico, acentos com teclas mortas.
- **Fase 4:** modo gráfico, fonte própria com acentos, touchpad, depois da correção H19-fix.
- **Fase 5:** área de trabalho com cinco programas abertos ao mesmo tempo.
- **Fase 6:** os dois primeiros testes: o pendrive não aparecia (erros 17 a 19, corrigidos). Aprovada no terceiro teste. Pontos que só o hardware real exercita: o EHCI de 64 bits, o hub interno do chipset (D024) e a passagem do controlador USB da BIOS para o sistema.

## O que ficou de fora

- **Bônus B1 e B2** (HD interno só leitura, boot UEFI): cancelados pelo Eli. B1: o SaguiOS nunca grava no HD, então listar as partições não acrescenta nada. B2: o objetivo é rodar em computadores antigos, com BIOS tradicional.
- **Hub USB testado no QEMU:** impossível no QEMU 8.2, que não tem hub de alta velocidade e recusa ligar o hub USB 1.1 no EHCI. O código segue a especificação, mas só é validado no G470 (D024).
- **Dispositivos USB de velocidade plena ou baixa** (que precisariam de *split transactions* atrás do hub): não suportados. Pendrives são de alta velocidade.
- **Teclado e mouse USB:** fora do escopo do PRD (o G470 usa PS/2 internamente).
- **Fora do escopo do PRD** (seção 1.1): threads, vários processadores, memória virtual com troca para disco, usuários e permissões.
- **Limitações conhecidas:** o kernel usa só os primeiros 896 MB de RAM (D008); um pendrive montado por vez; nomes de arquivo com mais de 63 letras no pendrive aparecem cortados; textos em UTF-8 vindos de fora são convertidos para Latin-1 ao mostrar (D027).

## A tabela do Tanenbaum (PRD 1.1), preenchida

| Conceito do livro | O que o SaguiOS implementa | Histórias | Estado | Onde ver |
| --- | --- | --- | --- | --- |
| Modo kernel e modo usuário | Kernel no anel 0; todos os programas no anel 3, com IOPL 0 | H03, H12 | Feito | `falha instrucao` derruba só o programa |
| Processos | PID, pai e filho, estados, `executar`, `esperar`, `sair`, `matar`, zumbis recolhidos | H11, H13 | Feito | `ps` |
| Escalonamento | Round-robin preemptivo, fatia de 20 ms no tique do PIT | H11 | Feito | teste A/B alternando; `contador &` |
| Comunicação entre processos | Pipes com bloqueio, fim de arquivo, `duplicar` | H15 | Feito | `ls bin \| conta`; Terminal em janela |
| Espaços de endereço | Paginação de 2 níveis, um diretório por processo, kernel compartilhado | H10 | Feito | teste de dois espaços |
| Gerenciamento de memória | Bitmap de páginas, `kmalloc`, `aumentar_memoria` + `aloca` nos programas | H06, H10, H13 | Feito | `mem` |
| Arquivos e diretórios | Árvore na RAM com cópia na escrita sobre o SGFS; FAT32 no pendrive com leitura e gravação | H08, H14, H32, H33 | Feito | Arquivos; `fsck.vfat` limpo |
| Entrada e saída | Drivers próprios por interrupção e por consulta: teclado, touchpad, vídeo, relógio, PCI, USB EHCI, armazenamento | H05, H07, H17, H19, H29 a H31 | Feito | `usb`, `discos` |
| Proteção | Páginas do kernel sem o bit de usuário; ponteiros validados em toda chamada | H12, H16 | Feito | `falha kernel` |
| Chamadas de sistema | ~45 chamadas: processos, arquivos, pastas, comunicação, diversas, janelas, dispositivos | H12 | Feito | `kernel/inc/abi.h` |
| O shell | `sagsh` no anel 3: redirecionamento, pipes, segundo plano, histórico, Tab, edição com setas | H16 | Feito | Terminal |
| Carregamento de programas | Carregador ELF32 próprio | H13 | Feito | todos os programas em `/bin` |
| Interface gráfica (extra) | Gerenciador de janelas no kernel, programas gráficos no anel 3 com memória compartilhada | H20 a H28 | Feito | a área de trabalho |

## Fase bônus

| Item | O que é | Onde ver |
| --- | --- | --- |
| B3 | Comando `bip`, sons no Sagui Pega Banana e no Sagui 3D, sem travar quem toca | D029, `tests/b3_bip.py` |
| B4 | Sagui 3D: motor 2,5D próprio (salas convexas com alturas, frestas por coluna, ponto fixo), três fases em texto, macacos-prego, estilingue de jabuticaba, bananas, cascas, porteira e cacho dourado | seção "Sagui 3D" em `docs/decisoes.md`, `tests/b4_sagui3d.py` |
