#!/usr/bin/env bash
# Roda o saguiOS gerado no VirtualBox.
# Uso: tools/rodar-virtualbox.sh [--criar] [--apagar]
#   --criar   (padrão) cria uma nova VM "saguiOS" com o VDI como disco
#   --apagar  remove a VM "saguiOS" existente
set -euo pipefail

VM_NAME="saguiOS"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
VDI="$REPO_DIR/build/saguios.vdi"

if [[ ! -f "$VDI" ]]; then
    echo "ERRO: $VDI não encontrado. Rode 'make' e 'tools/gerar-linux.sh' primeiro."
    exit 1
fi

Acao="${1:---criar}"

if [[ "$Acao" == "--apagar" ]]; then
    echo "Apagando VM '$VM_NAME' (se existir)..."
    VBoxManage unregistervm "$VM_NAME" --delete 2>/dev/null || true
    exit 0
fi

# Cria a VM se não existir
if ! VBoxManage showvminfo "$VM_NAME" >/dev/null 2>&1; then
    echo "Criando VM '$VM_NAME'..."
    VBoxManage createvm --name "$VM_NAME" --ostype "Other_32" --register
    VBoxManage modifyvm "$VM_NAME" \
        --memory 256 \
        --cpus 1 \
        --boot1 disk \
        --nic1 none
fi

# Remove discos antigos e anexa o VDI atual
for slot in 0 1 2 3; do
    VBoxManage storagectl "$VM_NAME" --name "IDE Controller" --remove 2>/dev/null || true
done
VBoxManage storageattach "$VM_NAME" \
    --storagectl "IDE Controller" \
    --port 0 --device 0 \
    --type hdd \
    --medium "$VDI"

echo "VM '$VM_NAME' pronta com disco: $VDI"
echo "Para rodar: VBoxManage startvm $VM_NAME"