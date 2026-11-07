# setup.ps1 - constroi o mlibc upstream vendorizado para o SaguiOS (i386).
#
# O que faz:
#   1. Clona o mlibc (commit edd69e3) em .local/mlibc se ainda nao estiver presente.
#   2. Aplica os patches do porte SaguiOS em .local/mlibc (meson.build, types.h,
#      pthread.cpp, collation.hpp) - idempotente.
#   3. Copia o overlay do porte (sysdeps/sagui/) e os headers abi-bits.
#   4. Gera .local/mlibc-cross.ini (host_machine 'sagui', i686, toolchain GCC 15.2).
#   5. Roda 'meson setup' + 'ninja -k 0' em .local/mlibc-build.
#   6. Copia os artefatos (libc.a, crt1.o, headers) para build/mlibc-upstream/.
#
# Requisitos: git, meson (python -m mesonbuild.mesonmain), ninja e a toolchain
# i686-elf em .local/windows-tools/toolchain/bin (GCC >= 14, de preferencia 15.2.0).
#
# Uso: pwsh -ExecutionPolicy Bypass -File tools/mlibc-sagui/setup.ps1

$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$repoRoot  = Resolve-Path (Join-Path $scriptDir '..\..')

$mlibcDir      = Join-Path $repoRoot '.local/mlibc'
$mlibcBuildDir = Join-Path $repoRoot '.local/mlibc-build'
$crossIni      = Join-Path $repoRoot '.local/mlibc-cross.ini'
$toolchainBin  = Join-Path $repoRoot '.local/windows-tools/toolchain/bin'
$upstreamDir   = Join-Path $repoRoot 'build/mlibc-upstream'

$mlibcCommit = 'edd69e3cbd17a0c9731aa9da823c7263423abfe6'
$mlibcUrl    = 'https://github.com/mlibc/mlibc.git'

$gcc  = (Join-Path $toolchainBin 'i686-elf-gcc.exe') -replace '\\','/'
$gxx  = (Join-Path $toolchainBin 'i686-elf-g++.exe') -replace '\\','/'
$ar   = (Join-Path $toolchainBin 'i686-elf-ar.exe')   -replace '\\','/'
$st   = (Join-Path $toolchainBin 'i686-elf-strip.exe') -replace '\\','/'
$nm   = (Join-Path $toolchainBin 'i686-elf-nm.exe')    -replace '\\','/'
$oc   = (Join-Path $toolchainBin 'i686-elf-objcopy.exe') -replace '\\','/'
$ld   = (Join-Path $toolchainBin 'i686-elf-ld.exe')    -replace '\\','/'
$as   = (Join-Path $toolchainBin 'i686-elf-as.exe')    -replace '\\','/'

$python = 'python'
$mesonArgs = @(
    '-m', 'mesonbuild.mesonmain',
    "--cross-file", $crossIni,
    "-Ddefault_library=static",
    "-Dbuild_tests=false",
    "-Duse_freestnd_hdrs=enabled",
    "-Dmlibc_abi=7",
    $mlibcBuildDir
)
$ninjaArgs = @(
    '-m', 'ninja',
    "-C", $mlibcBuildDir, "-k", "0"
)

Write-Host "== [setup] raiz do repositorio: $repoRoot" -ForegroundColor Cyan

# ---------- 1. clona o mlibc (se necessario) ----------
if (-not (Test-Path (Join-Path $mlibcDir 'meson.build'))) {
    Write-Host "== [setup] clonando mlibc em $mlibcDir ..." -ForegroundColor Cyan
    if (Test-Path $mlibcDir) { Remove-Item -Recurse -Force $mlibcDir }
    git clone --depth 1 $mlibcUrl $mlibcDir | Out-Null
    git -C $mlibcDir fetch --depth 1 origin $mlibcCommit | Out-Null
    git -C $mlibcDir checkout FETCH_HEAD | Out-Null
    Write-Host "== [setup] mlibc pronto em $mlibcDir" -ForegroundColor Green
} else {
    Write-Host "== [setup] mlibc ja presente em $mlibcDir" -ForegroundColor Green
}

# ---------- 2. patches do porte SaguiOS em .local/mlibc (idempotentes) ----------

function Normalize-Content {
    param([string] $Path)
    # .local/mlibc e vendorizado/gitignorado: normalizar CRLF -> LF e seguro.
    (Get-Content -Path $Path -Raw) -replace "`r`n", "`n"
}

function Apply-BlockPatch {
    param([string] $Path, [string] $OldBlock, [string] $NewBlock, [string] $Check, [string] $What)
    $content = Normalize-Content $Path
    if ($content -like "*$Check*") {
        Write-Host "   ja aplicado: $What"
        return
    }
    $pattern = [regex]::Escape($OldBlock)
    $replacement = [regex]::Escape($NewBlock)
    $novo = [regex]::Replace($content, $pattern, $replacement)
    if ($novo -eq $content) {
        Write-Warning "   patch NAO aplicado (bloco nao encontrado): $What"
        return
    }
    Set-Content -Path $Path -Value $novo -NoNewline
    Write-Host "   patch aplicado: $What"
}

function Apply-LinePatch {
    param([string] $Path, [string] $OldLine, [string] $NewLine, [string] $Check, [string] $What, [switch] $All)
    $content = Normalize-Content $Path
    if ($content -like "*$Check*") {
        Write-Host "   ja aplicado: $What"
        return
    }
    if ($All) {
        $novo = [regex]::Replace($content, [regex]::Escape($OldLine), [regex]::Escape($NewLine))
    } else {
        $novo = [regex]::Replace($content, [regex]::Escape($OldLine), [regex]::Escape($NewLine), 1)
    }
    if ($novo -eq $content) {
        Write-Warning "   patch NAO aplicado (linha nao encontrada): $What"
        return
    }
    Set-Content -Path $Path -Value $novo -NoNewline
    Write-Host "   patch aplicado: $What"
}

$mesonBuild = Join-Path $mlibcDir 'meson.build'
Apply-BlockPatch -Path $mesonBuild `
    -OldBlock "# ANCHOR_END: demo-sysdeps`n" `
    -NewBlock "# ANCHOR_END: demo-sysdeps`nelif host_machine.system() == 'sagui'`n`tsubdir('sysdeps/sagui')`n" `
    -Check "subdir('sysdeps/sagui')" `
    -What 'meson.build: sysdeps/sagui'

$typesH = Join-Path $mlibcDir 'options/internal/include/bits/types.h'
# No clone upstream o i386 usa __mlibc_int8/__mlibc_uint8 para fast8; o GCC do
# SaguiOS define __INT_FAST8_TYPE__/__UINT_FAST8_TYPE__ como int/unsigned int,
# então o static_assert de sanity so passa com tipos de 32 bits.
$comment = "/* SaguiOS: o GCC define __INT_FAST8_TYPE__ como int; usamos o mesmo tipo`n * para satisfazer a verificacao de sanity do mlibc. */"
Apply-LinePatch -Path $typesH `
    -OldLine "typedef __mlibc_int8 __mlibc_int_fast8;" `
    -NewLine "$comment`ntypedef __mlibc_int32 __mlibc_int_fast8;" `
    -Check "__mlibc_int32 __mlibc_int_fast8;" `
    -What 'types.h: int_fast8 i386'
Apply-LinePatch -Path $typesH `
    -OldLine "typedef __mlibc_uint8 __mlibc_uint_fast8;" `
    -NewLine "/* SaguiOS: o GCC define __UINT_FAST8_TYPE__ como unsigned int. */`ntypedef __mlibc_uint32 __mlibc_uint_fast8;" `
    -Check "__mlibc_uint32 __mlibc_uint_fast8;" `
    -What 'types.h: uint_fast8 i386'

$pthreadCpp = Join-Path $mlibcDir 'options/posix/generic/pthread.cpp'
Apply-LinePatch -Path $pthreadCpp `
    -OldLine "unsigned int m_expected = l->__mlibc_m.load(std::memory_order_relaxed);" `
    -NewLine "__mlibc_uint32 m_expected = l->__mlibc_m.load(std::memory_order_relaxed);" `
    -Check "__mlibc_uint32 m_expected" `
    -What 'pthread.cpp: m_expected' -All
Apply-LinePatch -Path $pthreadCpp `
    -OldLine "unsigned int desired = m_expected | __mlibc_fair_rwlock::mutex_waiters_bit;" `
    -NewLine "__mlibc_uint32 desired = m_expected | __mlibc_fair_rwlock::mutex_waiters_bit;" `
    -Check "__mlibc_uint32 desired" `
    -What 'pthread.cpp: desired (m)'
Apply-LinePatch -Path $pthreadCpp `
    -OldLine "unsigned int desired = 1;" `
    -NewLine "__mlibc_uint32 desired = 1;" `
    -Check "__mlibc_uint32 desired = 1;" `
    -What 'pthread.cpp: desired = 1 (trylock)' -All
Apply-LinePatch -Path $pthreadCpp `
    -OldLine "unsigned int rc_expected = l->__mlibc_rc.load(std::memory_order_acquire);" `
    -NewLine "__mlibc_uint32 rc_expected = l->__mlibc_rc.load(std::memory_order_acquire);" `
    -Check "__mlibc_uint32 rc_expected = l->__mlibc_rc.load(std::memory_order_acquire);" `
    -What 'pthread.cpp: rc_expected (acquire)' -All
Apply-LinePatch -Path $pthreadCpp `
    -OldLine "unsigned int rc_expected = l->__mlibc_rc.load(std::memory_order_relaxed);" `
    -NewLine "__mlibc_uint32 rc_expected = l->__mlibc_rc.load(std::memory_order_relaxed);" `
    -Check "__mlibc_uint32 rc_expected = l->__mlibc_rc.load(std::memory_order_relaxed);" `
    -What 'pthread.cpp: rc_expected (relaxed)'
Apply-LinePatch -Path $pthreadCpp `
    -OldLine "unsigned int desired = rc_expected | __mlibc_fair_rwlock::rc_waiters_bit;" `
    -NewLine "__mlibc_uint32 desired = rc_expected | __mlibc_fair_rwlock::rc_waiters_bit;" `
    -Check "__mlibc_uint32 desired = rc_expected" `
    -What 'pthread.cpp: desired (rc)' -All

$coll = Join-Path $mlibcDir 'options/internal/include/mlibc/collation.hpp'
$oldColl = @"
	static auto getWeight(const mlibc::localeinfo *l) {
		if constexpr (std::is_signed_v<wint_t>) {
			return l->collate.get(_NL_COLLATE_WEIGHTWC).asInt32Span();
		} else {
			return l->collate.get(_NL_COLLATE_WEIGHTWC).asUint32Span();
		}
	}

	static auto getExtra(const mlibc::localeinfo *l) {
		if constexpr (std::is_signed_v<wint_t>) {
			return l->collate.get(_NL_COLLATE_EXTRAWC).asInt32Span();
		} else {
			return l->collate.get(_NL_COLLATE_EXTRAWC).asUint32Span();
		}
	}
"@
$newColl = @"
	// Nota: neste alvo (i386 freestanding GCC 15), uint32_t/int32_t sao `long'
	// enquanto wint_t e 'unsigned int' - mesmo tamanho, tipos distintos. Os
	// spans de colacao sao apenas vistas de bytes; reinterpretamos para o tipo
	// do elemento da colacao (wint_t) para bater com coll_context<wchar_t>.
	static frg::span<const wint_t> getWeight(const mlibc::localeinfo *l) {
		if constexpr (std::is_signed_v<wint_t>) {
			auto s = l->collate.get(_NL_COLLATE_WEIGHTWC).asInt32Span();
			return frg::span<const wint_t>{reinterpret_cast<const wint_t *>(s.data()), s.size()};
		} else {
			auto s = l->collate.get(_NL_COLLATE_WEIGHTWC).asUint32Span();
			return frg::span<const wint_t>{reinterpret_cast<const wint_t *>(s.data()), s.size()};
		}
	}

	static frg::span<const wint_t> getExtra(const mlibc::localeinfo *l) {
		if constexpr (std::is_signed_v<wint_t>) {
			auto s = l->collate.get(_NL_COLLATE_EXTRAWC).asInt32Span();
			return frg::span<const wint_t>{reinterpret_cast<const wint_t *>(s.data()), s.size()};
		} else {
			auto s = l->collate.get(_NL_COLLATE_EXTRAWC).asUint32Span();
			return frg::span<const wint_t>{reinterpret_cast<const wint_t *>(s.data()), s.size()};
		}
	}
"@
Apply-BlockPatch -Path $coll -OldBlock $oldColl -NewBlock $newColl -Check 'frg::span<const wint_t> getWeight' -What 'collation.hpp: spans wint_t'

# ---------- 3. copia o overlay do porte sagui ----------
$overlay = Join-Path $scriptDir 'sysdeps/sagui'
$destSys = Join-Path $mlibcDir 'sysdeps/sagui'
if (Test-Path $destSys) { Remove-Item -Recurse -Force $destSys }
Copy-Item -Path $overlay -Destination $destSys -Recurse -Force
Write-Host "== [setup] overlay sysdeps/sagui copiado" -ForegroundColor Green

# ---------- 4. abi-bits: headers de abis/linux (Windows nao preserva symlinks) ----------
$abiBitsDest = Join-Path $destSys 'include/abi-bits'
if (-not (Test-Path $abiBitsDest)) { New-Item -ItemType Directory -Path $abiBitsDest | Out-Null }
$abiSrc = Join-Path $mlibcDir 'abis/linux'
Get-ChildItem -Path $abiSrc -Filter '*.h' -File | ForEach-Object {
    Copy-Item -Path $_.FullName -Destination (Join-Path $abiBitsDest $_.Name) -Force
}
Write-Host "== [setup] abi-bits copiados de abis/linux" -ForegroundColor Green

# ---------- 5. cross file do mlibc ----------
$crossContent = @"
[binaries]
c = '$gcc'
cpp = '$gxx'
ar = '$ar'
strip = '$st'
nm = '$nm'
objcopy = '$oc'
ld = '$ld'
as = '$as'

[host_machine]
system = 'sagui'
cpu_family = 'x86'
cpu = 'i686'
endian = 'little'

[built-in options]
c_args = ['-D_GNU_SOURCE', '-ffreestanding', '-fno-pic', '-fno-pie', '-fno-stack-protector', '-Wno-attributes', '-Wno-builtin-declaration-mismatch']
cpp_args = ['-D_GNU_SOURCE', '-ffreestanding', '-fno-pic', '-fno-pie', '-fno-stack-protector', '-fno-exceptions', '-fno-rtti', '-Wno-attributes', '-Wno-builtin-declaration-mismatch']
c_link_args = ['-nostdlib', '-ffreestanding']
cpp_link_args = ['-nostdlib', '-ffreestanding']
b_staticpic = false
b_lto = false

[properties]
needs_exe_wrapper = true
"@
# UTF-8 SEM BOM (o meson rejeita BOM no arquivo cross)
[System.IO.File]::WriteAllText($crossIni, $crossContent, (New-Object System.Text.UTF8Encoding $false))
Write-Host "== [setup] cross file gerado: $crossIni" -ForegroundColor Green

# ---------- 6. meson setup + ninja ----------
$mSrc = $mlibcDir -replace '\\','/'
$mBld = $mlibcBuildDir -replace '\\','/'
$cIni = $crossIni -replace '\\','/'
$mesonCmd = "python -m mesonbuild.mesonmain --cross-file `"$cIni`" -Ddefault_library=static `n" +
    "-Dbuild_tests=false -Duse_freestnd_hdrs=enabled -Dmlibc_abi=7 `"$mSrc`" `"$mBld`""
Write-Host "== [setup] meson setup ..." -ForegroundColor Cyan
cmd /c "$(($mesonCmd -replace "`n"," "))" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "meson setup falhou" }

$ninjaCmd = "python -m ninja -C `"$mBld`" -k 0"
Write-Host "== [setup] ninja -k 0 ..." -ForegroundColor Cyan
cmd /c "$ninjaCmd" | Out-Null
if ($LASTEXITCODE -ne 0) { throw "ninja falhou" }
Write-Host "== [setup] mlibc construido com sucesso" -ForegroundColor Green

# ---------- 7. copia artefatos para build/mlibc-upstream/ ----------
if (Test-Path $upstreamDir) { Remove-Item -Recurse -Force $upstreamDir }
New-Item -ItemType Directory -Path (Join-Path $upstreamDir 'lib') | Out-Null
New-Item -ItemType Directory -Path (Join-Path $upstreamDir 'include') | Out-Null

Copy-Item -Path (Join-Path $mlibcBuildDir 'libc.a') -Destination (Join-Path $upstreamDir 'lib/libc.a') -Force
Copy-Item -Path (Join-Path $mlibcBuildDir 'sysdeps/sagui/crt1.o') -Destination (Join-Path $upstreamDir 'lib/crt1.o') -Force

Copy-Item -Path (Join-Path $mlibcBuildDir 'mlibc-config.h') -Destination (Join-Path $upstreamDir 'include/mlibc-config.h') -Force
Copy-Item -Path (Join-Path $mlibcBuildDir 'internal-config.h') -Destination (Join-Path $upstreamDir 'include/internal-config.h') -Force

# headers em include/ (mesclados; internal/include por ultimo para que o
# bits/types.h do porte - ja patchado - venha por cima de qualquer copia)
$includeDirs = @(
    'subprojects/freestanding-c-hdrs/include',
    'subprojects/freestanding-cxx-hdrs/include',
    'subprojects/frigg/include',
    'options/elf/include',
    'options/posix/include',
    'options/ansi/include',
    'options/internal/include',
    # o alvo sagui expõe abi-bits (headers de ABI copiados de abis/linux)
    'sysdeps/sagui/include'
)
$includeDest = Join-Path $upstreamDir 'include'
foreach ($rel in $includeDirs) {
    $src = Join-Path $mlibcDir $rel
    if (Test-Path $src) {
        Copy-Item -Path (Join-Path $src '*') -Destination $includeDest -Recurse -Force
    }
}

Write-Host "== [setup] artefatos em $upstreamDir" -ForegroundColor Green
Write-Host "== [setup] concluido!" -ForegroundColor Green
