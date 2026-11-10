# tinycc-sagui - TinyCC vendido e executado dentro do SaguiOS.
#
# O TinyCC (x86/i386) e compilado com a toolchain cruzada i686-elf contra o
# libc nativo do SaguiOS e incluido na imagem em /bin/tinycc, com o sysroot (headers +
# libc.a + crt1.o) em /usr. Um programa em C pode ser compilado DENTRO do
# SaguiOS e executado ali mesmo:
#
#   tinycc -static -o /tmp/ola /tcc/hello.c
#   /tmp/ola
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File tools/tinycc-sagui/setup.ps1

$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$repoRoot  = Resolve-Path (Join-Path $scriptDir '..\..')

$tinyccDir   = Join-Path $repoRoot '.local\tinycc'
$tinyccBuild = Join-Path $repoRoot '.local\tinycc-build'
$tinyccCommit = '70adbb129760b77e3bdfec447c1f986def763f2c'
$tinyccUrl    = 'https://github.com/tinycc/tinycc.git'

if (-not (Test-Path (Join-Path $tinyccDir 'tcc.c'))) {
    Write-Host "== [setup] clonando tinycc em $tinyccDir ..."
    if (Test-Path $tinyccDir) { Remove-Item -Recurse -Force $tinyccDir }
    git clone $tinyccUrl $tinyccDir | Out-Null
    git -C $tinyccDir checkout $tinyccCommit | Out-Null
} else {
    Write-Host "== [setup] tinycc ja presente em $tinyccDir"
}

# O SaguiOS carrega programas em 0x40000000 (ver kernel/inc/processo.h);
# o padrao do tinycc para Linux i386 e 0x08048000, que o carregador rejeita.
$i386Link = Join-Path $tinyccDir 'i386-link.c'
$linkSrc = Get-Content $i386Link -Raw
if ($linkSrc -notmatch '0x40000000') {
    $linkSrc = $linkSrc -replace '#define ELF_START_ADDR 0x08048000', '#define ELF_START_ADDR 0x40000000'
    Set-Content $i386Link $linkSrc -Encoding Ascii -NoNewline
    Write-Host "== [setup] ELF_START_ADDR ajustado para 0x40000000"
}

# Sem -o, o tinycc gravaria "a.out". No SaguiOS o executavel recebe o nome do
# fonte (sem extensao), para o usuario rodar ./exemplo-gui direto. O ajuste e
# feito no fonte vendorizado, como o do ELF_START_ADDR acima.
$tccC = Join-Path $tinyccDir 'tcc.c'
$tccSrc = (Get-Content $tccC -Raw) -replace "`r`n", "`n"
if ($tccSrc -notmatch 'SaguiOS: sem -o') {
    $antigo = @(
        '    if ((s->just_deps || s->output_type == TCC_OUTPUT_OBJ) && !s->option_r && *ext)',
        '        strcpy(ext, ".o");',
        '    else',
        '        strcpy(buf, "a.out");'
    ) -join "`n"
    $novo = @(
        '    if ((s->just_deps || s->output_type == TCC_OUTPUT_OBJ) && !s->option_r && *ext)',
        '        strcpy(ext, ".o");',
        '    else if (s->output_type == TCC_OUTPUT_EXE && first_file && strcmp(first_file, "-"))',
        '        *ext = 0;   /* SaguiOS: sem -o, o executavel tem o nome do fonte */',
        '    else',
        '        strcpy(buf, "a.out");'
    ) -join "`n"
    $tccSrc = $tccSrc.Replace($antigo, $novo)
    Set-Content $tccC $tccSrc -Encoding Ascii -NoNewline
    Write-Host "== [setup] saida padrao do tinycc ajustada (nome do fonte)"
}

# config.h para o alvo Linux i386 (in-process; sem dlopen, sem backtrace).
@"
#define TARGETOS_Linux 1
#define CONFIG_TCCDIR "/usr/lib/tcc"
#define CONFIG_TCC_STATIC 1
#define CONFIG_TCC_BACKTRACE 0
#define CONFIG_TCC_PREDEFS 0
#define CONFIG_TCC_SEMLOCK 0
#define TCC_VERSION "0.9.28rc"
"@ | Set-Content (Join-Path $tinyccDir 'config.h') -Encoding Ascii
Write-Host "== [setup] config.h pronto"
