#!/usr/bin/env bash
# Gera os artefatos do SaguiOS em Linux (Ubuntu/Debian e derivados).
# Uso: ./tools/gerar-linux.sh [--testar] [--instalar-dependencias]
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
PATH="/usr/sbin:/sbin:$PATH"
export PATH
cd "$ROOT"
TESTAR=0
INSTALAR=0
for arg in "$@"; do
  case "$arg" in
    --testar) TESTAR=1 ;;
    --instalar-dependencias) INSTALAR=1 ;;
    -h|--help)
      cat <<'EOF'
Uso: ./tools/gerar-linux.sh [opções]
  --testar                  roda também a suíte automática no QEMU
  --instalar-dependencias   instala os pacotes necessários (Ubuntu/Debian; sudo)
  -h, --help                mostra esta ajuda

Saídas: build/saguios.img, build/saguios.vdi e hashes SHA-256.
EOF
      exit 0 ;;
    *) echo "Opção desconhecida: $arg" >&2; exit 2 ;;
  esac
done

pacotes=(build-essential gcc-multilib binutils nasm make python3 qemu-system-x86 qemu-utils imagemagick mtools dosfstools fdisk)
comandos=(gcc ld objcopy nasm make python3 qemu-system-i386 qemu-img convert sfdisk)
if (( INSTALAR )); then
  command -v apt-get >/dev/null || { echo "Instalação automática suportada somente em Debian/Ubuntu (apt-get)." >&2; exit 1; }
  command -v sudo >/dev/null || { echo "Instale os pacotes como administrador: ${pacotes[*]}" >&2; exit 1; }
  sudo apt-get update
  sudo apt-get install -y "${pacotes[@]}"
fi

faltam=()
for cmd in "${comandos[@]}"; do
  command -v "$cmd" >/dev/null 2>&1 || faltam+=("$cmd")
done
if ((${#faltam[@]})); then
  printf 'Faltam ferramentas: %s\n' "${faltam[*]}" >&2
  echo "Ubuntu/Debian: sudo apt-get install ${pacotes[*]}" >&2
  exit 1
fi

make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)" vdi
if (( TESTAR )); then make test; fi
printf '\nConcluído. Artefatos:\n  %s/build/saguios.img\n  %s/build/saguios.vdi\n  %s/build/saguios.vdi.sha256\n' "$ROOT" "$ROOT" "$ROOT"
