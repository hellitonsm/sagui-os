# comum.ps1 - funções compartilhadas pelos scripts de Windows do SaguiOS.
#
# Este arquivo é carregado com ". caminho\comum.ps1" pelos outros scripts.
# Nada aqui altera o sistema: só descobre caminhos e lê o marcador deixado pelo
# download-windows-tools.ps1.

function Get-Repositorio {
    return (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
}

function Get-PastaFerramentas {
    return (Join-Path (Get-Repositorio) '.local\windows-tools')
}

function Get-GitRaiz {
    $git = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($git) {
        $cmd = Split-Path -Parent $git.Source
        return (Split-Path -Parent $cmd)
    }
    foreach ($p in @('C:\Program Files\Git', 'C:\Program Files (x86)\Git')) {
        if (Test-Path (Join-Path $p 'usr\bin\bash.exe')) { return $p }
    }
    throw 'Git para Windows não encontrado. Instale em https://git-scm.com/download/win'
}

function Get-VBoxManage {
    $v = Get-Command VBoxManage.exe -ErrorAction SilentlyContinue
    if ($v) { return $v.Source }
    foreach ($p in @(
        'C:\Program Files\Oracle\VirtualBox\VBoxManage.exe',
        'C:\Program Files (x86)\Oracle\VirtualBox\VBoxManage.exe'
    )) {
        if (Test-Path -LiteralPath $p) { return $p }
    }
    return $null
}

function Find-Executavel {
    param([string]$Raiz, [string]$Nome)
    if (-not (Test-Path -LiteralPath $Raiz)) { return $null }
    $f = Get-ChildItem -LiteralPath $Raiz -Filter $Nome -Recurse -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($f) { return $f.FullName }
    return $null
}

function Get-Ferramentas {
    $base = Get-PastaFerramentas
    $marcador = Join-Path $base 'caminhos.json'
    if (-not (Test-Path -LiteralPath $marcador)) { return $null }

    $c = Get-Content -Raw -LiteralPath $marcador | ConvertFrom-Json
    $resolver = {
        param([string]$rel)
        if ([System.IO.Path]::IsPathRooted($rel)) { return $rel }
        return (Join-Path $base $rel)
    }
    $make = & $resolver $c.make
    $nasm = & $resolver $c.nasm
    $crossBin = & $resolver $c.crossBin
    $shims = & $resolver $c.shims
    $gcc = Join-Path $crossBin 'i686-elf-gcc.exe'

    if (-not (Test-Path -LiteralPath $make)) { return $null }
    if (-not (Test-Path -LiteralPath $nasm)) { return $null }
    if (-not (Test-Path -LiteralPath $gcc)) { return $null }

    return [pscustomobject]@{
        Base     = $base
        Make     = $make
        Nasm     = $nasm
        NasmDir  = (Split-Path -Parent $nasm)
        CrossBin = $crossBin
        Shims    = $shims
        Gcc      = $gcc
        Versao   = $c.versao
    }
}
