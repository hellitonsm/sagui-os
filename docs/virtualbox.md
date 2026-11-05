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

## Pela linha de comando

```bash
VBoxManage createvm --name SaguiOS --ostype Other --register
VBoxManage modifyvm SaguiOS --memory 512 --firmware bios --audio none
VBoxManage storagectl SaguiOS --name IDE --add ide
VBoxManage storageattach SaguiOS --storagectl IDE --port 0 --device 0 \
    --type hdd --medium build/saguios.vdi
# rede NAT com a placa Intel (troque por pcnet para a PCnet-FAST III)
VBoxManage modifyvm SaguiOS --nic1 nat --nictype1 82540EM --cableconnected1 on
VBoxManage startvm SaguiOS
```

`make vbox` gera o `.vdi` e imprime esses passos.

## Como isso é testado aqui

O QEMU emula as duas mesmas placas, então o teste automático `tests/h35_rede.py`
roda o sistema com `-device e1000` e com `-device pcnet`, confere o endereço por
DHCP, o comando `rede`, e baixa uma página de um servidor HTTP local pelo
`wget`. O caminho do código é exatamente o mesmo que roda no VirtualBox; o que
não dá para automatizar aqui é o VirtualBox em si.
