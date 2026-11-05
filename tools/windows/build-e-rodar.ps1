# build-e-rodar.ps1 - compila o SaguiOS e liga no VirtualBox, tudo no Windows.
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File tools\windows\build-e-rodar.ps1
#   powershell -ExecutionPolicy Bypass -File tools\windows\build-e-rodar.ps1 -SemRodar
#   powershell -ExecutionPolicy Bypass -File tools\windows\build-e-rodar.ps1 -SoRodar
#   powershell -ExecutionPolicy Bypass -File tools\windows\build-e-rodar.ps1 -Apagar
#
# Opções:
#   -SemRodar        compila e prepara a VM, mas não liga
#   -SoRodar         não compila: só reusa build\saguios.vdi e liga
#   -Limpar          apaga build\ antes de compilar
#   -Apagar          remove a VM "saguiOS" (o .vdi do build\ não é apagado)
#   -ForcarDownload  baixa as ferramentas de novo (ver download-windows-tools.ps1)
#
# Na primeira execução, se as ferramentas ainda não estiverem em
# .local\windows-tools\, o download-windows-tools.ps1 é chamado sozinho.
#
# Requisitos: Git para Windows (bash, find, cp, ...) e VirtualBox instalados.
# O compilador cruzado i686-elf, o NASM e o GNU make são baixados para a pasta
# local .local\windows-tools\ (ignorada pelo Git).
[CmdletBinding()]
param(
    [switch]$SemRodar,
    [switch]$SoRodar,
    [switch]$Apagar,
    [switch]$Limpar,
    [switch]$ForcarDownload,
    [string]$VersaoToolchain = '13.2.0'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'comum.ps1')

$VM = 'saguiOS'
$repo = Get-Repositorio

$vbox = Get-VBoxManage
if (-not $vbox) {
    throw 'VBoxManage.exe não encontrado. Instale o VirtualBox: https://www.virtualbox.org/wiki/Downloads'
}

# O VBoxManage escreve avisos em stderr mesmo quando o comando "falha" de forma
# esperada (por exemplo, showvminfo de uma VM que ainda não existe). Com
# $ErrorActionPreference = 'Stop', o PowerShell transformaria isso em exceção
# antes de podermos olhar o $LASTEXITCODE. Este helper roda o programa com
# stderr tratado como texto comum e devolve o código de saída.
function Invoke-Nativo {
    param([string]$Exe, [string[]]$Argumentos, [switch]$Silencioso)
    $anterior = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        if ($Silencioso) {
            & $Exe @Argumentos *> $null
        } else {
            & $Exe @Argumentos
        }
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $anterior
    }
}

function Get-VmRodando {
    param([string]$VBox, [string]$Nome)
    $anterior = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $lista = & $VBox list runningvms 2>$null
        return [bool]($lista -match ('"' + [regex]::Escape($Nome) + '"'))
    } finally {
        $ErrorActionPreference = $anterior
    }
}

if (Get-VmRodando $vbox $VM) {
    if ($SoRodar) {
        Write-Host "A VM '$VM' já está ligada."
        return
    }
    throw "A VM '$VM' está ligada. Desligue-a antes de rodar este script (VBoxManage controlvm $VM poweroff)."
}

function New-Vdi {
    param([string]$Repo, [string]$VBox)
    $img = Join-Path $Repo 'build\saguios.img'
    $vdi = Join-Path $Repo 'build\saguios.vdi'

    if (-not (Test-Path -LiteralPath $img)) {
        throw 'build\saguios.img não existe; rode sem -SoRodar para compilar primeiro.'
    }
    if ((Test-Path -LiteralPath $vdi) -and
        ((Get-Item -LiteralPath $vdi).LastWriteTime -ge (Get-Item -LiteralPath $img).LastWriteTime)) {
        Write-Host "VDI já está atualizado: $vdi"
        return $vdi
    }

    # O make vdi do Linux usa qemu-img; aqui usamos o próprio VBoxManage. Antes,
    # copiamos a imagem para um .raw e completamos até um múltiplo de 1 MB
    # (mesma decisão D004 do Makefile).
    Write-Host 'Gerando build\saguios.vdi ...'
    $raw = Join-Path $Repo 'build\vdi.raw'
    Copy-Item -Force -LiteralPath $img -Destination $raw

    $tamMB = 1MB
    $len = (Get-Item -LiteralPath $raw).Length
    $falta = ($tamMB - ($len % $tamMB)) % $tamMB
    if ($falta -gt 0) {
        $zeros = New-Object byte[] ([int]$falta)
        $fs = [System.IO.File]::Open($raw, [System.IO.FileMode]::Append)
        try { $fs.Write($zeros, 0, $zeros.Length) } finally { $fs.Dispose() }
    }

    Remove-Item -Force -LiteralPath $vdi -ErrorAction SilentlyContinue
    $code = Invoke-Nativo $VBox @('convertfromraw', $raw, $vdi, '--format', 'VDI')
    Remove-Item -Force -LiteralPath $raw -ErrorAction SilentlyContinue
    if ($code -ne 0 -or -not (Test-Path -LiteralPath $vdi)) {
        throw "VBoxManage convertfromraw falhou (código $code)"
    }

    $hash = (Get-FileHash -LiteralPath $vdi -Algorithm SHA256).Hash.ToLower()
    Set-Content -LiteralPath (Join-Path $Repo 'build\saguios.vdi.sha256') -Value "$hash  saguios.vdi" -Encoding Ascii
    return $vdi
}

if ($Apagar) {
    Write-Host "Apagando a VM '$VM' (se existir)..."
    # Sem desanexar, "unregistervm --delete" apaga também o .vdi do build\.
    Invoke-Nativo $vbox @('storageattach', $VM, '--storagectl', 'IDE Controller', '--port', '0', '--device', '0', '--type', 'hdd', '--medium', 'none') -Silencioso | Out-Null
    Invoke-Nativo $vbox @('unregistervm', $VM, '--delete') -Silencioso | Out-Null
    Write-Host 'Pronto. O disco em build\saguios.vdi continua no lugar.'
    return
}

# --- compilação -------------------------------------------------------------
if (-not $SoRodar) {
    $ferr = Get-Ferramentas
    if (-not $ferr) {
        Write-Host 'Ferramentas de build não encontradas; baixando para .local\windows-tools ...'
        & (Join-Path $PSScriptRoot 'download-windows-tools.ps1') -VersaoToolchain $VersaoToolchain -Forcar:$ForcarDownload
        $ferr = Get-Ferramentas
    }
    if (-not $ferr) { throw 'não consegui preparar as ferramentas de build' }

    # O Makefile é escrito para um shell Unix. O Git para Windows traz bash,
    # find, cp, rm, sha256sum e cia.; o make e o compilador cruzado vêm da
    # pasta local. A ordem importa: os "shims" e a toolchain precisam vir antes
    # do System32 (senão "find"/"sort" casam com os programas do Windows).
    $gitRaiz = Get-GitRaiz
    $env:PATH = @(
        $ferr.Shims,
        $ferr.CrossBin,
        $ferr.NasmDir,
        (Split-Path -Parent $ferr.Make),
        (Join-Path $gitRaiz 'usr\bin'),
        (Join-Path $gitRaiz 'bin'),
        $env:PATH
    ) -join ';'

    # mkfonte.py grava o C com open(..., "w"); no Windows o padrão é ANSI e o
    # comentário "Não editar" quebraria o -finput-charset=UTF-8. O modo UTF-8 do
    # Python faz o padrão ser UTF-8, igual ao Linux.
    $env:PYTHONUTF8 = '1'
    $env:PYTHONIOENCODING = 'utf-8'

    if ($Limpar) {
        $build = Join-Path $repo 'build'
        if (Test-Path -LiteralPath $build) { Remove-Item -Recurse -Force -LiteralPath $build }
    }

    Write-Host 'Compilando (make)...'
    Push-Location $repo
    try {
        # O CC é um wrapper em Python: a toolchain i686-elf para Windows foi
        # compilada sem libiconv, então -fexec-charset=ISO-8859-1 não funciona.
        # O wrapper converte os literais para escapes octais Latin-1 antes de
        # chamar o i686-elf-gcc de verdade (mesmos bytes do build no Linux).
        & $ferr.Make 'CC=python tools/windows/cc-latin1.py' LD=i686-elf-ld AS=nasm PY=python
        $code = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($code -ne 0) { throw "make falhou (código $code)" }
}

# --- imagem e VDI -----------------------------------------------------------
$img = Join-Path $repo 'build\saguios.img'
if ($SoRodar) {
    $vdi = Join-Path $repo 'build\saguios.vdi'
    if (-not (Test-Path -LiteralPath $vdi)) { $vdi = New-Vdi -Repo $repo -VBox $vbox }
} else {
    if (-not (Test-Path -LiteralPath $img)) { throw 'a compilação não gerou build\saguios.img' }
    $vdi = New-Vdi -Repo $repo -VBox $vbox
}

# --- máquina virtual --------------------------------------------------------
Write-Host "Configurando a VM '$VM' ..."
$code = Invoke-Nativo $vbox @('showvminfo', $VM) -Silencioso
if ($code -ne 0) {
    Write-Host "  criando a VM '$VM'..."
    $code = Invoke-Nativo $vbox @('createvm', '--name', $VM, '--ostype', 'Other', '--register') -Silencioso
    if ($code -ne 0) { throw 'createvm falhou' }

    # 512 MB, BIOS tradicional (o SaguiOS não usa EFI), mouse/teclado PS/2
    # (é o que o sistema lê) e 16 MB de vídeo para o VESA 1024x768x32.
    $code = Invoke-Nativo $vbox @(
        'modifyvm', $VM, '--memory', '512', '--cpus', '1', '--firmware', 'bios', '--vram', '16',
        '--mouse', 'ps2', '--keyboard', 'ps2', '--audio', 'none',
        '--boot1', 'disk', '--boot2', 'none', '--boot3', 'none', '--boot4', 'none'
    ) -Silencioso
    if ($code -ne 0) { throw 'modifyvm (configuração básica) falhou' }

    $code = Invoke-Nativo $vbox @('storagectl', $VM, '--name', 'IDE Controller', '--add', 'ide', '--controller', 'ICH6') -Silencioso
    if ($code -ne 0) { throw 'storagectl falhou' }
}

# A rede é reaplicada sempre: o nome da opção muda entre versões do VirtualBox
# (--nic-type1, --nictype1 e --nic-type 1=...). Para na primeira que aceitar.
$nicOk = $false
if ((Invoke-Nativo $vbox @('modifyvm', $VM, '--nic1', 'nat', '--nic-type1', '82540EM') -Silencioso) -eq 0) { $nicOk = $true }
if (-not $nicOk) {
    if ((Invoke-Nativo $vbox @('modifyvm', $VM, '--nic1', 'nat', '--nictype1', '82540EM') -Silencioso) -eq 0) { $nicOk = $true }
}
if (-not $nicOk) {
    if ((Invoke-Nativo $vbox @('modifyvm', $VM, '--nic', '1=nat', '--nic-type', '1=82540EM') -Silencioso) -eq 0) { $nicOk = $true }
}
if (-not $nicOk) {
    throw 'não consegui configurar a placa de rede (Intel PRO/1000 MT Desktop, NAT). Configure à mão em Configurações -> Rede.'
}
Invoke-Nativo $vbox @('modifyvm', $VM, '--nic-promisc1', 'deny', '--macaddress1', 'auto') -Silencioso | Out-Null
$code = Invoke-Nativo $vbox @('modifyvm', $VM, '--cable-connected1', 'on') -Silencioso
if ($code -ne 0) { Invoke-Nativo $vbox @('modifyvm', $VM, '--cableconnected1', 'on') -Silencioso | Out-Null }

# O .vdi é regenerado a cada build (com outro UUID), então desanexa e anexa de
# novo. Se o VirtualBox ainda estiver com o disco antigo registrado, solta o
# registro (closemedium) e tenta mais uma vez.
Invoke-Nativo $vbox @('storageattach', $VM, '--storagectl', 'IDE Controller', '--port', '0', '--device', '0', '--type', 'hdd', '--medium', 'none') -Silencioso | Out-Null
$code = Invoke-Nativo $vbox @('storageattach', $VM, '--storagectl', 'IDE Controller', '--port', '0', '--device', '0', '--type', 'hdd', '--medium', $vdi) -Silencioso
if ($code -ne 0) {
    Invoke-Nativo $vbox @('closemedium', 'disk', $vdi) -Silencioso | Out-Null
    $code = Invoke-Nativo $vbox @('storageattach', $VM, '--storagectl', 'IDE Controller', '--port', '0', '--device', '0', '--type', 'hdd', '--medium', $vdi) -Silencioso
    if ($code -ne 0) { throw "não consegui anexar $vdi na VM" }
}

Write-Host ''
Write-Host "VM '$VM' pronta:"
Write-Host '  placa: Intel PRO/1000 MT Desktop (82540EM) em NAT'
Write-Host "  disco: $vdi"
if ($SemRodar) {
    Write-Host "Para ligar depois: VBoxManage startvm `"$VM`""
} else {
    Write-Host 'Ligando...'
    $code = Invoke-Nativo $vbox @('startvm', $VM)
    if ($code -ne 0) { throw 'startvm falhou' }
}
