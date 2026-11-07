# tools/llvm-sagui/toolchain.cmake
#
# Toolchain CMake para cross-compilar o LLVM (modo AOT) mirando o SaguiOS i386.
#
# Uso (no host Linux/macOS/Windows com o cross-compiler i686-elf instalado):
#
#   cmake -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=tools/llvm-sagui/toolchain.cmake \
#     -DCMAKE_BUILD_TYPE=Release \
#     -DLLVM_TARGETS_TO_BUILD="X86" \
#     -DLLVM_ENABLE_THREADS=OFF \
#     -DLLVM_BUILD_TOOLS=ON \
#     -DLLVM_INCLUDE_TESTS=OFF \
#     -DLLVM_INCLUDE_BENCHMARKS=OFF \
#     -DLLVM_ENABLE_BACKTRACES=OFF \
#     ../llvm
#
#   ninja llvm-tblgen   # host tool, necessário primeiro em cross-compilation
#   ninja clang llc opt # ferramentas AOT para o SaguiOS
#
# Requisitos no host:
#   - cross-compiler i686-elf-{gcc,g++,ar,ld,strip,objcopy,as,nm} no PATH
#     (mesma toolchain já usada pelo mlibc do SaguiOS)
#   - CMake 3.20+, Ninja
#   - fontes do LLVM (branch release adequada)

# ---------- máquina alvo ----------
set(CMAKE_SYSTEM_NAME       SaguiOS)
set(CMAKE_SYSTEM_PROCESSOR  i386)

# ---------- compiladores cruzados (ajuste os caminhos se não estiverem no PATH) ----------
set(CMAKE_C_COMPILER   i686-elf-gcc)
set(CMAKE_CXX_COMPILER i686-elf-g++)
set(CMAKE_AR           i686-elf-ar)
set(CMAKE_LINKER       i686-elf-ld)
set(CMAKE_STRIP        i686-elf-strip)
set(CMAKE_OBJCOPY      i686-elf-objcopy)
set(CMAKE_ASM_COMPILER i686-elf-as)
set(CMAKE_NM           i686-elf-nm)

# ---------- sysroot com headers e libc estática do SaguiOS (mlibc upstream) ----------
# Caminho relativo a este arquivo: tools/llvm-sagui/ -> ../../build/mlibc-upstream
set(CMAKE_FIND_ROOT_PATH "${CMAKE_SOURCE_DIR}/../../build/mlibc-upstream")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ---------- flags do alvo freestanding i386 do SaguiOS ----------
set(CMAKE_C_FLAGS_INIT   "-target i386-pc-saguios -ffreestanding -fno-pic -fno-pie -fno-stack-protector -D_GNU_SOURCE")
set(CMAKE_CXX_FLAGS_INIT "-target i386-pc-saguios -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-exceptions -fno-rtti -D_GNU_SOURCE")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib -ffreestanding")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-nostdlib -ffreestanding")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-nostdlib -ffreestanding")

# ---------- cross-compilation: não tenta rodar binários do alvo ----------
set(CMAKE_CROSSCOMPILING ON)

# ---------- avisos úteis ----------
message(STATUS "LLVM toolchain SaguiOS: system=${CMAKE_SYSTEM_NAME} arch=${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "Sysroot: ${CMAKE_FIND_ROOT_PATH}")
