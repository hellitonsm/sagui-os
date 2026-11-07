# Makefile do SaguiOS
#
# Alvos principais:
#   make            -> gera build/saguios.img (imagem para pendrive)
#   make vdi        -> gera também build/saguios.vdi (VirtualBox)
#   make test       -> roda todos os testes automáticos no QEMU
#   make rodar      -> abre o sistema no QEMU com a placa Intel e1000
#   make rodar-pcnet-> abre no QEMU com a placa AMD PCnet (a padrão do VirtualBox)
#   make vbox       -> gera o .vdi e mostra como ligar a rede no VirtualBox
#   make entrega FASE=N -> copia .img, .vdi e SHA-256 para entrega/faseN/
#   make vazio      -> alvo vazio (critério da H01)
#   make limpar     -> apaga build/

# O alvo padrao do make e a imagem, e nao a primeira regra do arquivo.
.DEFAULT_GOAL := all

# O GCC do sistema em modo 32 bits e freestanding faz o papel do compilador
# cruzado (ver docs/decisoes.md, D001).
CC      := gcc
AS      := nasm
LD      := ld
PY      := python3

CFLAGS  := -m32 -march=i686 -std=gnu11 -O2 -g \
           -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie \
           -fno-stack-protector -fno-asynchronous-unwind-tables \
           -mgeneral-regs-only -fno-omit-frame-pointer \
           -Wall -Wextra -Werror=implicit-function-declaration \
           -finput-charset=UTF-8 -fexec-charset=ISO-8859-1 \
           -DSAGUI_KERNEL -Ikernel/inc
LDFLAGS := -m elf_i386 -nostdlib --no-dynamic-linker -z noexecstack --no-warn-rwx-segments

B       := build

KERNEL_C   := $(shell find kernel -name '*.c' | sort)
KERNEL_ASM := $(shell find kernel -name '*.asm' | sort)
KERNEL_OBJ := $(patsubst %.c,$(B)/%.o,$(KERNEL_C)) $(patsubst %.asm,$(B)/%.asm.o,$(KERNEL_ASM)) \
              $(B)/gerado/fonte8x16.o
KERNEL_HDR := $(shell find kernel -name '*.h')
ROOTFS     := $(shell find rootfs -type f | sort)

# Programas de usuário: libsagui + shim POSIX local (userland/mlibc) + um .c
# por programa em userland/programas/ (/bin) ou userland/testes/ (/testes).
# asagui e csagui entram pelo wildcard; csagui executa /bin/asagui no convidado.
UCFLAGS := -m32 -march=i686 -std=gnu11 -O2 \
           -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie \
           -fno-stack-protector -fno-asynchronous-unwind-tables -mgeneral-regs-only \
           -ffunction-sections -fdata-sections \
           -Wall -Wextra -Werror=implicit-function-declaration \
           -finput-charset=UTF-8 -fexec-charset=ISO-8859-1 \
           -Iuserland/inc -Iuserland/mlibc/inc -idirafter kernel/inc
ULDFLAGS := -m elf_i386 -nostdlib --gc-sections -z max-page-size=4096 \
            -z noexecstack --no-warn-rwx-segments
MLIBC_SRC := $(wildcard userland/mlibc/*.c)
MLIBC_OBJ := $(patsubst userland/mlibc/%.c,$(B)/userland/mlibc/%.o,$(MLIBC_SRC))
LIBSAGUI_OBJ := $(B)/userland/fonte8x16.o \
                $(patsubst userland/libsagui/%.c,$(B)/userland/libsagui/%.o,$(wildcard userland/libsagui/*.c)) \
                $(MLIBC_OBJ) \
                $(patsubst userland/libsagui/%.asm,$(B)/userland/libsagui/%.asm.o,$(wildcard userland/libsagui/*.asm))
USER_HDR  := $(wildcard userland/inc/*.h) $(shell find userland/mlibc -name '*.h' -type f | sort) kernel/inc/abi.h
PROGRAMAS := $(patsubst userland/programas/%.c,$(B)/userland/bin/%,$(wildcard userland/programas/*.c))
# O Sagui 3D (B4) tem vários arquivos, numa pasta própria.
SAGUI3D_OBJ := $(patsubst userland/sagui3d/%.c,$(B)/userland/sagui3d/%.o,$(wildcard userland/sagui3d/*.c))
PROGRAMAS += $(B)/userland/bin/sagui3d
PROG_TESTE := $(patsubst userland/testes/%.c,$(B)/userland/testes/%,$(wildcard userland/testes/*.c))

# ---------- mlibc upstream (opcional: make LIBC=upstream) ----------
# Vendorizado em .local/mlibc; construido por tools/mlibc-sagui/setup.ps1;
# artefatos em build/mlibc-upstream/ (libc.a, crt1.o e headers). Usado apenas
# pelo programa de teste upstream-teste (make LIBC=upstream); o tinycc e
# compilado contra a libc nativa do SaguiOS.
MLIBC_UPSTREAM := build/mlibc-upstream
TOOLCHAIN := .local/windows-tools/toolchain/bin/i686-elf-
UPSTREAM_CC := $(TOOLCHAIN)gcc.exe
UPSTREAM_LD := $(TOOLCHAIN)ld.exe
UPSTREAM_OBJCOPY := $(TOOLCHAIN)objcopy.exe
UPSTREAM_CFLAGS := -std=gnu11 -O2 -g \
    -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie \
    -fno-stack-protector -fno-asynchronous-unwind-tables -mgeneral-regs-only \
    -Wall -Wextra -Werror=implicit-function-declaration \
    -D_GNU_SOURCE -I$(MLIBC_UPSTREAM)/include
UPSTREAM_LIBC := $(MLIBC_UPSTREAM)/lib/libc.a
UPSTREAM_CRT1 := $(MLIBC_UPSTREAM)/lib/crt1.o
TINYCC_LIBGCC := .local/windows-tools/toolchain/lib/gcc/i686-elf/15.2.0/libgcc.a

# (Re)constroi os artefatos do mlibc upstream quando necessario. Se libc.a e
# crt1.o ja existem (build/mlibc-upstream), pula: rodar de novo corrompe o
# build existente com meson/ninja incompativel.
$(B)/mlibc-upstream.stamp $(UPSTREAM_LIBC) $(UPSTREAM_CRT1):
	@if [ ! -f $(UPSTREAM_LIBC) ] || [ ! -f $(UPSTREAM_CRT1) ]; then \
	    powershell -NoProfile -ExecutionPolicy Bypass -File tools/mlibc-sagui/setup.ps1; \
	else \
	    echo "mlibc upstream: artefatos existentes, pulando setup"; \
	fi
	@: > $@

ifeq ($(LIBC),upstream)
# Programa de teste que usa exclusivamente a API POSIX do mlibc upstream.
PROGRAMAS += $(B)/userland/bin/upstream-teste

$(B)/userland/programas/upstream-teste.o: userland/programas/upstream-teste.c
	@mkdir -p $(dir $@)
	$(UPSTREAM_CC) $(UPSTREAM_CFLAGS) -c -o $@ $<

$(B)/userland/bin/upstream-teste: $(B)/userland/programas/upstream-teste.o $(UPSTREAM_CRT1) $(UPSTREAM_LIBC) userland/programa-upstream.ld
	@mkdir -p $(dir $@)
	$(UPSTREAM_LD) $(ULDFLAGS) -T userland/programa-upstream.ld -o $@.elf $(UPSTREAM_CRT1) $< $(UPSTREAM_LIBC) $(TINYCC_LIBGCC)
	$(UPSTREAM_OBJCOPY) --strip-all $@.elf $@
endif

# ---------- tinycc: o compilador C dentro do SaguiOS ----------
# TinyCC (.local/tinycc, ver tools/tinycc-sagui/setup.ps1) compilado como
# qualquer outro programa do SaguiOS (libsagui + shim POSIX local) e
# instalado como /bin/tinycc no convidado, com o sysroot em /usr. Um programa
# em C pode ser compilado DENTRO do SaguiOS e executado ali mesmo:
#   tinycc -o /tmp/ola /tcc/hello-tcc.c && /tmp/ola
TINYCC_DIR    := .local/tinycc
TINYCC_CFLAGS := $(UCFLAGS) -mlong-double-64 -Wno-unused-function \
                 -Wno-unused-variable -I$(TINYCC_DIR)
TINYCC_LIBTCC1_OBJS := libtcc1 stdatomic builtin dsohandle
TINYCC_LIBTCC1_ASM  := alloca alloca-bt atomic pic86
TINYCC_LIBTCC1_OBJ  := $(addprefix $(B)/tinycc/,$(addsuffix .o,$(TINYCC_LIBTCC1_OBJS) $(TINYCC_LIBTCC1_ASM)))
TINYCC_CRT          := $(B)/tinycc/crti.o $(B)/tinycc/crtn.o
TINYCC_CRT1         := $(B)/tinycc/crt1-sagui.o
# Objetos da libc nativa do SaguiOS (shim POSIX + libsagui, sem fonte8x16 e sem
# o kit grafico). Empacotados como libc.a no sysroot.
SAGUI_LIBCORE_OBJ    := $(patsubst userland/libsagui/%.c,$(B)/userland/libsagui/%.o,$(filter-out userland/libsagui/gui.c,$(wildcard userland/libsagui/*.c)))
SAGUI_LIBC_OBJ       := $(MLIBC_OBJ) \
                        $(SAGUI_LIBCORE_OBJ) \
                        $(patsubst userland/libsagui/%.asm,$(B)/userland/libsagui/%.asm.o,$(wildcard userland/libsagui/*.asm))
# Biblioteca grafica do SaguiOS para programas C compilados com o tinycc:
# o kit grafico (userland/libsagui/gui.c) + a fonte 8x16. Instalada no
# sysroot como /usr/lib/libsaguigui.a; ligue com -lsaguigui.
SAGUI_GUI_OBJ        := $(B)/userland/libsagui/gui.o $(B)/userland/fonte8x16.o

# Clona e configura o tinycc quando necessario (vendored em .local/tinycc).
$(TINYCC_DIR)/tcc.c $(TINYCC_DIR)/config.h $(TINYCC_DIR)/lib/libtcc1.c:
	powershell -NoProfile -ExecutionPolicy Bypass -File tools/tinycc-sagui/setup.ps1

# O proprio tinycc: tudo vem de tcc.c (ONE_SOURCE) + o shim de entrada que
# chama principal() (a convencao dos programas do SaguiOS).
$(B)/tinycc/tcc.o: $(TINYCC_DIR)/tcc.c $(TINYCC_DIR)/config.h $(USER_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(TINYCC_CFLAGS) -c -o $@ $<
$(B)/tinycc/dsohandle.o: $(TINYCC_DIR)/lib/dsohandle.c
	@mkdir -p $(dir $@)
	$(CC) $(TINYCC_CFLAGS) -c -o $@ $<
$(B)/tinycc/principal.o: tools/tinycc-sagui/principal.c
	@mkdir -p $(dir $@)
	$(CC) $(TINYCC_CFLAGS) -c -o $@ $<
$(B)/userland/bin/tinycc: $(B)/tinycc/tcc.o $(B)/tinycc/dsohandle.o $(B)/tinycc/principal.o $(B)/tinycc/float_proibido.o $(LIBSAGUI_OBJ) $(B)/tinycc/libtcc1.a userland/programa.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) --gc-sections -T userland/programa.ld -o $@.elf $(B)/tinycc/tcc.o $(B)/tinycc/dsohandle.o $(B)/tinycc/principal.o $(B)/tinycc/float_proibido.o $(LIBSAGUI_OBJ) $(B)/tinycc/libtcc1.a
	objcopy --strip-all $@.elf $@

# Stub dos helpers de ponto flutuante (IEEE double): o SaguiOS desliga a
# FPU em CR0.EM e nao tem soft-float; o tinycc os referencia por dentro,
# entao aqui eles existem so para abortar com erro.
$(B)/tinycc/float_proibido.o: tools/tinycc-sagui/float_proibido.c
	@mkdir -p $(dir $@)
	$(CC) $(TINYCC_CFLAGS) -c -o $@ $<

# libtcc1.a: rotinas de 64 bits e atomicos que um programa compilado precisa.
$(B)/tinycc/%.o: $(TINYCC_DIR)/lib/%.c
	@mkdir -p $(dir $@)
	$(UPSTREAM_CC) -m32 -march=i686 -std=gnu11 -O2 -ffreestanding -fno-builtin \
	    -fno-stack-protector -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections \
	    -Wno-unused-function -Wno-unused-variable -I$(TINYCC_DIR) -c -o $@ $<
$(B)/tinycc/%.o: $(TINYCC_DIR)/lib/%.S
	@mkdir -p $(dir $@)
	$(UPSTREAM_CC) -m32 -march=i686 -O2 -ffreestanding -fno-builtin \
	    -fno-stack-protector -fno-asynchronous-unwind-tables -I$(TINYCC_DIR) \
	    -c -o $@ $<
$(B)/tinycc/libtcc1.a: $(TINYCC_LIBTCC1_OBJ)
	$(TOOLCHAIN)ar.exe rcs $@ $(TINYCC_LIBTCC1_OBJ)
$(B)/tinycc/crti.o: tools/tinycc-sagui/crti.S
	@mkdir -p $(dir $@)
	$(UPSTREAM_CC) -m32 -march=i686 -c -o $@ $<
$(B)/tinycc/crtn.o: tools/tinycc-sagui/crtn.S
	@mkdir -p $(dir $@)
	$(UPSTREAM_CC) -m32 -march=i686 -c -o $@ $<
$(B)/tinycc/crt1-sagui.o: tools/tinycc-sagui/crt1-sagui.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf32 -o $@ $<
# Sysroot: headers da libc Sagui (shim + libsagui + gui.h), crt1.o/crti.o/crtn.o,
# libc.a, libsaguigui.a e libtcc1.a. O ramdisk instala isso como /usr no
# convidado.
$(B)/sysroot: $(SAGUI_LIBC_OBJ) $(SAGUI_GUI_OBJ) $(TINYCC_CRT1) $(B)/tinycc/libtcc1.a $(TINYCC_CRT) $(TINYCC_DIR)/include
	rm -rf $(B)/sysroot
	mkdir -p $(B)/sysroot/include $(B)/sysroot/lib/tcc/include
	cp -r userland/mlibc/inc/. $(B)/sysroot/include/
	cp userland/inc/sagui.h $(B)/sysroot/include/sagui.h
	cp userland/inc/gui.h $(B)/sysroot/include/gui.h
	cp kernel/inc/abi.h $(B)/sysroot/include/abi.h
	cp $(TINYCC_CRT1) $(B)/sysroot/lib/crt1.o
	cp $(B)/tinycc/crti.o $(B)/sysroot/lib/crti.o
	cp $(B)/tinycc/crtn.o $(B)/sysroot/lib/crtn.o
	$(TOOLCHAIN)ar.exe rcs $(B)/sysroot/lib/libc.a $(SAGUI_LIBC_OBJ)
	$(TOOLCHAIN)ar.exe rcs $(B)/sysroot/lib/libsaguigui.a $(SAGUI_GUI_OBJ)
	cp $(B)/tinycc/libtcc1.a $(B)/sysroot/lib/tcc/libtcc1.a
	# Headers do tcc que o SaguiOS nao tem; stdarg/stddef/stdbool ficam com os
	# do SaguiOS (os builtins do tcc conflitam com eles).
	cp $(TINYCC_DIR)/include/float.h $(TINYCC_DIR)/include/stdatomic.h \
	   $(TINYCC_DIR)/include/stdalign.h $(TINYCC_DIR)/include/stdnoreturn.h \
	   $(TINYCC_DIR)/include/tgmath.h $(TINYCC_DIR)/include/varargs.h \
	   $(TINYCC_DIR)/include/tccdefs.h $(B)/sysroot/lib/tcc/include/
PROGRAMAS += $(B)/userland/bin/tinycc

.PHONY: all vazio vdi test rodar rodar-pcnet vbox entrega limpar dashboard

all: $(B)/saguios.img

vazio:
	@echo "SaguiOS: alvo vazio ok"

# ---------- carregador de boot ----------
$(B)/boot/mbr.bin: boot/mbr.asm boot/cabecalho.inc
	@mkdir -p $(dir $@)
	$(AS) -f bin -I boot/ -o $@ $<

$(B)/boot/estagio2.bin: boot/estagio2.asm $(wildcard boot/*.inc)
	@mkdir -p $(dir $@)
	$(AS) -f bin -I boot/ -o $@ $<

# ---------- kernel ----------
$(B)/kernel/%.o: kernel/%.c $(KERNEL_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(B)/kernel/%.asm.o: kernel/%.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf32 -I kernel/arch/ -o $@ $<

# A fonte é desenhada em texto (tools/fonte/) e vira C no build.
$(B)/gerado/fonte8x16.c: tools/fonte/sagui8x16.txt tools/mkfonte.py
	@mkdir -p $(dir $@)
	$(PY) tools/mkfonte.py $< $@

$(B)/gerado/fonte8x16.o: $(B)/gerado/fonte8x16.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(B)/kernel.elf: $(KERNEL_OBJ) kernel/ligacao.ld
	$(LD) $(LDFLAGS) -T kernel/ligacao.ld -o $@ $(KERNEL_OBJ)

$(B)/kernel.bin: $(B)/kernel.elf
	objcopy -O binary $< $@

# ---------- programas de usuário ----------
$(B)/userland/%.o: userland/%.c $(USER_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c -o $@ $<

$(B)/userland/fonte8x16.o: $(B)/gerado/fonte8x16.c
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c -o $@ $<

$(B)/userland/libsagui/%.asm.o: userland/libsagui/%.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf32 -o $@ $<

$(B)/userland/bin/%: $(B)/userland/programas/%.o $(LIBSAGUI_OBJ) userland/programa.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -T userland/programa.ld -o $@.elf $< $(LIBSAGUI_OBJ)
	objcopy --strip-all $@.elf $@

$(B)/userland/sagui3d/%.o: userland/sagui3d/%.c userland/sagui3d/jogo.h $(USER_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c -o $@ $<

$(B)/userland/bin/sagui3d: $(SAGUI3D_OBJ) $(LIBSAGUI_OBJ) userland/programa.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -T userland/programa.ld -o $@.elf $(SAGUI3D_OBJ) $(LIBSAGUI_OBJ)
	objcopy --strip-all $@.elf $@

$(B)/userland/testes/%: $(B)/userland/testes/%.o $(LIBSAGUI_OBJ) userland/programa.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -T userland/programa.ld -o $@.elf $< $(LIBSAGUI_OBJ)
	objcopy --strip-all $@.elf $@

# ---------- ramdisk (SGFS) ----------
# O ramdisk é montado a partir de uma cópia do rootfs/ com os programas em
# /bin e os programas de teste em /testes.
$(B)/ramdisk.sgfs: $(ROOTFS) $(PROGRAMAS) $(PROG_TESTE) $(B)/sysroot tools/mksgfs.py
	rm -rf $(B)/rootfs
	cp -r rootfs $(B)/rootfs
	mkdir -p $(B)/rootfs/usr/lib
	cp -r $(B)/sysroot/include $(B)/rootfs/usr/include
	cp -r $(B)/sysroot/lib/. $(B)/rootfs/usr/lib/
	$(if $(PROGRAMAS),cp $(PROGRAMAS) $(B)/rootfs/bin/)
	$(if $(PROG_TESTE),mkdir -p $(B)/rootfs/testes && cp $(PROG_TESTE) $(B)/rootfs/testes/)
	$(PY) tools/mksgfs.py $(B)/rootfs $@

# ---------- imagem final ----------
$(B)/saguios.img: $(B)/boot/mbr.bin $(B)/boot/estagio2.bin $(B)/kernel.bin $(B)/ramdisk.sgfs tools/mkimagem.py
	$(PY) tools/mkimagem.py --mbr $(B)/boot/mbr.bin --estagio2 $(B)/boot/estagio2.bin \
	    --kernel $(B)/kernel.bin --kernel-elf $(B)/kernel.elf --ramdisk $(B)/ramdisk.sgfs \
	    --saida $@ --vesa
	cd $(B) && sha256sum saguios.img > saguios.img.sha256

$(B)/saguios.vdi: $(B)/saguios.img
	rm -f $@ $(B)/vdi.raw
	cp $< $(B)/vdi.raw
	# disco virtual com tamanho múltiplo de 1 MB (ver D004)
	truncate -s %1M $(B)/vdi.raw
	qemu-img convert -f raw -O vdi $(B)/vdi.raw $@
	rm -f $(B)/vdi.raw
	cd $(B) && sha256sum saguios.vdi > saguios.vdi.sha256

vdi: $(B)/saguios.vdi

test: $(B)/saguios.img
	$(PY) tests/rodar_todos.py

# As duas placas que o VirtualBox oferece (e o QEMU imita) precisam do mesmo
# driver genérico: e1000 -> Intel 8254x, pcnet -> AMD PCnet-FAST III.
rodar: $(B)/saguios.img
	qemu-system-i386 -m 512M -drive format=raw,file=$< -serial stdio \\
	    -netdev user,id=net0 -device e1000,netdev=net0

rodar-pcnet: $(B)/saguios.img
	qemu-system-i386 -m 512M -drive format=raw,file=$< -serial stdio \\
	    -netdev user,id=net0 -device pcnet,netdev=net0

# Liga a imagem no VirtualBox: anexa o .vdi e explica a configuração da rede.
vbox: $(B)/saguios.vdi
	@echo "VirtualBox (o sistema já sabe falar com as duas placas):"
	@echo "  1. Novo -> tipo \"Outro/Outro (32 bits)\" -> use um disco existente:"
	@echo "       $(abspath $(B)/saguios.vdi)"
	@echo "  2. Configurações -> Rede -> Adaptador 1 -> habilitado, ligado a NAT"
	@echo "     (aceita também \"Bridge\" e \"Host-only\", que usam DHCP):"
	@echo "       Intel PRO/1000 MT Desktop  ou  PCnet-FAST III"
	@echo "  3. Ligue a máquina. O boot mostra a placa e o IP; no shell: rede / wget"

entrega: $(B)/saguios.img $(B)/saguios.vdi
	@test -n "$(FASE)" || (echo "use: make entrega FASE=N" && false)
	mkdir -p entrega/fase$(FASE)
	cp $(B)/saguios.img $(B)/saguios.vdi entrega/fase$(FASE)/
	cd entrega/fase$(FASE) && sha256sum saguios.img saguios.vdi > SHA256SUMS.txt
	@cat entrega/fase$(FASE)/SHA256SUMS.txt

dashboard:
	$(PY) tools/status.py atualizar

limpar:
	rm -rf $(B)
