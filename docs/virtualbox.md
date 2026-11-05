# Rede no VirtualBox

O SaguiOS liga a rede sozinho nas duas placas que o VirtualBox oferece para uma
máquina de sistema caseiro. Não é preciso instalar nada no hóspede nem escolher
um driver: o kernel procura as placas pelo PCI, na ordem **Intel 8254x** e
**AMD PCnet**, e usa a primeira que encontrar.

| Placa no VirtualBox | ID PCI | Driver | Estado |
| --- | --- | --- | --- |
| Intel PRO/1000 MT Desktop | `8086:100e` (82540EM) | `e1000` | funciona |
| Intel PRO/1000 MT Server | `8086:100f` (82545EM) | `e1000` | funciona |
| Intel PRO/1000 T Server | `8086:1004` (82543GC) | `e1000` | funciona |
| PCnet-FAST III | `1022:2000` (Am79C973) | `pcnet` | funciona |
| PCnet-PCI II | `1022:2000`/`2001` (Am79C970A) | `pcnet` | funciona |
| Paravirtualized (virtio-net) | `1af4:1000` | — | **não suportado** |

Se a placa estiver em **Paravirtualized Network (virtio-net)**, troque para
"Intel PRO/1000 MT Desktop": é a mesma rede, com um driver que o SaguiOS tem.

## O que a pilha faz

- **DHCP** (descobrir, ofertar, pedir, confirmar): pega IP, máscara, gateway e
  DNS do servidor da rede. Funciona na NAT do VirtualBox, na NAT do QEMU, em
  *Bridge*, em *Host-only* e em *Internal Network* (se houver servidor DHCP).
- **Sem servidor DHCP** (rede isolada), o sistema assume o endereço padrão da
  NAT, `10.0.2.15/24`, gateway `10.0.2.2` e DNS `10.0.2.3`, e continua
  funcionando.
- **ARP** para qualquer destino (o MAC do gateway não é fixo no código).
- **DNS** por UDP, para o `wget` aceitar nomes e não só IPs.
- **TCP** básico e **HTTP/1.0** sem TLS, o suficiente para
  `wget http://host/caminho [arquivo]`.

O que não existe: HTTPS/TLS, IPv6, fragmentação de IPv4, `chunked`, download em
várias conexões, IRQ de rede (os drivers fazem *polling*) e virtio-net.

## Criando a máquina virtual

1. **Novo** → nome `SaguiOS` → tipo **Outro / Outro (32 bits)** →
   memória 512 MB → "Usar um disco rígido virtual existente" e escolha o
   `saguios.vdi` (gerado por `make vdi`, ou o de `entrega/final/`).
2. **Configurações → Sistema → Placa-mãe:** EFI **desligado** (o SaguiOS usa a
   BIOS tradicional) e disco de boot na ordem.
3. **Configurações → Rede → Adaptador 1:**
   - Marque **Habilitar placa de rede**;
   - **Conectado a:** `NAT` (padrão, o mais fácil) — `Placa em modo bridge` e
     `Somente anfitrião` também funcionam;
   - **Avançado → Tipo:** `Intel PRO/1000 MT Desktop` (recomendado) ou
     `PCnet-FAST III`.
4. Ligue a máquina.

No boot, o SaguiOS mostra duas linhas assim:

```
[ ok ] Rede: placa e1000 (8086:100e), MAC 8:0:27:1a:2b:3c, link ligado
[ ok ] Rede: IP 10.0.2.15/255.255.255.0 (DHCP), gateway 10.0.2.2, DNS 10.0.2.3
```

Com a PCnet-FAST III, a primeira linha diz `placa pcnet (1022:2000)`. Se aparecer
`[aviso] Nenhuma placa de rede conhecida`, o tipo de placa está em
"Paravirtualized" ou a placa está desabilitada.

## Conferindo de dentro do sistema

No shell (janela Terminal):

```
rede
```

```
Placa....: e1000 (8086:100e)
MAC......: 8:0:27:1a:2b:3c
Link.....: ligado
Endereco.: 10.0.2.15/24 (DHCP)
Mascara..: 255.255.255.0
Gateway..: 10.0.2.2
DNS......: 10.0.2.3
Servidor.: 10.0.2.2 (DHCP)
Quadros..: 12 enviados, 9 recebidos
```

`(DHCP)` quer dizer que o endereço veio do servidor; `(fixo da NAT)` significa
que não houve resposta e o sistema usou o padrão.

## Testando o `wget` pela NAT

Na NAT do VirtualBox, `10.0.2.2` é **o computador de verdade** (o anfitrião).
Então sirva um arquivo nele e baixe de dentro do SaguiOS:

```bash
# no anfitrião (Linux/macOS)
cd /tmp && echo "ola do anfitriao" > ola.txt && python3 -m http.server 8000
```

No SaguiOS:

```
wget http://10.0.2.2:8000/ola.txt ola.txt
cat ola.txt
```

Nomes também funcionam (o DNS da NAT vai responder pelo anfitrião):

```
wget http://example.com/
```

Para o caminho inverso (alcançar um serviço do hóspede a partir do anfitrião),
use **Avançado → Encaminhamento de portas**: por exemplo, anfitrião `8080` →
hóspede `80`. O SaguiOS não tem servidor HTTP; o encaminhamento serve para os
testes futuros.

## Rede bridge e somente anfitrião

Com **Placa em modo bridge**, o SaguiOS pega um IP da rede real por DHCP e
alcança qualquer máquina dela (`rede` mostra o endereço da sua rede, não
`10.0.2.x`). Com **Somente anfitrião** (`Host-only`), o VirtualBox entrega um
endereço `192.168.56.x` pelo próprio servidor DHCP dele — também automático.

## Pelo script (mais rápido)

O repositório tem `tools/rodar-virtualbox.sh`, que cria a VM já do jeito certo
e liga o `.vdi` do `build/`:

```bash
make vdi                              # gera build/saguios.vdi
tools/rodar-virtualbox.sh --rodar     # cria/atualiza a VM "saguiOS" e liga
```

O script configura 512 MB, BIOS tradicional (sem EFI), mouse e teclado PS/2,
16 MB de vídeo e **Adaptador 1 = Intel PRO/1000 MT Desktop em NAT** — e
reconecta a placa e o disco toda vez que roda, então uma VM criada antes
também passa a usar a placa certa. Outras opções:

| Comando | O que faz |
| --- | --- |
| `--criar` (padrão) | cria a VM se não existir, ajusta a rede e anexa o disco |
| `--atualizar` | só reconecta a placa de rede e o disco (VM já criada) |
| `--rodar` | faz o de cima e liga a máquina |
| `--apagar` | remove a VM `saguiOS` (o `.vdi` do `build/` não é apagado) |

Ele tenta `--nic-type1`, `--nictype1` e `--nic-type 1=` nessa ordem, porque o
nome dessa opção muda entre as versões do `VBoxManage`; se nenhuma funcionar,
avisa para configurar a placa pela interface gráfica. `make vbox` continua
existindo, mas só imprime os passos (não mexe no VirtualBox).

## Pela linha de comando, à mão

```bash
VBoxManage createvm --name SaguiOS --ostype Other --register
VBoxManage modifyvm SaguiOS --memory 512 --firmware bios --audio none
VBoxManage storagectl SaguiOS --name "IDE Controller" --add ide --controller ICH6
VBoxManage storageattach SaguiOS --storagectl "IDE Controller" --port 0 --device 0 \
    --type hdd --medium build/saguios.vdi
# rede NAT com a placa Intel (troque 82540EM por Am79C973 para a PCnet-FAST III)
VBoxManage modifyvm SaguiOS --nic1 nat --nictype1 82540EM --cableconnected1 on
VBoxManage startvm SaguiOS
```

## O link demora ~5 segundos (e isso é normal)

O VirtualBox não levanta o link do controlador na hora: ele espera
`LinkUpDelay` ms depois do reset do adaptador — **5000 ms por padrão**, igual
para a Intel e para a PCnet. Enquanto o link está fora, o controlador emulado
**descarta todo quadro recebido e não transmite nada**, então quem pede DHCP
logo depois de inicializar a placa fica sem endereço sem mensagem nenhuma
(foi assim que a primeira versão se comportou aqui).

O SaguiOS espera: no boot você vê uma destas linhas no serial

```
Rede: link ligado
Rede: link ligado depois de 5002 ms de espera
```

Se o link não aparecer em 6 s (cabo desconectado, `--cableconnected1 off`,
adaptador desligado):

```
[aviso] Rede: link sem sinal depois de 6010 ms (cabo/adaptador desconectado?)
```

Nesse caso o sistema **segue normalmente** com o endereço fixo da NAT
(`10.0.2.15`) e você pode religar o cabo e rodar `rede` — o comando relê o
link na placa, então ele mostra `Link: ligado` assim que o cabo volta (o
endereço, porém, só é negociado de novo no próximo boot). Para conferir a
configuração do adaptador:

```bash
VBoxManage showvminfo saguiOS --machinereadable | grep -i nic
VBoxManage modifyvm saguiOS --cableconnected1 on
```

O mesmo atraso acontece ao "puxar o cabo" numa máquina ligada: os contadores
`Quadros` do comando `rede` param de subir até o link voltar.

## Como isso é testado aqui

O QEMU emula as duas mesmas placas, então o teste automático `tests/h35_rede.py`
roda o sistema com `-device e1000` e com `-device pcnet`, confere o endereço por
DHCP, o comando `rede`, e baixa uma página de um servidor HTTP local pelo
`wget`. O caminho do código é exatamente o mesmo que roda no VirtualBox; o que
não dá para automatizar aqui é o VirtualBox em si.

O atraso do link também é imitado: `tests/h36_link_atrasado.py` usa o monitor
do QEMU (`set_link net0 off` / `set_link net0 on`) para derrubar o link durante
o boot e levantá-lo 3 s depois, nas duas placas — o teste exige a espera, o
DHCP, o `rede` e o `wget` funcionando. O mesmo teste confere o caso do link que
só aparece depois do limite (8 s): aviso no boot, endereço fixo e `rede`
mostrando o link relido.
