# download-windows-tools.ps1 - baixa as ferramentas de build do SaguiOS no Windows.
#
# Uso: powershell -ExecutionPolicy Bypass -File tools\windows\download-windows-tools.ps1
#
# Tudo é gravado em .local\windows-tools\ (ignorado pelo Git):
#   make\make.exe       GNU make para Windows
#   nasm\               NASM (montador)
#   toolchain\          i686-elf-gcc / binutils (GCC 13.2.0 por padrão)
#   shims\              cópias de objcopy/nm com o nome que o Makefile usa
#   downloads\          os .zip baixados (podem ser apagados)
#   caminhos.json       onde cada ferramenta ficou
#
# O ambiente de shell (bash, find, cp, rm, sha256sum, ...) vem do Git para
# Windows, que já precisa estar instalado. O VirtualBox também é usado depois,
# mas não é baixado por este script.
[CmdletBinding()]
param(
    [switch]$Forcar,
    [string]$VersaoToolchain = '13.2.0'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'comum.ps1')

$base = Get-PastaFerramentas
$downloads = Join-Path $base 'downloads'
New-Item -ItemType Directory -Force -Path $base, $downloads | Out-Null

function Get-Relativo {
    param([string]$Caminho)
    return $Caminho.Substring($base.Length).TrimStart('\')
}

function Baixar {
    param([string]$Url, [string]$Destino)
    if ((Test-Path -LiteralPath $Destino) -and -not $Forcar) {
        Write-Host "  já baixado: $(Split-Path -Leaf $Destino)"
        return
    }
    $parcial = "$Destino.parcial"
    if (Test-Path -LiteralPath $parcial) { Remove-Item -Force -LiteralPath $parcial }
    Write-Host "  baixando $Url"
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($curl) {
        & $curl.Source -L --fail --retry 3 --retry-delay 2 -o $parcial $Url
        if ($LASTEXITCODE -ne 0) { throw "falha ao baixar $Url" }
    } else {
        Invoke-WebRequest -Uri $Url -OutFile $parcial -UseBasicParsing
    }
    Move-Item -Force -LiteralPath $parcial -Destination $Destino
}

function Extrair {
    param([string]$Zip, [string]$Destino)
    if (Test-Path -LiteralPath $Destino) { Remove-Item -Recurse -Force -LiteralPath $Destino }
    New-Item -ItemType Directory -Force -Path $Destino | Out-Null

    # tar.exe (bsdtar) lê zip e é bem mais rápido que Expand-Archive em arquivos
    # grandes; se não existir ou falhar, cai para o Expand-Archive do PowerShell.
    $tar = Get-Command tar.exe -ErrorAction SilentlyContinue
    if ($tar) {
        & $tar.Source -xf $Zip -C $Destino
        if ($LASTEXITCODE -eq 0) { return }
        Write-Host '  tar não conseguiu extrair; tentando Expand-Archive...'
        Remove-Item -Recurse -Force -LiteralPath $Destino -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Force -Path $Destino | Out-Null
    }
    Expand-Archive -LiteralPath $Zip -DestinationPath $Destino -Force
}

Write-Host "Ferramentas locais em: $base"

# --- GNU make ---------------------------------------------------------------
$makeDir = Join-Path $base 'make'
New-Item -ItemType Directory -Force -Path $makeDir | Out-Null
$makeExe = Join-Path $makeDir 'make.exe'
Write-Host 'GNU make:'
Baixar -Url 'https://raw.githubusercontent.com/mbuilov/gnumake-windows/master/gnumake-4.4.1-x64.exe' -Destino $makeExe

# --- NASM -------------------------------------------------------------------
Write-Host 'NASM:'
$nasmDir = Join-Path $base 'nasm'
$nasmZip = Join-Path $downloads 'nasm-2.16.03-win64.zip'
Baixar -Url 'https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/win64/nasm-2.16.03-win64.zip' -Destino $nasmZip
if ($Forcar -or -not (Find-Executavel $nasmDir 'nasm.exe')) {
    Write-Host '  extraindo...'
    Extrair -Zip $nasmZip -Destino $nasmDir
}
$nasmExe = Find-Executavel $nasmDir 'nasm.exe'
if (-not $nasmExe) { throw 'nasm.exe não encontrado depois de extrair o NASM' }

# --- toolchain i686-elf -----------------------------------------------------
Write-Host "toolchain i686-elf (GCC $VersaoToolchain):"
$toolDir = Join-Path $base 'toolchain'
$toolZip = Join-Path $downloads "i686-elf-tools-$VersaoToolchain-windows.zip"
Baixar -Url "https://github.com/lordmilko/i686-elf-tools/releases/download/$VersaoToolchain/i686-elf-tools-windows.zip" -Destino $toolZip
$gcc = Find-Executavel $toolDir 'i686-elf-gcc.exe'
if ($Forcar -or -not $gcc) {
    Write-Host '  extraindo (o arquivo é grande; pode demorar alguns minutos)...'
    Extrair -Zip $toolZip -Destino $toolDir
    $gcc = Find-Executavel $toolDir 'i686-elf-gcc.exe'
}
if (-not $gcc) { throw 'i686-elf-gcc.exe não encontrado depois de extrair a toolchain' }
$crossBin = Split-Path -Parent $gcc

# --- atalhos objcopy/nm -----------------------------------------------------
# O Makefile e o mkimagem.py chamam "objcopy" e "nm" (sem o prefixo i686-elf).
# Em vez de alterar o projeto, deixamos cópias com esses nomes num diretório
# que vai na frente do PATH.
Write-Host 'preparando objcopy/nm...'
$shims = Join-Path $base 'shims'
New-Item -ItemType Directory -Force -Path $shims | Out-Null
foreach ($par in @(
    @('i686-elf-objcopy.exe', 'objcopy.exe'),
    @('i686-elf-nm.exe', 'nm.exe')
)) {
    $origem = Join-Path $crossBin $par[0]
    if (-not (Test-Path -LiteralPath $origem)) { throw "não encontrei $($par[0]) na toolchain" }
    Copy-Item -Force -LiteralPath $origem -Destination (Join-Path $shims $par[1])
}

# --- marcador com os caminhos ----------------------------------------------
$caminhos = [ordered]@{
    versao   = $VersaoToolchain
    make     = (Get-Relativo $makeExe)
    nasm     = (Get-Relativo $nasmExe)
    crossBin = (Get-Relativo $crossBin)
    shims    = (Get-Relativo $shims)
}
$caminhos | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $base 'caminhos.json') -Encoding UTF8

Write-Host ''
Write-Host 'Ferramentas prontas:'
Write-Host "  make:      $makeExe"
Write-Host "  nasm:      $nasmExe"
Write-Host "  toolchain: $crossBin"
Write-Host ''
Write-Host 'Agora rode: powershell -ExecutionPolicy Bypass -File tools\windows\build-e-rodar.ps1'
