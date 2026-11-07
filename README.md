<div align="center">

# 🐒 SaguiOS

**Um sistema operacional escrito do zero por IA**
para PCs x86 de 32 bits, rodando de verdade num notebook de 2011.

Feito por Claude (Anthropic) para o projeto do **Eli Rigobeli**.

`562 KB` · `sem GRUB` · `sem libc externa` · `sem código de outros sistemas` · `48 testes automáticos`

<img src="docs/capturas/B4-3-fase1-corredor.png" width="49%" alt="Sagui 3D, o jogo em primeira pessoa do SaguiOS">
<img src="docs/capturas/H28-tres-programas.png" width="49%" alt="Área de trabalho do SaguiOS com três programas abertos">

</div>

---

## O que é

O SaguiOS é um sistema operacional completo, do carregador de boot ao jogo em 3D, escrito
inteiro neste projeto, seguindo a especificação em [`prd.md`](prd.md). Ele dá boot por um
pendrive num **Lenovo G470** (2011), no VirtualBox e no QEMU, e cabe em **575.488 bytes**
úteis.

Tudo o que vai dentro da imagem foi escrito aqui: carregador de boot em dois estágios, kernel,
drivers (teclado ABNT2, touchpad, vídeo VESA, USB 2.0, pendrive), sistema de arquivos FAT32,
gerenciador de janelas, a biblioteca dos programas (`libsagui`) e uma shim C/POSIX própria
(`userland/mlibc`, não o mlibc upstream), o shell, os programas, a fonte de letras e todos os desenhos. Cada decisão de projeto está explicada em
[`docs/decisoes.md`](docs/decisoes.md) e cada erro encontrado, com causa e correção, em
[`docs/erros.md`](docs/erros.md).

## Rodando no hardware real

Fotos do Eli a cada fase, no Lenovo G470:

| Fase 1: boot | Fase 2: kernel e terminal | Fase 3: processos e shell |
| :---: | :---: | :---: |
| <img src="testes_eli/fase_1_no_notebook.jpg" width="260"> | <img src="testes_eli/fase_2_no_notebook.jpg" width="260"> | <img src="testes_eli/fase_3_no_notebook.jpg" width="260"> |
| **Fase 4: gráfico e mouse** | **Fase 5: janelas e programas** | **Bônus: Sagui 3D (VM)** |
| <img src="testes_eli/fase_4_no_notebook.jpg" width="260"> | <img src="testes_eli/fase_5_no_notebook.jpg" width="260"> | <img src="testes_eli/fase_bonus_vm.png" width="260"> |

## As fases

| Fase | O que foi construído | Na tela |
| --- | --- | :---: |
| **1. Boot** | Carregador próprio em dois estágios (MBR + estágio 2), linha A20, mapa de memória E820, cópia para acima de 1 MB em *unreal mode*, soma de verificação, modo protegido, kernel em C | <img src="docs/capturas/H04-boot-vdi.png" width="200"> |
| **2. Kernel e terminal** | GDT, IDT, TSS, tela de erro, PIC, relógio a 100 Hz, memória física, `kmalloc`, teclado ABNT2 com acentos, sistema de arquivos SGFS | <img src="docs/capturas/H09-ls-cd.png" width="200"> |
| **3. Processos e programas** | Paginação com um espaço por processo, escalonador preemptivo, anel 3, chamadas de sistema, carregador ELF, pipes, shell `sagsh` | <img src="docs/capturas/H18-edicao-e-segundo-plano.png" width="200"> |
| **4. Gráfico e mouse** | VESA 1024x768x32 com buffer duplo, fonte 8x16 própria com acentos, terminal gráfico, touchpad PS/2 | <img src="docs/capturas/H18-fonte-acentos.png" width="200"> |
| **5. Janelas e programas** | Área de trabalho, gerenciador de janelas, Arquivos, Terminal, Editor, Calculadora, Sagui Pega Banana | <img src="docs/capturas/H28-programas-abertos.png" width="200"> |
| **6. Pendrive de dados** | PCI, driver USB 2.0 (EHCI) com hubs e conexão a quente, armazenamento USB, FAT32 com leitura e gravação | <img src="docs/capturas/H34-2-arquivos-no-pendrive.png" width="200"> |
| **Bônus** | B3: bip pelo alto-falante do PC. B4: **Sagui 3D**. (B1 e B2 cancelados pelo Eli.) | <img src="docs/capturas/B4-6-macaco-tonto.png" width="200"> |

## Sagui 3D

O sagui foi preso num labirinto de pedra no meio da Mata Atlântica. Ele precisa juntar bananas
para abrir a porteira de cada fase, enquanto macacos-prego guardas tentam pegar as bananas do
bolso dele. O estilingue atira sementes de jabuticaba (três deixam o macaco tonto, e ele foge),
cada banana comida deixa uma casca que faz o macaco escorregar, e cada fase esconde um cacho
dourado atrás de uma parede de cipó.

<div align="center">
<img src="docs/capturas/B4-1-tela-de-inicio.png" width="32%">
<img src="docs/capturas/B4-7-macaco-escorregou.png" width="32%">
<img src="docs/capturas/B4-5-vitoria.png" width="32%">
</div>

O motor 2,5D foi pensado a partir da geometria, sem consultar nenhum código, tutorial ou
repositório de jogo: salas convexas com altura própria de chão e de teto, desenho coluna por
coluna com uma "fresta" que estreita a cada passagem, perspectiva sem olho de peixe, ponto fixo
(os programas não usam a FPU), chão e teto texturizados e luz por sala. São três fases em
arquivos de texto, com texturas e personagens desenhados no próprio código. Explicação completa
na seção "Sagui 3D" de [`docs/decisoes.md`](docs/decisoes.md).

## Números

| | |
| --- | --- |
| Imagem de boot | **702.464 bytes** úteis |
| Linhas de código | boot 903, kernel 11.204, programas 9.849 (mais 4.004 de ferramentas e testes) |
| Programas | 40 em `/bin`: shell, comandos, compiladores nativos, `lua` e 8 programas gráficos |
| Testes automáticos | 48 scripts (46 H + 2 bônus), incluindo o teste dos compiladores nativos e o da biblioteca gráfica do tinycc |
| Decisões registradas | 33, com o raciocínio de cada uma |
| Erros registrados | 24, com sintoma, causa e correção |
| Commits | ~55, um por história |

## Experimentar

As imagens prontas estão em [`entrega/final/`](entrega/final/):

- **VirtualBox:** `saguios.vdi` (máquina "Other/Unknown", 32 bits, EFI desligado).
- **PC de verdade:** grave `saguios.img` num pendrive em modo DD (Rufus, balenaEtcher ou `dd`)
  e dê boot pela BIOS tradicional.

O passo a passo está em [`docs/como-gravar.md`](docs/como-gravar.md) e, para a rede e a
máquina virtual, em [`docs/virtualbox.md`](docs/virtualbox.md). No Linux, depois de `make vdi`,
`tools/rodar-virtualbox.sh --rodar` cria a VM já com a placa Intel e liga o sistema.

### Rede (NAT do VirtualBox e do QEMU, Bridge e Host-only)

O SaguiOS fala com as duas placas que o VirtualBox oferece num sistema caseiro:
**Intel PRO/1000 MT Desktop** (82540EM; também 82545EM e 82543GC) e **AMD
PCnet-PCI/FAST III**, que é a placa que o VirtualBox escolhe sozinho quando a
máquina é criada como "Outro/Desconhecido". No QEMU são as `-device e1000` e
`-device pcnet`.

Depois de achar a placa (pelo PCI, com MAC e link), o kernel pede endereço por
**DHCP** e resolve os MACs por **ARP** — não há mais endereço nem MAC de gateway
fixos no código. Numa NAT (VirtualBox ou QEMU) o DHCP entrega `10.0.2.15/24`,
gateway `10.0.2.2` e DNS `10.0.2.3`; em "Bridge" e "Host-only" vem o endereço da
rede de verdade. Se não houver servidor DHCP, o sistema assume o endereço padrão
da NAT e continua funcionando.

Por cima disso há uma pilha mínima de IPv4/UDP/TCP com HTTP/1.0 sem TLS: o
comando `rede` mostra placa, MAC, IP, máscara, gateway, DNS e os contadores, e
`wget http://IP/caminho [arquivo]` baixa uma página ou arquivo (nomes também
resolvem, pelo DNS do DHCP). Não há HTTPS/TLS, fragmentação de IPv4,
redirecionamentos nem `chunked`. Para conferir no VirtualBox, veja
[`docs/virtualbox.md`](docs/virtualbox.md); `make rodar` e `make rodar-pcnet`
abrem o sistema no QEMU com cada uma das duas placas.

## Compilar e testar

Requisitos (Ubuntu): `nasm gcc gcc-multilib binutils make python3 qemu-system-x86 qemu-utils imagemagick mtools dosfstools fdisk`.

```bash
make            # build/saguios.img
make vdi        # build/saguios.vdi (VirtualBox)
make test       # todos os testes no QEMU (sem janela)
make rodar      # abre no QEMU com janela
```

### Compilacao nativa de um subconjunto Go (experimental)

A imagem inclui `gosagui`, que compila dentro do SaguiOS um subconjunto minimo
(`package main`, `func init`/`func main`, bootstrap proprio e importacao de
`fmt`, `sagui` e `sagui/log`) para ELF32:

```text
gosagui build -o /go/biblioteca /go/biblioteca.go
/go/biblioteca
```

**Nao e a toolchain oficial do Go nem suporta programas Go gerais.** Veja
[`docs/go.md`](docs/go.md) para limites, bootstrap, bibliotecas, teste nativo e
o trabalho necessario para portar o Go completo.

### Assembler e compilador C nativos (experimental)

`asagui` monta um subconjunto de x86 i386 com sintaxe inspirada em NASM;
`csagui` compila um subconjunto pequeno de C e usa `asagui` para gerar ELF32,
tudo dentro do SaguiOS:

```text
asagui build -o /tmp/ola-asm /asm/ola.asm
csagui build -o /tmp/ola-c /c/ola.c
```

**Nao sao NASM/GCC completos.** Consulte [`docs/asm-c.md`](docs/asm-c.md) para
a sintaxe aceita, limites, exemplos e o teste nativo.

### tinycc (compilador C completo)

Alem deles, `/bin/tinycc` e um compilador de C de verdade (TinyCC 0.9.28rc) rodando
dentro do SaguiOS: compila um fonte `.c` para ELF32 i386 e o executa ali mesmo:

```text
tinycc -o /tmp/ola /tcc/hello-tcc.c
/tmp/ola
```

Funciona com C inteiro (sem FPU); veja [`docs/tcc.md`](docs/tcc.md) para o sysroot
nativo (`/usr`), os limites e o teste `/testes/tinycc`.

Com `libsaguigui.a` (no sysroot, `/usr/lib`), o tinycc tambem compila programas
graficos para a area de trabalho do SaguiOS — janela, rotulo, caixa de texto e
botao — usando o mesmo kit grafico `gui.h` dos programas nativos:

```text
tinycc -o /tmp/ex /tcc/exemplo-gui.c -lsaguigui
/tmp/ex
```

Veja o teste `/testes/gui_tinycc` e [`docs/tcc.md`](docs/tcc.md#biblioteca-gráfica-libsaguigui).

### IDE de código

No Terminal, execute `ide` para abrir o editor de código. Os botões carregam
exemplos Go e Lua; salve com extensão `.go` ou `.lua` e clique em **Executar**.
Go é compilado no próprio SaguiOS; Lua é interpretado, sem gerar executável.
Consulte [o guia da IDE](docs/ide.md) para instruções e limitações.

### Lua nativo (experimental)

A imagem tambem inclui `/bin/lua`, um interpretador nativo de um subconjunto de
Lua escrito em C freestanding, no anel 3. Ele roda scripts direto, sem o Lua
oficial, sem libc e sem gerar ELF:

```text
lua /lua/ola.lua
lua /lua/recursos.lua
lua -e "print(2 + 3)"
lua -v
```

O subconjunto tem valores, variaveis, funcoes com fechamentos e varargs,
tabelas, os lacos `for`/`while`/`repeat`, e as bibliotecas `string`, `table`,
`math`, `os` e `io`, alem de `sagui.registrar`, `sagui.executar`,
`sagui.dormir` e outras ligacoes com o sistema. Numeros sao inteiros de 32 bits
(o PRD proibe a FPU) e nao ha metatabelas, corrotinas nem coleta de lixo.

**Nao e o Lua oficial.** Veja [`docs/lua.md`](docs/lua.md) para a lista do que
funciona, os limites, os exemplos em `/lua` e o teste nativo `h38`.

### Gerar a imagem facilmente no Linux

No Ubuntu/Debian, clone o repositório e execute:

```bash
./tools/gerar-linux.sh
```

Para instalar as dependências automaticamente (usa `sudo`):

```bash
./tools/gerar-linux.sh --instalar-dependencias
```

Para compilar, gerar o VDI e rodar os testes automáticos:

```bash
./tools/gerar-linux.sh --testar
```

Os arquivos ficam em `build/saguios.img` (disco raw), `build/saguios.vdi` (VirtualBox) e `build/saguios.vdi.sha256`.

### Gerar a imagem e rodar no Windows

No Windows não é preciso WSL, QEMU nem instalar o compilador cruzado à mão. Com
**Git para Windows** e **VirtualBox** instalados, clone o repositório e rode:

```powershell
powershell -ExecutionPolicy Bypass -File tools\windows\build-e-rodar.ps1
```

Na primeira execução o script baixa GNU make, NASM e o i686-elf GCC para
`.local\windows-tools\` (ignorado pelo Git; cerca de 750 MB), compila, gera
`build\saguios.img` e `build\saguios.vdi` e cria/atualiza a VM `saguiOS` com a
placa Intel em NAT. Opções: `-SemRodar` (não liga), `-SoRodar` (só liga),
`-Limpar` (apaga `build\`), `-Apagar` (remove a VM) e `-ForcarDownload`.

A toolchain pré-compilada para Windows não tem libiconv, então o script usa
`tools\windows\cc-latin1.py` como `CC`: ele transforma os acentos dos literais
em escapes Latin-1 antes de chamar o GCC, gerando os mesmos bytes do build no
Linux. O restante do shell (`bash`, `find`, `cp`, `sha256sum`) vem do Git para
Windows.

## Estrutura

```
boot/        estágio 1 (MBR) e estágio 2 do carregador, em NASM
kernel/      kernel em C freestanding + NASM: núcleo, memória, drivers, rede, USB, arquivos, janelas
userland/    libsagui e os programas; userland/sagui3d/ é o jogo do bônus B4
rootfs/      arquivos que vão para o disco na RAM (textos, fases do Sagui 3D)
tools/       build (mksgfs.py, mkimagem.py), painel (status.py), verificador de fases
tests/       testes automáticos no QEMU, um por história
docs/        histórias, decisões, diário, erros, roteiros de teste e capturas de tela
dashboard/   painel do projeto (abra dashboard/index.html no navegador)
entrega/     imagens .img e .vdi de cada fase, com SHA-256
testes_eli/  fotos dos testes do Eli no G470
```

## Documentação

| Documento | O que tem |
| --- | --- |
| [`docs/resumo-final.md`](docs/resumo-final.md) | Resumo do projeto, tempo de cada fase e a tabela de conceitos do livro do Tanenbaum preenchida |
| [`docs/decisoes.md`](docs/decisoes.md) | As 31 decisões de projeto e a seção do Sagui 3D |
| [`docs/erros.md`](docs/erros.md) | Os 21 erros encontrados, inclusive os achados no G470 |
| [`docs/virtualbox.md`](docs/virtualbox.md) | Como ligar a rede no VirtualBox (placas, NAT, Bridge, Host-only) |
| [`docs/go.md`](docs/go.md) | Compilador nativo do subconjunto Go (`gosagui`) |
| [`docs/asm-c.md`](docs/asm-c.md) | Assembler x86 reduzido (`asagui`) e compilador do subconjunto C (`csagui`) |
| [`docs/jogos.md`](docs/jogos.md) | C compilado dentro do sistema (`csagui` 0.3): Sudoku, Pong e Tetris em `/jogos` |
| [`docs/tcc.md`](docs/tcc.md) | TinyCC como compilador C completo do SaguiOS: sysroot nativo em `/usr`, uso, limites |
| [`docs/lua.md`](docs/lua.md) | Interpretador nativo do subconjunto de Lua (`lua`) |
| [`docs/gui-lua.md`](docs/gui-lua.md) | Biblioteca gráfica `gui` no Lua nativo: janelas, botões, caixas, listas e diálogos |
| [`docs/mlibc.md`](docs/mlibc.md) | Shim C/POSIX do SaguiOS, APIs e limites (não é o upstream) |
| [`docs/diario.md`](docs/diario.md) | O diário do projeto, fase a fase |
| [`docs/historias/`](docs/historias/) | Uma página por história (H01 a H34, B1 a B4) |
| [`status.md`](status.md) e [`dashboard/`](dashboard/index.html) | Situação de cada fase e história |

## As regras do projeto

O [`prd.md`](prd.md) proíbe código de outros sistemas operacionais, GRUB e bibliotecas prontas.
A shim `userland/mlibc` é uma exceção posterior e deliberada: implementação própria do projeto,
sem importar o mlibc upstream ou outra libc. Fontes e imagens continuam criadas no projeto, com
nomes/comentários em português e testes automáticos; veja a decisão D037 e os limites em
[`docs/mlibc.md`](docs/mlibc.md).
