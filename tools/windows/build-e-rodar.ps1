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
            # A saida do programa nao pode se misturar com o codigo devolvido.
            # O ForEach-Object converte os ErrorRecord que o PowerShell cria
            # para o stderr em texto comum; sem isso o stderr do VBoxManage
            # aparece como "NativeCommandError" no console.
            & $Exe @Argumentos 2>&1 | ForEach-Object { "$_" } | Out-Host
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

function Wait-VmRodando {
    param([string]$VBox, [string]$Nome, [int]$Tentativas = 20)
    for ($i = 0; $i -lt $Tentativas; $i++) {
        if (Get-VmRodando $VBox $Nome) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

# Pasta onde o VirtualBox guarda as VMs criadas sem --basefolder. O valor pode
# ser mudado com "VBoxManage setproperty machinefolder", entao perguntamos ao
# proprio VirtualBox em vez de fixar %USERPROFILE%\VirtualBox VMs.
function Get-PastaVmPadrao {
    param([string]$VBox)
    $anterior = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $linhas = & $VBox list systemproperties 2>$null } finally { $ErrorActionPreference = $anterior }
    foreach ($linha in $linhas) {
        if ($linha -match '^Default machine folder:\s+(.+?)\s*$') { return $Matches[1] }
    }
    return (Join-Path $env:USERPROFILE 'VirtualBox VMs')
}

# Uma VM cujo .vbox nao carrega (por exemplo, porque o disco que ela referencia
# nao esta mais registrado) continua aparecendo em "list vms", mas showvminfo
# falha. O nome fica reservado e createvm responde "Machine settings file ...
# already exists". Aqui tiramos essa entrada quebrada do registro e apagamos o
# .vbox antigo; a VM sera recriada do zero logo em seguida.
function Remove-VmQuebrada {
    param([string]$VBox, [string]$Nome)

    # So age quando a VM esta registrada mas nao carrega. Uma VM valida (mesmo
    # desligada) responde ao showvminfo e nunca e tocada por esta funcao.
    if ((Invoke-Nativo $VBox @('showvminfo', $Nome) -Silencioso) -eq 0) { return $false }

    $anterior = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $lista = & $VBox list vms 2>$null } finally { $ErrorActionPreference = $anterior }
    $padrao = '"' + [regex]::Escape($Nome) + '"\s+\{([^}]+)\}'
    $uuid = $null
    foreach ($linha in @($lista)) {
        if ($linha -match $padrao) { $uuid = $Matches[1]; break }
    }
    if (-not $uuid) { return $false }

    # Numa VM inacessivel o unregistervm pode devolver erro ("The object is not
    # ready") e mesmo assim remover a entrada; por isso o codigo nao e checado.
    Invoke-Nativo $VBox @('unregistervm', $uuid) -Silencioso | Out-Null

    $pasta = Join-Path (Get-PastaVmPadrao -VBox $VBox) $Nome
    foreach ($sufixo in @('.vbox', '.vbox-prev')) {
        Remove-Item -Force -LiteralPath (Join-Path $pasta ($Nome + $sufixo)) -ErrorAction SilentlyContinue
    }
    return $true
}

if (Get-VmRodando $vbox $VM) {
    if ($SoRodar) {
        Write-Host "A VM '$VM' já está ligada."
        return
    }
    throw "A VM '$VM' está ligada. Desligue-a antes de rodar este script (VBoxManage controlvm $VM poweroff)."
}

# Devolve o UUID com que um .vdi esta registrado no VirtualBox, ou $null se
# este caminho ainda nao aparece no registro de midias.
function Get-UuidRegistrado {
    param([string]$VBox, [string]$Caminho)
    $anterior = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $linhas = & $VBox list hdds 2>$null } finally { $ErrorActionPreference = $anterior }
    $uuid = $null
    foreach ($linha in $linhas) {
        if ($linha -match '^UUID:\s+(\S+)') { $uuid = $Matches[1] }
        elseif ($linha -match '^Location:\s+(.+?)\s*$' -and $Matches[1] -ieq $Caminho) {
            return $uuid
        }
    }
    return $null
}

# Solta o registro de um .vdi que sera recriado. Enquanto outra VM usar o mesmo
# arquivo (por exemplo uma "sagui-final" criada a mao), o closemedium falha:
# nesse caso o registro antigo continua valendo, e New-Vdi recria o .vdi com o
# mesmo UUID para nao deixa-lo "inaccessible".
function Remove-RegistroVdi {
    param([string]$VBox, [string]$Caminho, [string]$NomeVm)
    Invoke-Nativo $VBox @('storageattach', $NomeVm, '--storagectl', 'IDE Controller',
        '--port', '0', '--device', '0', '--type', 'hdd', '--medium', 'none') -Silencioso | Out-Null
    $uuid = Get-UuidRegistrado -VBox $VBox -Caminho $Caminho
    if ($uuid) {
        Invoke-Nativo $VBox @('closemedium', 'disk', $uuid) -Silencioso | Out-Null
    }
}

# Um .vdi registrado com outro UUID fica "inaccessible" no VirtualBox. Nesse
# caso o arquivo precisa ser refeito mesmo que pareca atualizado.
function Test-MedioInacessivel {
    param([string]$VBox, [string]$Caminho)
    $anterior = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $linhas = & $VBox list hdds 2>$null } finally { $ErrorActionPreference = $anterior }
    $estado = $null
    foreach ($linha in $linhas) {
        if ($linha -match '^State:\s+(\S+)') { $estado = $Matches[1] }
        elseif ($linha -match '^Location:\s+(.+?)\s*$' -and $Matches[1] -ieq $Caminho) {
            return ($estado -ieq 'inaccessible')
        }
    }
    return $false
}

function New-Vdi {
    param([string]$Repo, [string]$VBox)
    $img = Join-Path $Repo 'build\saguios.img'
    $vdi = Join-Path $Repo 'build\saguios.vdi'

    if (-not (Test-Path -LiteralPath $img)) {
        throw 'build\saguios.img não existe; rode sem -SoRodar para compilar primeiro.'
    }
    if ((Test-Path -LiteralPath $vdi) -and
        ((Get-Item -LiteralPath $vdi).LastWriteTime -ge (Get-Item -LiteralPath $img).LastWriteTime) -and
        -not (Test-MedioInacessivel -VBox $VBox -Caminho $vdi)) {
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

    # Reaproveita o UUID do registro atual. Se outra VM usa este .vdi, o
    # VirtualBox nao deixa fechar a midia, e um UUID novo deixaria o disco
    # "inaccessible" (o arquivo nao bate mais com o que esta no registro).
    $uuid = Get-UuidRegistrado -VBox $VBox -Caminho $vdi

    Remove-RegistroVdi -VBox $VBox -Caminho $vdi -NomeVm $VM
    Remove-Item -Force -LiteralPath $vdi -ErrorAction SilentlyContinue
    $argumentos = @('convertfromraw', $raw, $vdi, '--format', 'VDI')
    if ($uuid) { $argumentos += @('--uuid', $uuid) }
    $code = Invoke-Nativo $VBox $argumentos
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
    $code = Invoke-Nativo $vbox @('unregistervm', $VM, '--delete') -Silencioso
    if ($code -ne 0) { Remove-VmQuebrada -VBox $vbox -Nome $VM | Out-Null }
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
    if (Remove-VmQuebrada -VBox $vbox -Nome $VM) {
        Write-Host '  registro antigo/inacessível removido; recriando do zero...'
    }
    Write-Host "  criando a VM '$VM'..."
    $code = Invoke-Nativo $vbox @('createvm', '--name', $VM, '--ostype', 'Other', '--register') -Silencioso
    if ($code -ne 0) { throw "createvm falhou (código $code)" }

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

# Encaminhamento de porta da NAT: a porta 2500 do convidado (o servidor de
# arquivos SAGUI/1, ver kernel/rede/servidor.c e o cliente
# tools/windows/sagui-fs.ps1) fica escutando no host em 127.0.0.1:2500. Só o
# localhost do host pode conectar, nada é exposto para a rede em volta. A regra
# tem nome, então a antiga é removida primeiro (o erro é ignorado quando a
# regra ainda não existe) e reaplicada a cada build.
Invoke-Nativo $vbox @('modifyvm', $VM, '--natpf1', 'delete', 'sagui-arquivos') -Silencioso | Out-Null
$code = Invoke-Nativo $vbox @('modifyvm', $VM, '--natpf1', 'sagui-arquivos,tcp,127.0.0.1,2500,,2500') -Silencioso
if ($code -ne 0) { throw 'não consegui encaminhar a porta 2500 do convidado para o host (NAT)' }

# O .vdi é regenerado a cada build (com outro UUID), então desanexa e anexa de
# novo. Se o VirtualBox ainda estiver com o disco antigo registrado, solta o
# registro (closemedium) e tenta mais uma vez.
Invoke-Nativo $vbox @('storageattach', $VM, '--storagectl', 'IDE Controller', '--port', '0', '--device', '0', '--type', 'hdd', '--medium', 'none') -Silencioso | Out-Null
$code = Invoke-Nativo $vbox @('storageattach', $VM, '--storagectl', 'IDE Controller', '--port', '0', '--device', '0', '--type', 'hdd', '--medium', $vdi) -Silencioso
if ($code -ne 0) {
    Remove-RegistroVdi -VBox $vbox -Caminho $vdi -NomeVm $VM
    $code = Invoke-Nativo $vbox @('storageattach', $VM, '--storagectl', 'IDE Controller', '--port', '0', '--device', '0', '--type', 'hdd', '--medium', $vdi) -Silencioso
    if ($code -ne 0) { throw "não consegui anexar $vdi na VM" }
}

Write-Host ''
Write-Host "VM '$VM' pronta:"
Write-Host '  placa: Intel PRO/1000 MT Desktop (82540EM) em NAT'
Write-Host "  disco: $vdi"
Write-Host '  porta: 127.0.0.1:2500 -> 2500 do convidado (cliente de arquivos)'
if ($SemRodar) {
    Write-Host "Para ligar depois: VBoxManage startvm `"$VM`""
} else {
    Write-Host 'Ligando...'
    $code = Invoke-Nativo $vbox @('startvm', $VM)
    if ($code -ne 0 -and -not (Wait-VmRodando $vbox $VM)) { throw 'startvm falhou' }
}
