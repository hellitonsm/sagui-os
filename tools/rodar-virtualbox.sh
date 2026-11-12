#!/usr/bin/env bash
# Roda o SaguiOS gerado no VirtualBox.
# Uso: tools/rodar-virtualbox.sh [--criar|--atualizar|--rodar|--apagar]
#   --criar     (padrão) cria a VM "saguiOS" se não existir, atualiza a rede e
#               anexa o build/saguios.vdi como disco
#   --atualizar só reconecta a placa de rede e o disco (VM já existente)
#   --rodar     faz o de cima e liga a máquina
#   --apagar    remove a VM "saguiOS" (o .vdi no build/ não é apagado)
#
# A placa é configurada como Intel PRO/1000 MT Desktop (82540EM) em NAT, que é
# a que o driver e1000 do SaguiOS atende. A PCnet-FAST III (Am79C973, padrão do
# VirtualBox para "Outro/Desconhecido") também funciona, pelo driver pcnet.
set -euo pipefail

VM_NAME="saguiOS"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
VDI="$REPO_DIR/build/saguios.vdi"
NIC_TYPE="82540EM"
NIC_MODE="nat"

if ! command -v VBoxManage >/dev/null 2>&1; then
    echo "ERRO: VBoxManage não encontrado no PATH." >&2
    echo "      Instale o VirtualBox (o VBoxManage vem com ele) ou ajuste o PATH." >&2
    exit 1
fi

Acao="${1:---criar}"

if [[ "$Acao" != "--apagar" && ! -f "$VDI" ]]; then
    echo "ERRO: $VDI não encontrado. Rode 'make vdi' (ou tools/gerar-linux.sh) primeiro." >&2
    exit 1
fi

case "$Acao" in
--apagar)
    echo "Apagando VM '$VM_NAME' (se existir)..."
    # Sem desanexar, "unregistervm --delete" apaga também o .vdi do build/.
    VBoxManage storageattach "$VM_NAME" --storagectl "IDE Controller" \
        --port 0 --device 0 --type hdd --medium none >/dev/null 2>&1 || true
    VBoxManage unregistervm "$VM_NAME" --delete 2>/dev/null || true
    echo "Pronto. O disco $VDI continua no lugar."
    exit 0
    ;;
--criar|--atualizar|--rodar) ;;
*)
    echo "Uso: $0 [--criar|--atualizar|--rodar|--apagar]" >&2
    exit 2
    ;;
esac

# --- cria a VM (só na primeira vez) ---------------------------------------
if ! VBoxManage showvminfo "$VM_NAME" >/dev/null 2>&1; then
    echo "Criando VM '$VM_NAME'..."
    VBoxManage createvm --name "$VM_NAME" --ostype Other --register >/dev/null
    # 512 MB, BIOS tradicional (o SaguiOS não tem suporte a EFI), mouse e
    # teclado PS/2 (é o que o sistema tem driver para ler) e 16 MB de vídeo
    # para o VESA 1024x768x32 da área de trabalho.
    VBoxManage modifyvm "$VM_NAME" \
        --memory 512 \
        --cpus 1 \
        --firmware bios \
        --vram 16 \
        --mouse ps2 \
        --keyboard ps2 \
        --audio none \
        --boot1 disk \
        --boot2 none \
        --boot3 none \
        --boot4 none >/dev/null
    VBoxManage storagectl "$VM_NAME" --name "IDE Controller" --add ide --controller ICH6 >/dev/null
fi

# --- rede: aplicada sempre ------------------------------------------------
# (uma VM criada antes desta configuração também passa a usar a placa certa)
configurar_rede() {
    # O nome da opção mudou entre versões do VirtualBox: --nic-type1 (7.x em
    # algumas builds), --nictype1 (clássico) e --nic-type 1=... (7.2). Tenta na
    # ordem e para na primeira que o VBoxManage aceitar.
    if VBoxManage modifyvm "$VM_NAME" --nic1 "$NIC_MODE" --nic-type1 "$NIC_TYPE" >/dev/null 2>&1; then return 0; fi
    if VBoxManage modifyvm "$VM_NAME" --nic1 "$NIC_MODE" --nictype1 "$NIC_TYPE"  >/dev/null 2>&1; then return 0; fi
    if VBoxManage modifyvm "$VM_NAME" --nic "1=$NIC_MODE" --nic-type "1=$NIC_TYPE" >/dev/null 2>&1; then return 0; fi
    echo "ERRO: não consegui configurar a placa de rede ($NIC_TYPE, $NIC_MODE)." >&2
    echo "      Configure à mão: Configurações -> Rede -> Adaptador 1 ->" >&2
    echo "      Ligado a NAT, Tipo = Intel PRO/1000 MT Desktop." >&2
    return 1
}
configurar_rede
VBoxManage modifyvm "$VM_NAME" --nic-promisc1 deny --macaddress1 auto 2>/dev/null || true
VBoxManage modifyvm "$VM_NAME" --cable-connected1 on 2>/dev/null \
    || VBoxManage modifyvm "$VM_NAME" --cableconnected1 on 2>/dev/null || true

# Encaminhamento de porta da NAT: a porta 2500 do convidado (o servidor de
# arquivos SAGUI/1, ver kernel/rede/servidor.c e o cliente
# tools/windows/sagui-fs.ps1) fica escutando no host em 127.0.0.1:2500. Só o
# localhost do host pode conectar, nada é exposto para a rede em volta. A regra
# tem nome, então a antiga é removida primeiro (o erro é ignorado quando a
# regra ainda não existe) e reaplicada sempre.
VBoxManage modifyvm "$VM_NAME" --natpf1 delete "sagui-arquivos" >/dev/null 2>&1 || true
if ! VBoxManage modifyvm "$VM_NAME" --natpf1 "sagui-arquivos,tcp,127.0.0.1,2500,,2500" >/dev/null 2>&1; then
    echo "AVISO: não consegui encaminhar a porta 2500 do convidado para o host." >&2
    echo "        O cliente de arquivos (tools/windows/sagui-fs.ps1) não vai conectar." >&2
fi

# --- disco: reanexa o VDI atual -------------------------------------------
anexar_disco() {
    VBoxManage storageattach "$VM_NAME" \
        --storagectl "IDE Controller" \
        --port 0 --device 0 \
        --type hdd \
        --medium "$VDI" >/dev/null
}
# Desanexa o que estiver lá (o .vdi é regenerado pelo make, com outro UUID).
VBoxManage storageattach "$VM_NAME" --storagectl "IDE Controller" \
    --port 0 --device 0 --type hdd --medium none >/dev/null 2>&1 || true
# Cada nova geração do .vdi tem outro UUID. Sem soltar o registro anterior na
# media registry do VirtualBox, o startvm reclama de UUID incompatível.
VBoxManage closemedium disk "$VDI" >/dev/null 2>&1 || true
if ! anexar_disco; then
    echo "ERRO: não consegui anexar $VDI ao controlador IDE." >&2
    exit 1
fi

echo "VM '$VM_NAME' pronta:"
echo "  placa:  Intel PRO/1000 MT Desktop ($NIC_TYPE) em $NIC_MODE"
echo "  disco:  $VDI"
echo "  porta:  127.0.0.1:2500 -> 2500 do convidado (cliente de arquivos)"
echo "Para ligar: VBoxManage startvm \"$VM_NAME\"   (ou $0 --rodar)"

if [[ "$Acao" == "--rodar" ]]; then
    echo "Ligando..."
    VBoxManage startvm "$VM_NAME"
fi
