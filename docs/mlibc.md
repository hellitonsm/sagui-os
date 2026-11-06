# mlibc para compilar programas do SaguiOS

## Pergunta

Musl e dietlibc foram analisados e rejeitados como porte inteiro.
O pedido novo: **usar o mlibc** (managarm) para compilar programas
do SaguiOS. Referencia: `https://docs.managarm.org/mlibc-book/porting/intro.html`.

Resposta curta: o mlibc e **melhor que musl/dietlibc para este
trabalho**, porque ele foi feito para ser portado. Mas o porte
completo (Meson + cross-toolchain GCC + sysdeps em C++) e grande.
Para o SaguiOS, o passo pratico e adaptar **só os sysdeps**
obrigatorios sobre a libsagui, com link estatico, sem trazer o
mlibc inteiro.

## O que o guia do mlibc pede (resumo)

1. Kernel com: texto/serial, paging, ELF loader, syscalls de userspace.
2. Implementar `sysdeps/<sistema>` e ligar no `meson.build` do mlibc
   com `sysdep_supported_options` (ex.: `'posix': true`).
3. ABI headers: o guia recomenda reusar uma ABI existente
   (ex.: Linux) e symlinkar `abis/linux/include/abi-bits/*.h`.
4. Toolchain OS-specific (binutils + GCC com `--with-sysroot`), com
   `(S)crt1.o` no caminho do linker.
5. `static` primeiro (`-Ddefault_library=static`), depois dinamico.
6. Sysdeps obrigatorios para "hello" estatico:

   `Exit`, `FutexWait/Wake`, `Open`, `Read`, `Write`, `Seek`,
   `Close`, `ClockGet`, `LibcLog`, `LibcPanic`, `AnonAllocate/Free`,
   `VmMap/Unmap`, `TcbSet`.

## O que o SaguiOS ja tem

- Serial/texto, paginacao (um espaco por processo), ELF loader e
  syscalls via `int 0x80` e `kernel/inc/abi.h`.
- Um processo = um espaco de endereco (sem threads de verdade).
- `inicio.asm` com `_start` e `libsagui` equivalente ao `crt1`+entry
  do mlibc, so que em C/asm simples e sem C++/Meson.

Ou seja, os pre-requisitos minimos do guia **ja existem**. O que
falta sao `Futex`, `VmMap/Unmap` e `TcbSet` (threads/TCB), que o
SaguiOS nao tem nem precisa para programas monoprocesso.

## Mapeamento sagui (sysdeps) x mlibc

| sysdep mlibc | SaguiOS hoje | Adaptacao |
| --- | --- | --- |
| `Exit` | `SIS_SAIR` | direto |
| `Open/Read/Write/Seek/Close` | `SIS_ABRIR/LER/ESCREVER/POSICIONAR/FECHAR` | direto |
| `ClockGet` | `SIS_TEMPO` | `ms -> timespec` |
| `LibcLog/Panic` | `SIS_REGISTRAR` (serial) | direto |
| `AnonAllocate/Free` | `aumentar_memoria` + `aloca/libera` | direto, sem `mmap` |
| `VmMap/Unmap` | paginacao ja separa processos | stub no-op p/ single-thread |
| `FutexWait/Wake` | sem threads | stub que retorna `ENOSYS` ou dorme 0 |
| `TcbSet` | sem TCB | stub no-op |
| `_start`/`crt1` | `userland/libsagui/inicio.asm` | reusar como esta |

O resto do POSIX (stdio, malloc, string) ja e a libsagui renomeada,
igual ao esqueleto `userland/musl/` que pode ser reaproveitado como
`userland/mlibc/` trocando so o nome/diretorio.

## Por que o mlibc e melhor que musl/dietlibc aqui

- Foi desenhado para "um kernel novo por vez": a camada de syscall
  e um diretorio isolado (`sysdeps/<sistema>`), diferente dos outros
  que amarram a syscall Linux no meio do codigo.
- Permite `headers_only` e `static` primeiro: da para ter os
  headers POSIX sem puxar threads/DNS/FPU.
- ABI headers sao separados (`abi-bits/*`): da para reusar a ABI
  Linux nos headers e nao implementar Linux no kernel, exatamente
  como o guia recomenda. No SaguiOS, o equivalente seria manter
  `abi.h` proprio e so symlinkar `abi-bits` que batem.
- Menos codigo morto que o musl inteiro e mais futuro que a shim
  atual: se o SaguiOS um dia ganhar threads, o caminho mlibc ja tem
  lugar para `TcbSet/Futex`.

## Por que nao trazer o mlibc inteiro agora

- Exige Meson + cross-toolchain GCC (`i686-sagui-gcc`, sysroot) com
  patches binutils/GCC, mais C++ (`sysdeps.cpp`, `__mlibc_entry`).
- Exige cumprir os 15 sysdeps obrigatorios, incluindo memoria
  anonima e VM map que o kernel hoje nao expoe como chamada.
- O PRD proibe bibliotecas prontas e pede toolchain freestanding
  simples (`gcc -m32`, D001). Trazer o mlibc inteiro viola isso e
  polui a imagem sem ganho imediato.

## Passo pratico proposto (sem porte inteiro)

1. Reaproveitar `userland/musl/` como `userland/mlibc/`:
   headers POSIX + `sysdeps.c` sobre a libsagui.
2. `Makefile`: `-Iuserland/mlibc/inc` no `UCFLAGS` e `mlibc.o` no
   `LIBSAGUI_OBJ`, com um teste `userland/testes/mlibc_nativo.c`
   (`malloc/printf/open`) e `tests/h43_mlibc.py`.
3. `tools/i686-sagui.cross` (exemplo de cross file do guia,
   adaptado) e nota de como compilar um programa externo.
4. Nao baixar o mlibc oficial para dentro do repo; manter a
   adaptacao fina e documentada.

Ver decisao D037 em `docs/decisoes.md` e `docs/dietlibc.md`
para a comparacao anterior.

## Implementacao (sem porte inteiro)

Feito em `userland/mlibc/` (headers POSIX em `inc/` + `sysdeps.c`
sobre a libsagui + `inc/mlibc/sysdeps.h` com os 15 obrigatorios,
stubs `Futex/VmMap/TcbSet`), `Makefile` com `-Iuserland/mlibc/inc`,
`tools/i686-sagui.cross` (exemplo do guia) e teste
`userland/testes/mlibc_nativo.c` (`tests/h43_mlibc.py`).
Um programa externo usa `principal()` como ponto de entrada
(igual aos demais) e os nomes POSIX incluindo `<stdio.h>`.
Ver D037.
