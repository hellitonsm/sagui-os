# Roteiro de teste: fase 5 (Janelas e programas)

**Arquivos:** `entrega/fase5/saguios.vdi` (VirtualBox) e `entrega/fase5/saguios.img` (pendrive).
**Como gravar e configurar:** `docs/como-gravar.md` (para trocar o `.vdi` da VM, veja "Atualizar a VM numa fase nova").

Teste primeiro na máquina virtual. Se der erro, mande o print antes de ir para o G470.

---

## O que mudou desde a fase 4

O SaguiOS agora liga direto numa **área de trabalho**:

- fundo em degradê verde, amarelo e azul, com o **sagui** em pixel art no canto;
- **barra superior** com o menu SaguiOS (nome do sistema) e o relógio;
- **ícones** de Arquivos, Terminal, Editor, Calculadora, Banana e Sobre (**duplo clique** abre);
- **janelas** com barra de título: arraste pela barra, clique para trazer para a frente, **X** para fechar;
- a área de trabalho **nasce limpa**: o **Terminal** (com o `sagsh` dentro, todos os comandos da fase 3) abre pelo duplo clique no ícone.

Todos os programas gráficos são programas comuns no anel 3: o kernel só desenha as molduras e entrega os cliques e as teclas.

---

## Etapa 1: VirtualBox (`saguios.vdi`)

1. Troque o disco da VM pelo `saguios.vdi` novo e ligue. Deixe a janela da VM em **1024x768 ou maior** (ou em tela cheia).
2. **Tela esperada** (veja `docs/capturas/H20-area-de-trabalho.png`): a área de trabalho com os ícones à esquerda, o sagui no canto e a barra superior — sem janela alguma. Duplo clique no ícone **Terminal** abre o prompt `sagui:/$`.
3. Teste:

| O que fazer | O que deve acontecer |
| --- | --- |
| Arrastar o Terminal pela barra de título | A janela acompanha o mouse, sem deixar rastro. |
| Clicar no nome **SaguiOS** na barra | Abre o menu: **Desligar** corta a energia (a VM/máquina desliga), **Reiniciar** reinicia; Esc ou clique fora fecha. |
| Clicar numa **faixa do alternador** na barra (à direita do nome) | A janela correspondente vem para a frente; a que está em foco fica verde. |
| No Terminal: `ps` e `ls bin \| conta` | Os processos (inclusive `janelas (kernel)` e `terminal`) e a contagem dos programas. |
| Duplo clique no ícone **Arquivos** | Abre o gerenciador de arquivos com as pastas e os arquivos com ícones. |
| No Arquivos: duplo clique em `historia`, depois em `linux.txt` | Entra na pasta e abre o texto no **Editor**. |
| No Editor: digitar algo | O título ganha um `*`. |
| No Editor: **Ctrl+S** | Pede o caminho (sugere `/tmp/`): digite `nota.txt` e Enter. O `*` some. |
| Fechar o Editor no **X** depois de mudar o texto | Pergunta "Salvar antes?". |
| No Arquivos: **Voltar**, entrar em `tmp`, **Nova pasta**, selecionar e **Apagar** | Cria e apaga a pasta, com "Tem certeza?". |
| Duplo clique em **Calculadora** | `12,5 × 4 =` dá `50`; `7 ÷ 2 =` dá `3,5`; `1 ÷ 0 =` mostra o aviso. Funciona com clique e com o teclado. |
| Duplo clique em **Banana** | Tela de início do **Sagui Pega Banana**. Enter começa; **setas** movem o sagui; pegue as bananas; 3 vidas; recorde da sessão. |
| Duplo clique em **Sobre** | Versão, tamanho da imagem, processos ativos e créditos. |
| No Terminal: `editor`, `calculadora`, `banana`, `arquivos` | Os mesmos programas abrem a partir do shell. |
| No Terminal: `sair` | O Terminal fecha. Para abrir de novo, duplo clique no ícone **Terminal**. |

---

## Etapa 2: Lenovo G470 (`saguios.img` no pendrive)

1. Grave o `saguios.img` no pendrive e dê boot (F12, **USB HDD**).
2. **O que você deve ver no G470:**
   1. A **área de trabalho** com o degradê, o sagui, os ícones, o relógio na barra e o menu SaguiOS (desligar/reiniciar). O Terminal abre pelo ícone.
   2. O **touchpad** arrastando janelas e o **duplo clique** abrindo os programas pelos ícones.
   3. O **Sagui Pega Banana** jogável com as setas.
3. Atenção especial no G470:
   - **Duplo clique no touchpad:** se for difícil acertar (dois toques em menos de meio segundo), me diga: eu ajusto o tempo.
   - **Velocidade:** arrastar janelas grandes redesenha bastante tela. Me diga se ficar lento ou "travando" ao arrastar.
   - **Botões do touchpad:** clique esquerdo para tudo; o direito ainda não tem função (fica para o pendrive, na fase 6).
   - **Relógio na barra:** deve bater com a hora da BIOS.
4. **Fotografe** a área de trabalho com três programas abertos (por exemplo Arquivos, Calculadora e Banana) e uma tela do jogo.

**Se der errado**, fotografe a tela e me diga:

- **Tela vermelha** "O SaguiOS encontrou um erro": a foto mostra o erro e os registradores.
- **A área de trabalho aparece, mas o mouse ou o teclado não respondem:** me diga se a seta se move e se a luz do Caps Lock acende.
- **Uma janela não abre** pelo ícone: tente o mesmo programa pelo Terminal (`calculadora`, por exemplo) e me diga o que aparece.

Quando testar, me diga **"fase 5 ok"** ou descreva o que aconteceu.
