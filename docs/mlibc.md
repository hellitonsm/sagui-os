# mlibc upstream no SaguiOS

Port do [mlibc](https://github.com/mlibc/mlibc) (upstream, Meson) como
**libc estática vendorizada** do SaguiOS (i386, freestanding).

- Clone/vendor: `.local/mlibc` (commit `edd69e3`, `.gitignore` ignora `/.local/`).
- Fonte da verdade do porte: `tools/mlibc-sagui/` (overlay do sysdep sagui,
  patches e script de setup reproduzível).
- Build: `.local/mlibc-build/` (Meson/Ninja); artefatos finais em
  `build/mlibc-upstream/` (`libc.a`, `crt1.o`, headers).
- Integração opcional no Makefile: `make LIBC=upstream` liga programas de
  usuário estaticamente contra o mlibc (`crt1.o` + `libc.a`) em vez da
  `libsagui` + shim POSIX (`userland/mlibc`).

---

## Requisitos

- Meson 1.12+, Ninja, `python -m mesonbuild.mesonmain` / `python -m ninja`.
- Toolchain cruzada i686-elf (GCC >= 14; usada 15.2.0 via
  `tools/windows/download-windows-tools.ps1 -VersaoToolchain 15.2.0 -Forcar`):
  `.local/windows-tools/toolchain/bin/i686-elf-{gcc,g++,ar,nm,strip,objcopy,ld,as}.exe`.
- Git (para clonar o mlibc) e PowerShell (para o script de setup).
- `C:\Program Files\Git\usr\bin` no `PATH` (patch, usado por builds anteriores;
  o setup atual aplica patches pelo PowerShell).

---

## Construção do mlibc

```pwsh
pwsh -ExecutionPolicy Bypass -File tools/mlibc-sagui/setup.ps1
```

O que o script faz, em ordem:

1. Clona o mlibc (`edd69e3`) em `.local/mlibc` se ainda não estiver lá.
2. Aplica os patches do porte SaguiOS (idempotentes):
   - `meson.build`: `elif host_machine.system() == 'sagui'` +
     `subdir('sysdeps/sagui')` (ao lado do bloco `demo`).
   - `options/internal/include/bits/types.h`: `__mlibc_int_fast8`/`__mlibc_uint_fast8`
     do i386 passam de 8 bits para 32 bits (`__mlibc_int32`/`__mlibc_uint32`),
     pois o GCC freestanding define `__INT_FAST8_TYPE__`/`__UINT_FAST8_TYPE__`
     como `int`/`unsigned int` e o `static_assert` de sanity do mlibc exige
     igualdade de tamanho com o tipo do compilador.
   - `options/posix/generic/pthread.cpp`: locais `m_expected`/`rc_expected` dos
     `compare_exchange_*` do `__mlibc_fair_rwlock` passam de `unsigned int` para
     `__mlibc_uint32` (`unsigned long` neste alvo — `atomic<unsigned long>&`
     não aceita `unsigned int&`).
   - `options/internal/include/mlibc/collation.hpp`:
     `CollationPolicy<wchar_t>::getExtra`/`getWeight` retornam
     `frg::span<const wint_t>` (reinterpretando o span de bytes), porque
     `uint32_t`/`int32_t` são `long` enquanto `wint_t` é `unsigned int`.
3. Copia o overlay `tools/mlibc-sagui/sysdeps/sagui/` para
   `.local/mlibc/sysdeps/sagui/` (`meson.build`, `sysdeps.cpp`, `syscall.cpp`,
   `entry.cpp`, `crt1.S`, `include/sagui-abi.h`, `include/mlibc/sysdeps.hpp`).
4. Gera `include/abi-bits/` a partir de `.local/mlibc/abis/linux/` (63 headers;
   symlinks do Windows não sobrevivem à cópia raspa do git).
5. Gera `.local/mlibc-cross.ini` (host_machine `sagui`, i686,
   `-D_GNU_SOURCE -ffreestanding -fno-pic -fno-pie -fno-stack-protector`,
   `-nostdlib -ffreestanding`, `needs_exe_wrapper = true`).
6. `meson setup --cross-file ... -Ddefault_library=static -Dbuild_tests=false
   -Duse_freestnd_hdrs=enabled -Dmlibc_abi=7` + `ninja -k 0`.
7. Copia artefatos para `build/mlibc-upstream/` (`lib/libc.a`,
   `lib/crt1.o`, `include/`).

---

## Sysdeps do SaguiOS (`tools/mlibc-sagui/sysdeps/sagui/`)

O porte implementa os sysdeps do mlibc sobre a ABI do SaguiOS (`int 0x80`,
EAX = número do syscall, EBX/ECX/EDX/ESI/EDI, valor negativo = erro):

| Syscall | EAX | Uso no mlibc |
|---------|-----|--------------|
| `SIS_EXECUTAR` | 1 | `Execve` (ENOSYS por enquanto) |
| `SIS_ESPERAR` | 2 | `Waitpid` |
| `SIS_SAIR` | 3 | `Exit` |
| `SIS_PID` | 4 | `GetPid` (via `this_tid`) |
| `SIS_MATAR` | 5 | `Kill` (ENOSYS se sem alvo) |
| `SIS_ABRIR` | 10 | `Open` |
| `SIS_LER` | 11 | `Read` |
| `SIS_ESCREVER` | 12 | `Write` |
| `SIS_FECHAR` | 13 | `Close` |
| `SIS_POSICIONAR` | 14 | `Seek` |
| `SIS_INFO_ARQUIVO` | 15 | `Stat` |
| `SIS_CRIAR_PASTA` | 20 | `Mkdir` |
| `SIS_REMOVER_PASTA` | 21 | `Rmdir` |
| `SIS_APAGAR` | 22 | `Unlink` |
| `SIS_LISTAR` | 23 | `OpenDir`/`ReadEntries` |
| `SIS_MUDAR_PASTA` | 24 | `Chdir` |
| `SIS_PASTA_ATUAL` | 25 | `GetCwd` |
| `SIS_RENOMEAR` | 26 | `Rename` |
| `SIS_PIPE` | 30 | `Pipe` |
| `SIS_DUPLICAR` | 31 | `Dup`/`Dup2` |
| `SIS_DISPONIVEL` | 32 | `Fcntl` (disponibilidade de leitura) |
| `SIS_DORMIR` | 40 | `Sleep`/`clock_nanosleep` |
| `SIS_TEMPO` | 41 | `ClockGettime` |
| `SIS_DATA` | 42 | (data/hora) |
| `SIS_INFO_SISTEMA` | 43 | `Sysconf` |
| `SIS_AUMENTAR_MEM` | 44 | `AnonAllocate`/`VmMap` |
| `SIS_REGISTRAR` | 48 | log de sistema (`Log`/`Panic`) |
| `SIS_TLS` | 62 | `TcbSet` (GS base) |

Pontos do porte:

- **TLS/TCB**: `TcbSet` escreve a base GS diretamente (i386); canário em
  `%gs:0x14`, magic de debug em `%gs:0x10`.
- **Entrada**: `crt1.S` (`_start`) alinha a pilha e chama
  `__mlibc_entry(&argc, main)`; `entry.cpp` define `main` (weak) chamando
  `principal(argc, argv)` — a convenção de programas do SaguiOS — e chama
  `exit(result)`. O `__dlapi_enter` do rtld estático (`MLIBC_STATIC_BUILD`)
  executa `interpreterMain`, que instala o TCB inicial, o canário de stack e
  parseia o vetor auxiliar da pilha do kernel; `init_libc()` (construtor)
  então preenche `mlibc::entry_stack` (argc/argv/envp).
- **Memória**: `AnonAllocate` via `SIS_AUMENTAR_MEM(0)` com padding de 4096;
  `AnonFree`, `VmUnmap`, `VmProtect` são no-op (sem paginação arbitrária neste
  subset).
- **Descritores de arquivo**: `kFdMax = 16` (igual ao kernel); handles de
  diretório falsificados em 1000..1015.
- **Processos**: `Fork`/`Execve`/`Clone` retornam ENOSYS; `Waitpid` usa
  `SIS_ESPERAR` (morto pelo kernel = -1000 → `SAIDA_ENCERRADO_PELO_KERNEL`).
- **Relógio**: `SIS_TEMPO` (ms desde boot); `SIS_DORMIR`; entropia via
  `AT_RANDOM` quando disponível.

---

## Requisito do kernel: pilha de usuário

O `interpreterMain`/`parse_exec_stack` do mlibc esperam a pilha inicial ELF
padrão:

```
[esp]   = argc
[esp+4] = argv[0] .. argv[argc-1], NULL
        = NULL                 <- envp vazio
        = pares (chave, valor) do vetor auxiliares ... , AT_NULL, 0
        = strings dos argumentos (no topo)
```

O carregador do SaguiOS (`kernel/nucleo/carregador.c`, `montar_pilha`) foi
ajustado para este layout (mantendo `argc` em `[esp]` e `argv` em `[esp+4]`,
compatível com a `inicio.asm` da libsagui existente). Sem o vetor de
argumentos/ambiente/auxiliar no formato acima, o `__dlapi_enter` do mlibc
interpreta a pilha incorretamente.

---

## Usando o mlibc nos programas (`LIBC=upstream`)

No `Makefile`, `make LIBC=upstream` ativa o mlibc upstream para os programas
de usuário:

- Compilação com `i686-elf-gcc` (toolchain cruzada) e headers em
  `build/mlibc-upstream/include` (`-D_GNU_SOURCE`).
- Ligação: `crt1.o` + objeto do programa + `build/mlibc-upstream/lib/libc.a`
  (+ `libgcc.a` para helpers de 64 bits), script
  `userland/programa-upstream.ld` (`ENTRY(_start)` e símbolos
  `__init_array_*`, `__preinit_array_*`, `__ehdr_start` exigidos pelo rtld
  estático do mlibc).
- O programa define `int principal(int argc, char **argv)`; o crt1 do mlibc
  chama `main` (fraco, em `entry.cpp`) que chama `principal`.

Exemplo de programa de teste (`userland/programas/upstream-teste.c`):

```c
#include <unistd.h>
#include <fcntl.h>

int principal(int argc, char **argv)
{
    const char msg[] = "ola, mlibc upstream no SaguiOS!\r\n";
    const char caminho[] = "/bin/upstream-teste";
    char buf[16];
    int fd;
    ssize_t n;

    (void)argc;
    (void)argv;

    write(STDOUT_FILENO, msg, sizeof(msg) - 1);

    fd = open(caminho, O_RDONLY);
    if (fd >= 0) {
        n = read(fd, buf, sizeof(buf));
        (void)n;
        close(fd);
    }

    return 0;
}
```

Ele é copiado para `build/rootfs/bin/` pelo `ramdisk.sgfs` e pode ser
executado no shell do convidado:

```
/bin/upstream-teste
```

Saída esperada na serial:

```
ola, mlibc upstream no SaguiOS!
```

---

## Execução (QEMU / VirtualBox)

A imagem de disco é `build/saguios.img` (ou `.vdi` para o VirtualBox).

### QEMU

```sh
qemu-system-i386 -m 512M -drive format=raw,file=build/saguios.img -serial stdio \
    -netdev user,id=net0 -device e1000,netdev=net0
```

### VirtualBox

```sh
VBoxManage convertfromraw build/saguios.img build/saguios.vdi --format VDI
```

Crie uma VM (tipo "Outro/Outro (32 bits)"), anexe `build/saguios.vdi` como
disco IDE, 512 MB de RAM, rede NAT, e ligue. A saída da serial (COM1) pode ser
capturada com `--uart1 0x3f8 4 --uartmode1 file com1.log`.

---

## Limitações conhecidas deste subset

- Sem threads/futexes (`Futex*` → ENOSYS), sem `mmap`/`munmap`/`mprotect`
  reais (`Vm*` são no-op; `AnonAllocate` cresce o heap via `SIS_AUMENTAR_MEM`).
- `Fork`, `Execve`, `Clone` retornam ENOSYS.
- Symlinks de Windows: `sysdeps/*/include/abi-bits/*.h` não sobrevivem ao clone
  raspo no Windows; são copiados de `abis/linux/` durante o setup.
- O mlibc é recompilado por `tools/mlibc-sagui/setup.ps1`; o Makefile só consome
  os artefatos já prontos em `build/mlibc-upstream/`.
