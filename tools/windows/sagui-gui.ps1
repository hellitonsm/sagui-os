# sagui-gui.ps1 - janela de arquivos do SaguiOS, rodando no Windows.
#
# A mesma conversa do sagui-fs.ps1 (protocolo SAGUI/1, porta 2500), agora
# como uma janela: navegar pelas pastas, baixar, enviar (inclusive
# arrastando do Explorer), criar pasta, renomear, apagar e remover pasta.
# O servidor continua sendo o comando "servidor" no SaguiOS, e o caminho
# ateh ele eh o encaminhamento da NAT (regra sagui-arquivos,
# 127.0.0.1:2500, aplicada pelos scripts que criam a VM).
#
# Uso:
#   powershell -ExecutionPolicy Bypass -File tools\windows\sagui-gui.ps1
#   powershell -ExecutionPolicy Bypass -File tools\windows\sagui-gui.ps1 -Maquina 127.0.0.1 -Porta 2500
#
# O botao "Ligar servidor" liga a VM do VirtualBox (padrao "saguiOS", mude
# com -Vm), espera a area de trabalho, abre o Terminal, digita "servidor" e
# confirma a porta 2500. Ele usa a COM API do VirtualBox para mover o mouse
# e teclar dentro do convidado; a VM precisa ser a local (127.0.0.1).
#
# A CLI sagui-fs.ps1 continua existindo para scripts e testes. Esta janela
# repete as funcoes do protocolo de proposito: eh um arquivo so, para
# copiar e rodar sem depender de outro script.
#
# Modos de validacao:
#   -Autoteste       faz LIST/INFO/GET/PUT/pastas/erro sem abrir a janela
#   -TesteJanela     so constroi a janela e sai (confere o layout)
#   -AutotesteJanela constroi a janela, conecta e navega de verdade,
#                    sem abrir dialogos (precisa do servidor ligado)
#   -AutotesteLigar  constroi a janela, liga a VM e o servidor, e sai

param(
    [string]$Maquina = '127.0.0.1',
    [int]$Porta = 2500,
    [string]$Vm = 'saguiOS',
    [switch]$Autoteste,
    [switch]$TesteJanela,
    [switch]$AutotesteJanela,
    [switch]$AutotesteLigar
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName Microsoft.VisualBasic

$script:Maquina = $Maquina
$script:Porta = $Porta
$script:Vm = $Vm
$script:vbox = $null
$script:caminho = '/'
$script:entradas = @()
$script:conectado = $false
$script:ocupado = $false
$script:falhas = 0
$script:semCaixas = $false
$script:ultimaPasta = [Environment]::GetFolderPath('MyDocuments')

$Encoding = [System.Text.Encoding]::GetEncoding(28591)   # Latin-1, o idioma do SaguiOS

# ------------------------------------------------------------------ protocolo

function Ler-Linha {
    param($Fluxo)
    $bytes = New-Object System.Collections.Generic.List[byte]
    for (;;) {
        try { $k = $Fluxo.ReadByte() }
        catch { throw 'a conexão caiu (o servidor saiu? o convidado reiniciou?)' }
        if ($k -lt 0) { throw 'a conexão caiu no meio da resposta' }
        if ($k -eq 10) { break }
        [void]$bytes.Add([byte]$k)
    }
    return $Encoding.GetString($bytes.ToArray())
}

function Mandar-Linha {
    param($Fluxo, [string]$Texto)
    $b = $Encoding.GetBytes($Texto + "`n")
    $Fluxo.Write($b, 0, $b.Length)
    $Fluxo.Flush()
}

function Esperar {
    param($Fluxo, [string]$O_que)
    $linha = Ler-Linha $Fluxo
    if ($linha.StartsWith('ERRO ')) { throw "o SaguiOS devolveu: $linha" }
    if (-not ($linha -match $O_que)) { throw "resposta inesperada (esperava $O_que): $linha" }
    return $Matches
}

function Sair-Do-Servidor {
    param($Fluxo)
    try {
        Mandar-Linha $Fluxo 'SAIR'
        $null = Ler-Linha $Fluxo        # TCHAU
    } catch { }
}

function Nova-Conexao {
    param([string]$Alvo, [int]$PortaAlvo, [int]$TempoMs = 20000)
    $cliente = New-Object System.Net.Sockets.TcpClient
    $cliente.ReceiveTimeout = $TempoMs
    $cliente.SendTimeout = $TempoMs
    try { $cliente.Connect($Alvo, $PortaAlvo) }
    catch {
        $cliente.Dispose()
        throw "não conectou em ${Alvo}:${PortaAlvo}. O servidor está ligado no SaguiOS? (comando servidor)"
    }
    $fluxo = $cliente.GetStream()
    try { $banner = Ler-Linha $fluxo }
    catch {
        $fluxo.Dispose(); $cliente.Dispose()
        throw 'a conexão caiu logo no começo (o servidor saiu?)'
    }
    if ($banner -ne 'SAGUI/1') {
        $fluxo.Dispose(); $cliente.Dispose()
        throw "resposta inesperada na porta ${PortaAlvo}: '$banner' (não é o servidor de arquivos?)"
    }
    return [pscustomobject]@{ Cliente = $cliente; Fluxo = $fluxo }
}

function Com-Servidor {
    param([scriptblock]$Acao)
    $c = Nova-Conexao $script:Maquina $script:Porta
    try { & $Acao $c.Fluxo }
    finally {
        Sair-Do-Servidor $c.Fluxo
        $c.Fluxo.Dispose()
        $c.Cliente.Dispose()
    }
}

function Get-Lista {
    param([string]$Caminho)
    Com-Servidor {
        param($Fluxo)
        Mandar-Linha $Fluxo "LIST $Caminho"
        $m = Esperar $Fluxo '^ENTRADAS (\d+)$'
        $n = [int]$m[1]
        $entradas = @()
        for ($i = 0; $i -lt $n; $i++) {
            $e = Esperar $Fluxo '^([APX]) (\d+) (.*)$'
            $tipo = switch ($e[1]) { 'P' { 'pasta' } 'X' { 'programa' } default { 'arquivo' } }
            $entradas += [pscustomobject]@{ Nome = $e[3]; Tipo = $tipo; Tamanho = [int]$e[2] }
        }
        return $entradas
    }
}

function Get-Info {
    param([string]$Caminho)
    Com-Servidor {
        param($Fluxo)
        Mandar-Linha $Fluxo "INFO $Caminho"
        $m = Esperar $Fluxo '^TIPO ([APX]) (\d+)$'
        $tipo = switch ($m[1]) { 'P' { 'pasta' } 'X' { 'programa' } default { 'arquivo' } }
        return [pscustomobject]@{ Caminho = $Caminho; Tipo = $tipo; Tamanho = [int]$m[2] }
    }
}

function Baixar-Arquivo {
    param([string]$Remoto, [string]$Local, [scriptblock]$Progresso)
    Com-Servidor {
        param($Fluxo)
        Mandar-Linha $Fluxo "GET $Remoto"
        $m = Esperar $Fluxo '^DADOS (\d+)$'
        $n = [int]$m[1]
        $arquivo = [System.IO.File]::Create($Local)
        try {
            $lido = 0
            $buf = New-Object byte[] 16384
            while ($lido -lt $n) {
                $pedaco = [Math]::Min($buf.Length, $n - $lido)
                $k = $Fluxo.Read($buf, 0, $pedaco)
                if ($k -le 0) { throw 'a conexão caiu no meio dos dados' }
                $arquivo.Write($buf, 0, $k)
                $lido += $k
                if ($Progresso) { & $Progresso $lido $n }
            }
        } catch {
            $arquivo.Dispose()
            if (Test-Path -LiteralPath $Local) { Remove-Item -LiteralPath $Local -Force }
            throw
        } finally { $arquivo.Dispose() }
        return $n
    }
}

function Enviar-Arquivo {
    param([string]$Local, [string]$Remoto, [scriptblock]$Progresso)
    $dados = [System.IO.File]::ReadAllBytes($Local)
    if ($dados.Length -gt 8MB) {
        throw "o servidor aceita até 8 MiB (o arquivo tem $($dados.Length) bytes)"
    }
    Com-Servidor {
        param($Fluxo)
        Mandar-Linha $Fluxo "PUT $Remoto $($dados.Length)"
        $null = Esperar $Fluxo '^PRONTO$'
        $enviado = 0
        while ($enviado -lt $dados.Length) {
            $n = [Math]::Min(16384, $dados.Length - $enviado)
            $Fluxo.Write($dados, $enviado, $n)
            $Fluxo.Flush()
            $enviado += $n
            if ($Progresso) { & $Progresso $enviado $dados.Length }
        }
        $null = Esperar $Fluxo '^OK$'
        return $dados.Length
    }
}

function Remover-Arquivo {
    param([string]$Caminho)
    Com-Servidor {
        param($Fluxo)
        Mandar-Linha $Fluxo "RM $Caminho"
        $null = Esperar $Fluxo '^OK$'
    }
}

function Criar-Pasta {
    param([string]$Caminho)
    Com-Servidor {
        param($Fluxo)
        Mandar-Linha $Fluxo "MKDIR $Caminho"
        $null = Esperar $Fluxo '^OK$'
    }
}

function Remover-Pasta {
    param([string]$Caminho)
    Com-Servidor {
        param($Fluxo)
        Mandar-Linha $Fluxo "RMDIR $Caminho"
        $null = Esperar $Fluxo '^OK$'
    }
}

function Renomear-Item {
    param([string]$Origem, [string]$Destino)
    if ($Origem -match ' ') {
        throw 'a origem não pode ter espaços (limite do protocolo SAGUI/1)'
    }
    Com-Servidor {
        param($Fluxo)
        Mandar-Linha $Fluxo "REN $Origem $Destino"
        $null = Esperar $Fluxo '^OK$'
    }
}

# ------------------------------------------------------------- virtualbox

function Achar-VBoxManage {
    if ($script:vbox -and (Test-Path -LiteralPath $script:vbox)) { return $script:vbox }
    $candidatos = New-Object System.Collections.Generic.List[string]
    if ($env:VBOX_MSI_INSTALL_PATH) {
        $candidatos.Add((Join-Path $env:VBOX_MSI_INSTALL_PATH 'VBoxManage.exe'))
    }
    if ($env:ProgramFiles) {
        $candidatos.Add((Join-Path $env:ProgramFiles 'Oracle\VirtualBox\VBoxManage.exe'))
    }
    if (${env:ProgramFiles(x86)}) {
        $candidatos.Add((Join-Path ${env:ProgramFiles(x86)} 'Oracle\VirtualBox\VBoxManage.exe'))
    }
    foreach ($c in $candidatos) {
        if (Test-Path -LiteralPath $c) { $script:vbox = $c; return $c }
    }
    $cmd = Get-Command 'VBoxManage.exe' -ErrorAction SilentlyContinue
    if ($cmd) { $script:vbox = $cmd.Source; return $script:vbox }
    throw 'VBoxManage.exe não encontrado: instale o VirtualBox ou coloque-o no PATH.'
}

function Rodar-VBox {
    param([string[]]$Argumentos)
    $vbox = Achar-VBoxManage
    $saida = & $vbox @Argumentos 2>&1
    return [pscustomobject]@{ Codigo = $LASTEXITCODE; Saida = @($saida) }
}

function Estado-Da-Vm {
    $r = Rodar-VBox -Argumentos @('showvminfo', $script:Vm, '--machinereadable')
    if ($r.Codigo -ne 0) { return $null }
    foreach ($linha in $r.Saida) {
        if ("$linha" -match '^VMState="(.+)"$') { return $Matches[1] }
    }
    return ''
}

# Testa o servidor com um tempo curto (a NAT do VirtualBox pode aceitar a
# conexão e ficar esperando o convidado, então não dá para usar o prazo longo).
function Testar-Servidor {
    param([int]$Tentativas = 1)
    for ($i = 0; $i -lt $Tentativas; $i++) {
        $c = $null
        try {
            $c = Nova-Conexao $script:Maquina $script:Porta 1200
            Sair-Do-Servidor $c.Fluxo
            return $true
        } catch {
        } finally {
            if ($c) {
                try { $c.Fluxo.Dispose() } catch { }
                try { $c.Cliente.Dispose() } catch { }
            }
        }
        if ($i -lt ($Tentativas - 1)) { Start-Sleep -Milliseconds 500 }
    }
    return $false
}

# A área de trabalho é um degradê verde/amarelo/azul com a barra escura no
# topo; o console de boot é preto. Duas amostras bastam para distinguir.
function Testar-TelaDesktop {
    param([string]$Arquivo)
    $bmp = $null
    try {
        $bmp = [System.Drawing.Bitmap]::FromFile($Arquivo)
        # (10,10) cai na cabecinha do mascote; o meio da barra é escuro.
        $barra = $bmp.GetPixel([int]($bmp.Width * 0.55), 10)
        # O centro exato pode ser o cursor do mouse (preto); olha em volta.
        $claros = 0
        foreach ($fy in 0.35, 0.5, 0.65) {
            $p = $bmp.GetPixel([int]($bmp.Width / 2), [int]($bmp.Height * $fy))
            if (($p.R + $p.G + $p.B) -gt 250) { $claros++ }
        }
        $barraOk = ($barra.R -ge 15 -and $barra.R -le 45 -and
                    $barra.G -ge 20 -and $barra.G -le 50 -and
                    $barra.B -ge 30 -and $barra.B -le 70)
        return ($barraOk -and ($claros -ge 2))
    } catch {
        return $false
    } finally {
        if ($bmp) { $bmp.Dispose() }
    }
}

function Esperar-Desktop {
    param([int]$Segundos = 90)
    $png = Join-Path $env:TEMP 'sagui-gui-tela.png'
    $fim = (Get-Date).AddSeconds($Segundos)
    while ((Get-Date) -lt $fim) {
        if (Test-Path -LiteralPath $png) {
            Remove-Item -LiteralPath $png -Force -ErrorAction SilentlyContinue
        }
        $r = Rodar-VBox -Argumentos @('controlvm', $script:Vm, 'screenshotpng', $png)
        if ($r.Codigo -eq 0 -and (Test-Path -LiteralPath $png)) {
            $pronta = Testar-TelaDesktop $png
            Remove-Item -LiteralPath $png -Force -ErrorAction SilentlyContinue
            if ($pronta) { return }
        }
        Start-Sleep -Milliseconds 800
    }
    throw 'a área de trabalho do SaguiOS não apareceu; a VM ligou?'
}

# Move o mouse por deltas de no máximo 50 px (o PS/2 do convidado não tem
# posicionamento absoluto). O primeiro movimento grande encosta em (0,0).
function Mover-Mouse {
    param($Mouse, [int]$Dx, [int]$Dy)
    $passos = [Math]::Max(1, [Math]::Ceiling([Math]::Max([Math]::Abs($Dx), [Math]::Abs($Dy)) / 50.0))
    $px = [Math]::Floor($Dx / $passos)
    $py = [Math]::Floor($Dy / $passos)
    for ($i = 0; $i -lt $passos; $i++) {
        $x = if ($i -eq ($passos - 1)) { $Dx - $px * $i } else { $px }
        $y = if ($i -eq ($passos - 1)) { $Dy - $py * $i } else { $py }
        $Mouse.PutMouseEvent([int]$x, [int]$y, 0, 0, 0) | Out-Null
        Start-Sleep -Milliseconds 25
    }
}

$script:mapaTeclas = @{
    'a' = 0x1E; 'b' = 0x30; 'c' = 0x2E; 'd' = 0x20; 'e' = 0x12
    'f' = 0x21; 'g' = 0x22; 'h' = 0x23; 'i' = 0x17; 'j' = 0x24
    'k' = 0x25; 'l' = 0x26; 'm' = 0x32; 'n' = 0x31; 'o' = 0x18
    'p' = 0x19; 'q' = 0x10; 'r' = 0x13; 's' = 0x1F; 't' = 0x14
    'u' = 0x16; 'v' = 0x2F; 'w' = 0x11; 'x' = 0x2D; 'y' = 0x15
    'z' = 0x2C
    '1' = 0x02; '2' = 0x03; '3' = 0x04; '4' = 0x05; '5' = 0x06
    '6' = 0x07; '7' = 0x08; '8' = 0x09; '9' = 0x0A; '0' = 0x0B
    ' ' = 0x39; '.' = 0x34; ',' = 0x33; '-' = 0x0C; '=' = 0x0D
    '/' = 0x73; ';' = 0x35; "'" = 0x29; '[' = 0x1B; ']' = 0x2B
}

function Teclas-De {
    param([string]$Texto)
    $out = New-Object System.Collections.Generic.List[int]
    foreach ($ch in $Texto.ToCharArray()) {
        $c = "$ch".ToLowerInvariant()
        if (-not $script:mapaTeclas.ContainsKey($c)) {
            throw "sem scan code para '$ch'"
        }
        $sc = [int]$script:mapaTeclas[$c]
        $out.Add($sc)
        $out.Add($sc -bor 0x80)
    }
    $out.Add(0x1C)
    $out.Add(0x9C)   # Enter
    return $out
}

# Duplo clique no ícone Terminal (centro em 68,168 na área de trabalho) e
# "servidor" + Enter. A janela nova recebe o foco, então as teclas vão para
# o shell. Se o servidor já estiver ligado, nem chega aqui.
function Injetar-Servidor {
    $sessao = New-Object -ComObject VirtualBox.Session
    $vbox = New-Object -ComObject VirtualBox.VirtualBox
    $maquina = $vbox.FindMachine($script:Vm)
    $maquina.LockMachine($sessao, 1)
    try {
        $mouse = $sessao.Console.Mouse
        Mover-Mouse -Mouse $mouse -Dx -1000 -Dy -1000
        Start-Sleep -Milliseconds 120
        Mover-Mouse -Mouse $mouse -Dx 68 -Dy 168
        Start-Sleep -Milliseconds 150
        for ($c = 0; $c -lt 2; $c++) {
            $mouse.PutMouseEvent(0, 0, 0, 0, 1) | Out-Null
            Start-Sleep -Milliseconds 80
            $mouse.PutMouseEvent(0, 0, 0, 0, 0) | Out-Null
            if ($c -eq 0) { Start-Sleep -Milliseconds 130 }
        }
        Start-Sleep -Milliseconds 900
        foreach ($b in (Teclas-De 'servidor')) {
            $sessao.Console.Keyboard.PutScancode([int]$b) | Out-Null
            Start-Sleep -Milliseconds 8
        }
    } finally {
        $sessao.UnlockMachine()
    }
}

function Ligar-Servidor {
    param([scriptblock]$Aviso)
    $null = Achar-VBoxManage
    if ($Aviso) { & $Aviso 'conferindo o servidor...' }
    if (Testar-Servidor 1) { return 'o servidor já estava ligado' }

    $estado = Estado-Da-Vm
    if ($null -eq $estado) {
        throw "a máquina virtual '$($script:Vm)' não existe (veja os nomes com VBoxManage list vms)"
    }
    if ($estado -eq 'paused') {
        if ($Aviso) { & $Aviso 'retomando a VM...' }
        $r = Rodar-VBox -Argumentos @('controlvm', $script:Vm, 'resume')
        if ($r.Codigo -ne 0) { throw "não consegui retomar a VM '$($script:Vm)'" }
    } elseif ($estado -ne 'running') {
        if ($Aviso) { & $Aviso "ligando a VM '$($script:Vm)'..." }
        $r = Rodar-VBox -Argumentos @('startvm', $script:Vm)
        if ($r.Codigo -ne 0) {
            throw "não consegui ligar a VM '$($script:Vm)': $($r.Saida -join ' ')"
        }
    }

    if ($Aviso) { & $Aviso 'esperando a área de trabalho do SaguiOS...' }
    Esperar-Desktop
    if ($Aviso) { & $Aviso 'abrindo o Terminal e digitando "servidor"...' }
    Injetar-Servidor
    if ($Aviso) { & $Aviso 'esperando o servidor na porta...' }
    if (-not (Testar-Servidor 20)) {
        throw "o servidor não respondeu em $($script:Maquina):$($script:Porta). Veja o Terminal do SaguiOS (a rede pode estar ocupada por um wget)."
    }
    return 'servidor ligado'
}

# ------------------------------------------------------------------ caminhos

function Normalizar-Caminho {
    param([string]$Caminho)
    if ([string]::IsNullOrWhiteSpace($Caminho)) { return '/' }
    $texto = $Caminho.Trim() -replace '\\', '/'
    $partes = New-Object System.Collections.Generic.List[string]
    foreach ($parte in $texto.Split('/')) {
        if ($parte -eq '' -or $parte -eq '.') { continue }
        if ($parte -eq '..') {
            if ($partes.Count -gt 0) { $partes.RemoveAt($partes.Count - 1) }
            continue
        }
        $partes.Add($parte)
    }
    if ($partes.Count -eq 0) { return '/' }
    return '/' + ($partes -join '/')
}

function Caminho-Filho {
    param([string]$Base, [string]$Nome)
    $b = Normalizar-Caminho $Base
    if ($b -eq '/') { return "/$Nome" }
    return "$b/$Nome"
}

function Caminho-Pai {
    param([string]$Caminho)
    $c = Normalizar-Caminho $Caminho
    if ($c -eq '/') { return '/' }
    $i = $c.LastIndexOf('/')
    if ($i -le 0) { return '/' }
    return $c.Substring(0, $i)
}

# ------------------------------------------------------------------ interface

function Definir-Estado {
    param([string]$Texto)
    $script:rotuloStatus.Text = $Texto
}

function Mostrar-Progresso {
    param([int]$Lido, [int]$Total)
    $script:barraProgresso.Visible = $true
    if ($Total -gt 0) {
        $pct = [int](100.0 * $Lido / $Total)
        if ($pct -gt 100) { $pct = 100 }
        $script:barraProgresso.Value = $pct
    }
    Definir-Estado "$Lido de $Total bytes..."
    [System.Windows.Forms.Application]::DoEvents()
}

$script:aoProgresso = { param($l, $t) Mostrar-Progresso $l $t }

function Marcar-Conectado {
    $script:conectado = $true
    $script:rotuloConexao.Text = 'conectado'
    $script:rotuloConexao.ForeColor = [System.Drawing.Color]::FromArgb(11, 122, 62)
}

function Marcar-Sem-Conexao {
    $script:conectado = $false
    $script:rotuloConexao.Text = 'sem conexão'
    $script:rotuloConexao.ForeColor = [System.Drawing.Color]::FromArgb(180, 40, 40)
}

function Preencher-Lista {
    param($Entradas)
    $script:lista.BeginUpdate()
    try {
        $script:lista.Items.Clear()
        $ordem = @($Entradas | Sort-Object `
            @{ Expression = { if ($_.Tipo -eq 'pasta') { 0 } else { 1 } } }, `
            @{ Expression = { $_.Nome.ToLower() } })
        foreach ($e in $ordem) {
            if ($null -eq $e) { continue }
            $item = New-Object System.Windows.Forms.ListViewItem($e.Nome)
            [void]$item.SubItems.Add($e.Tipo)
            if ($e.Tipo -eq 'pasta') {
                [void]$item.SubItems.Add('')
                $item.ForeColor = [System.Drawing.Color]::FromArgb(11, 122, 62)
            } else {
                [void]$item.SubItems.Add(('{0:N0}' -f $e.Tamanho))
                if ($e.Tipo -eq 'programa') { $item.ForeColor = [System.Drawing.Color]::FromArgb(42, 111, 216) }
            }
            $item.Tag = $e
            [void]$script:lista.Items.Add($item)
        }
    } finally { $script:lista.EndUpdate() }
}

function Navegar {
    param([string]$Destino)
    $alvo = Normalizar-Caminho $Destino
    $entradas = Get-Lista $alvo
    $script:caminho = $alvo
    $script:txtCaminho.Text = $alvo
    if ($script:botaoAcima) { $script:botaoAcima.Enabled = ($alvo -ne '/') }
    $script:entradas = @()
    if ($entradas) { $script:entradas = @($entradas) }
    Preencher-Lista $script:entradas
    Marcar-Conectado
    $pastas = @($script:entradas | Where-Object { $_.Tipo -eq 'pasta' }).Count
    $arquivos = $script:entradas.Count - $pastas
    Definir-Estado "$alvo - $pastas pasta(s), $arquivos arquivo(s)"
}

function Item-Selecionado {
    if ($script:lista.SelectedItems.Count -eq 0) { return $null }
    return $script:lista.SelectedItems[0]
}

function Atualizar-Selecao {
    $item = Item-Selecionado
    if (-not $item) {
        Definir-Estado "$($script:caminho) - $($script:entradas.Count) entrada(s)"
        return
    }
    $e = $item.Tag
    if ($e.Tipo -eq 'pasta') {
        Definir-Estado "$($e.Nome) - pasta (duplo clique para entrar)"
    } else {
        Definir-Estado ("{0} - {1}, {2} bytes (duplo clique para baixar)" -f $e.Nome, $e.Tipo, $e.Tamanho)
    }
}

function Executar {
    param([scriptblock]$Acao)
    if ($script:ocupado) { return }
    $script:ocupado = $true
    $script:form.UseWaitCursor = $true
    $script:barraProgresso.Value = 0
    try {
        & $Acao
    } catch {
        $msg = $_.Exception.Message
        Definir-Estado "erro: $msg"
        if ($msg -match 'conect|conexão|caiu|banner') { Marcar-Sem-Conexao }
        if (-not $script:semCaixas) {
            [void][System.Windows.Forms.MessageBox]::Show($msg, 'SaguiOS - Arquivos', 'OK', 'Warning')
        }
    } finally {
        $script:barraProgresso.Visible = $false
        $script:form.UseWaitCursor = $false
        $script:ocupado = $false
    }
}

function Protegido {
    param([scriptblock]$Acao)
    if ($script:ocupado) { return }
    try { & $Acao }
    catch {
        Definir-Estado "erro: $($_.Exception.Message)"
        if (-not $script:semCaixas) {
            [void][System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'SaguiOS - Arquivos', 'OK', 'Warning')
        }
    }
}

# ------------------------------------------------------------------ ações

function Testar-Conexao {
    param([switch]$Silencioso)
    try {
        $entradas = Get-Lista '/'
        $script:caminho = '/'
        $script:txtCaminho.Text = '/'
        $script:entradas = @()
        if ($entradas) { $script:entradas = @($entradas) }
        Preencher-Lista $script:entradas
        Marcar-Conectado
        $pastas = @($script:entradas | Where-Object { $_.Tipo -eq 'pasta' }).Count
        Definir-Estado "conectado a $($script:Maquina):$($script:Porta) - $($script:entradas.Count) entrada(s) em /"
    } catch {
        Marcar-Sem-Conexao
        if (-not $Silencioso) { throw }
        Definir-Estado 'sem conexão - ligue o servidor no SaguiOS (comando "servidor")'
    }
}

function Acao-Navegar {
    param([string]$Destino)
    Executar { Navegar $Destino }
}

function Acao-Subir {
    if ($script:caminho -eq '/') { return }
    Acao-Navegar (Caminho-Pai $script:caminho)
}

function Ler-Destino {
    $p = 0
    if (-not [int]::TryParse($script:txtPorta.Text.Trim(), [ref]$p)) {
        throw 'a porta precisa ser um número.'
    }
    $script:Maquina = $script:txtMaquina.Text.Trim()
    $script:Porta = $p
}

function Acao-Conectar {
    try { Ler-Destino } catch {
        [void][System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'SaguiOS - Arquivos', 'OK', 'Warning')
        return
    }
    Executar { Testar-Conexao }
}

function Acao-LigarServidor {
    try { Ler-Destino } catch {
        [void][System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'SaguiOS - Arquivos', 'OK', 'Warning')
        return
    }
    if ($script:Maquina -notmatch '^(127\.0\.0\.1|localhost)$') {
        [void][System.Windows.Forms.MessageBox]::Show(
            'O botão "Ligar servidor" liga a VM local. Para outra máquina, ligue o servidor nela.',
            'Ligar servidor', 'OK', 'Information')
        return
    }
    Executar {
        $script:barraProgresso.Visible = $true
        $script:barraProgresso.Style = 'Marquee'
        try {
            $resultado = Ligar-Servidor { param($texto) Definir-Estado $texto }
            Testar-Conexao
            Definir-Estado "$resultado - $($script:lista.Items.Count) entrada(s) em /"
        } finally {
            $script:barraProgresso.Style = 'Blocks'
        }
    }
}

function Acao-Abrir {
    $item = Item-Selecionado
    if (-not $item) { return }
    $e = $item.Tag
    if ($e.Tipo -eq 'pasta') {
        Acao-Navegar (Caminho-Filho $script:caminho $e.Nome)
    } else {
        Acao-Baixar
    }
}

function Acao-Baixar {
    $item = Item-Selecionado
    if (-not $item) { Definir-Estado 'selecione um arquivo na lista'; return }
    $e = $item.Tag
    if ($e.Tipo -eq 'pasta') { Acao-Navegar (Caminho-Filho $script:caminho $e.Nome); return }

    $dlg = New-Object System.Windows.Forms.SaveFileDialog
    $dlg.Title = 'Salvar arquivo do SaguiOS'
    $dlg.FileName = $e.Nome
    if ($script:ultimaPasta) { $dlg.InitialDirectory = $script:ultimaPasta }
    if ($dlg.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { return }

    $local = $dlg.FileName
    $script:ultimaPasta = [System.IO.Path]::GetDirectoryName($local)
    $remoto = Caminho-Filho $script:caminho $e.Nome
    Executar {
        $n = Baixar-Arquivo $remoto $local $script:aoProgresso
        Definir-Estado "baixado '$($e.Nome)' ($n bytes) para $local"
    }
}

function Enviar-Arquivos {
    param([string[]]$Arquivos)
    $arquivosOk = @($Arquivos | Where-Object { $_ -and (Test-Path -LiteralPath $_) })
    if ($arquivosOk.Count -eq 0) { return }
    Executar {
        $enviados = 0
        foreach ($f in $arquivosOk) {
            $nome = [System.IO.Path]::GetFileName($f)
            $remoto = Caminho-Filho $script:caminho $nome
            $null = Enviar-Arquivo $f $remoto $script:aoProgresso
            $enviados++
        }
        Navegar $script:caminho
        Definir-Estado "enviado(s) $enviados arquivo(s) para $($script:caminho)"
    }
}

function Acao-Enviar {
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Title = 'Enviar para o SaguiOS'
    $dlg.Multiselect = $true
    if ($script:ultimaPasta) { $dlg.InitialDirectory = $script:ultimaPasta }
    if ($dlg.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { return }
    $script:ultimaPasta = [System.IO.Path]::GetDirectoryName($dlg.FileNames[0])
    Enviar-Arquivos $dlg.FileNames
}

function Acao-NovaPasta {
    $nome = [Microsoft.VisualBasic.Interaction]::InputBox('Nome da nova pasta:', 'Nova pasta no SaguiOS', '')
    if ([string]::IsNullOrWhiteSpace($nome)) { return }
    $nome = $nome.Trim()
    if ($nome -match '[\\/]') {
        [void][System.Windows.Forms.MessageBox]::Show('O nome não pode ter barras.', 'Nova pasta', 'OK', 'Warning')
        return
    }
    Executar {
        Criar-Pasta (Caminho-Filho $script:caminho $nome)
        Navegar $script:caminho
        Definir-Estado "pasta '$nome' criada"
    }
}

function Acao-Renomear {
    $item = Item-Selecionado
    if (-not $item) { Definir-Estado 'selecione um arquivo ou pasta'; return }
    $e = $item.Tag
    $origem = Caminho-Filho $script:caminho $e.Nome
    if ($origem -match ' ') {
        [void][System.Windows.Forms.MessageBox]::Show(
            'O caminho de origem tem espaços: o protocolo SAGUI/1 não renomeia origem com espaço. Renomeie para uma pasta sem espaços.',
            'Renomear', 'OK', 'Warning')
        return
    }
    $novo = [Microsoft.VisualBasic.Interaction]::InputBox('Novo nome:', 'Renomear no SaguiOS', $e.Nome)
    if ([string]::IsNullOrWhiteSpace($novo)) { return }
    $novo = $novo.Trim()
    if ($novo -match '[\\/]') {
        [void][System.Windows.Forms.MessageBox]::Show('O nome não pode ter barras.', 'Renomear', 'OK', 'Warning')
        return
    }
    $destino = Caminho-Filho $script:caminho $novo
    Executar {
        Renomear-Item $origem $destino
        Navegar $script:caminho
        Definir-Estado "renomeado para '$novo'"
    }
}

function Acao-Apagar {
    $item = Item-Selecionado
    if (-not $item) { Definir-Estado 'selecione um arquivo'; return }
    $e = $item.Tag
    if ($e.Tipo -eq 'pasta') {
        [void][System.Windows.Forms.MessageBox]::Show(
            'Isto é uma pasta: use "Remover pasta" (só funciona se estiver vazia).',
            'Apagar', 'OK', 'Information')
        return
    }
    $remoto = Caminho-Filho $script:caminho $e.Nome
    $r = [System.Windows.Forms.MessageBox]::Show("Apagar '$($e.Nome)' do SaguiOS?", 'Apagar', 'YesNo', 'Question')
    if ($r -ne [System.Windows.Forms.DialogResult]::Yes) { return }
    Executar {
        Remover-Arquivo $remoto
        Navegar $script:caminho
        Definir-Estado "apagado '$($e.Nome)'"
    }
}

function Acao-RemoverPasta {
    $item = Item-Selecionado
    if (-not $item) { Definir-Estado 'selecione uma pasta'; return }
    $e = $item.Tag
    if ($e.Tipo -ne 'pasta') {
        [void][System.Windows.Forms.MessageBox]::Show('Isto não é uma pasta: use "Apagar" para arquivos.', 'Remover pasta', 'OK', 'Information')
        return
    }
    $remoto = Caminho-Filho $script:caminho $e.Nome
    $r = [System.Windows.Forms.MessageBox]::Show("Remover a pasta vazia '$($e.Nome)'?", 'Remover pasta', 'YesNo', 'Question')
    if ($r -ne [System.Windows.Forms.DialogResult]::Yes) { return }
    Executar {
        Remover-Pasta $remoto
        Navegar $script:caminho
        Definir-Estado "pasta '$($e.Nome)' removida"
    }
}

# ------------------------------------------------------------------ a janela

function Criar-Janela {
    $form = New-Object System.Windows.Forms.Form
    $form.Text = 'SaguiOS - Arquivos pela rede'
    $form.ClientSize = New-Object System.Drawing.Size(920, 620)
    $form.MinimumSize = New-Object System.Drawing.Size(760, 480)
    $form.StartPosition = 'CenterScreen'
    $form.Font = New-Object System.Drawing.Font('Segoe UI', 9)

    # barra de conexão
    $painelConexao = New-Object System.Windows.Forms.Panel
    $painelConexao.Dock = 'Top'
    $painelConexao.Height = 44

    $rotuloMaquina = New-Object System.Windows.Forms.Label
    $rotuloMaquina.Text = 'Máquina:'
    $rotuloMaquina.Location = New-Object System.Drawing.Point(10, 14)
    $rotuloMaquina.AutoSize = $true

    $txtMaquina = New-Object System.Windows.Forms.TextBox
    $txtMaquina.Location = New-Object System.Drawing.Point(72, 11)
    $txtMaquina.Size = New-Object System.Drawing.Size(150, 24)
    $txtMaquina.Text = $script:Maquina

    $rotuloPorta = New-Object System.Windows.Forms.Label
    $rotuloPorta.Text = 'Porta:'
    $rotuloPorta.Location = New-Object System.Drawing.Point(238, 14)
    $rotuloPorta.AutoSize = $true

    $txtPorta = New-Object System.Windows.Forms.TextBox
    $txtPorta.Location = New-Object System.Drawing.Point(280, 11)
    $txtPorta.Size = New-Object System.Drawing.Size(60, 24)
    $txtPorta.Text = "$($script:Porta)"

    $botaoConectar = New-Object System.Windows.Forms.Button
    $botaoConectar.Text = 'Conectar'
    $botaoConectar.Location = New-Object System.Drawing.Point(356, 9)
    $botaoConectar.Size = New-Object System.Drawing.Size(96, 27)

    $botaoLigarServidor = New-Object System.Windows.Forms.Button
    $botaoLigarServidor.Text = 'Ligar servidor'
    $botaoLigarServidor.Location = New-Object System.Drawing.Point(462, 9)
    $botaoLigarServidor.Size = New-Object System.Drawing.Size(124, 27)

    $rotuloConexao = New-Object System.Windows.Forms.Label
    $rotuloConexao.Text = 'sem conexão'
    $rotuloConexao.Location = New-Object System.Drawing.Point(600, 14)
    $rotuloConexao.AutoSize = $true
    $rotuloConexao.ForeColor = [System.Drawing.Color]::FromArgb(106, 115, 124)

    [void]$painelConexao.Controls.Add($rotuloMaquina)
    [void]$painelConexao.Controls.Add($txtMaquina)
    [void]$painelConexao.Controls.Add($rotuloPorta)
    [void]$painelConexao.Controls.Add($txtPorta)
    [void]$painelConexao.Controls.Add($botaoConectar)
    [void]$painelConexao.Controls.Add($botaoLigarServidor)
    [void]$painelConexao.Controls.Add($rotuloConexao)

    # barra de caminho
    $painelCaminho = New-Object System.Windows.Forms.Panel
    $painelCaminho.Dock = 'Top'
    $painelCaminho.Height = 40

    $rotuloPasta = New-Object System.Windows.Forms.Label
    $rotuloPasta.Text = 'Pasta:'
    $rotuloPasta.Location = New-Object System.Drawing.Point(10, 12)
    $rotuloPasta.AutoSize = $true

    $txtCaminho = New-Object System.Windows.Forms.TextBox
    $txtCaminho.Location = New-Object System.Drawing.Point(62, 9)
    $txtCaminho.Size = New-Object System.Drawing.Size(610, 24)
    $txtCaminho.Anchor = 'Top, Left, Right'
    $txtCaminho.Text = '/'

    $botaoIr = New-Object System.Windows.Forms.Button
    $botaoIr.Text = 'Ir'
    $botaoIr.Location = New-Object System.Drawing.Point(750, 7)
    $botaoIr.Size = New-Object System.Drawing.Size(64, 26)
    $botaoIr.Anchor = 'Top, Right'

    $botaoAtualizar = New-Object System.Windows.Forms.Button
    $botaoAtualizar.Text = 'Atualizar'
    $botaoAtualizar.Location = New-Object System.Drawing.Point(828, 7)
    $botaoAtualizar.Size = New-Object System.Drawing.Size(80, 26)
    $botaoAtualizar.Anchor = 'Top, Right'

    [void]$painelCaminho.Controls.Add($rotuloPasta)
    [void]$painelCaminho.Controls.Add($txtCaminho)
    [void]$painelCaminho.Controls.Add($botaoIr)
    [void]$painelCaminho.Controls.Add($botaoAtualizar)

    # barra de ações
    $painelBotoes = New-Object System.Windows.Forms.Panel
    $painelBotoes.Dock = 'Top'
    $painelBotoes.Height = 42

    $fluxoBotoes = New-Object System.Windows.Forms.FlowLayoutPanel
    $fluxoBotoes.Dock = 'Fill'
    $fluxoBotoes.WrapContents = $false
    $fluxoBotoes.Padding = New-Object System.Windows.Forms.Padding(8, 8, 0, 0)

    $botaoAcima = New-Object System.Windows.Forms.Button
    $botaoAcima.Text = '↑ Pasta acima'
    $botaoAcima.Size = New-Object System.Drawing.Size(116, 26)
    $botaoAcima.Margin = New-Object System.Windows.Forms.Padding(0, 0, 6, 0)

    $botaoBaixar = New-Object System.Windows.Forms.Button
    $botaoBaixar.Text = 'Baixar'
    $botaoBaixar.Size = New-Object System.Drawing.Size(88, 26)
    $botaoBaixar.Margin = New-Object System.Windows.Forms.Padding(0, 0, 6, 0)

    $botaoEnviar = New-Object System.Windows.Forms.Button
    $botaoEnviar.Text = 'Enviar...'
    $botaoEnviar.Size = New-Object System.Drawing.Size(96, 26)
    $botaoEnviar.Margin = New-Object System.Windows.Forms.Padding(0, 0, 6, 0)

    $botaoNovaPasta = New-Object System.Windows.Forms.Button
    $botaoNovaPasta.Text = 'Nova pasta'
    $botaoNovaPasta.Size = New-Object System.Drawing.Size(96, 26)
    $botaoNovaPasta.Margin = New-Object System.Windows.Forms.Padding(0, 0, 6, 0)

    $botaoRenomear = New-Object System.Windows.Forms.Button
    $botaoRenomear.Text = 'Renomear'
    $botaoRenomear.Size = New-Object System.Drawing.Size(88, 26)
    $botaoRenomear.Margin = New-Object System.Windows.Forms.Padding(0, 0, 6, 0)

    $botaoApagar = New-Object System.Windows.Forms.Button
    $botaoApagar.Text = 'Apagar'
    $botaoApagar.Size = New-Object System.Drawing.Size(80, 26)
    $botaoApagar.Margin = New-Object System.Windows.Forms.Padding(0, 0, 6, 0)

    $botaoRemoverPasta = New-Object System.Windows.Forms.Button
    $botaoRemoverPasta.Text = 'Remover pasta'
    $botaoRemoverPasta.Size = New-Object System.Drawing.Size(110, 26)
    $botaoRemoverPasta.Margin = New-Object System.Windows.Forms.Padding(0, 0, 6, 0)

    [void]$fluxoBotoes.Controls.Add($botaoAcima)
    [void]$fluxoBotoes.Controls.Add($botaoBaixar)
    [void]$fluxoBotoes.Controls.Add($botaoEnviar)
    [void]$fluxoBotoes.Controls.Add($botaoNovaPasta)
    [void]$fluxoBotoes.Controls.Add($botaoRenomear)
    [void]$fluxoBotoes.Controls.Add($botaoApagar)
    [void]$fluxoBotoes.Controls.Add($botaoRemoverPasta)
    [void]$painelBotoes.Controls.Add($fluxoBotoes)

    # lista
    $lista = New-Object System.Windows.Forms.ListView
    $lista.Dock = 'Fill'
    $lista.View = 'Details'
    $lista.FullRowSelect = $true
    $lista.GridLines = $true
    $lista.MultiSelect = $false
    $lista.HideSelection = $false
    $lista.AllowDrop = $true
    [void]$lista.Columns.Add('Nome', 520)
    [void]$lista.Columns.Add('Tipo', 110)
    [void]$lista.Columns.Add('Tamanho (bytes)', 150)

    # barra de estado
    $status = New-Object System.Windows.Forms.StatusStrip
    $rotuloStatus = New-Object System.Windows.Forms.ToolStripStatusLabel
    $rotuloStatus.Spring = $true
    $rotuloStatus.TextAlign = 'MiddleLeft'
    $rotuloStatus.Text = 'ligue o servidor no SaguiOS (comando "servidor") e clique em Conectar'
    $barraProgresso = New-Object System.Windows.Forms.ToolStripProgressBar
    $barraProgresso.Width = 180
    $barraProgresso.Visible = $false
    [void]$status.Items.Add($rotuloStatus)
    [void]$status.Items.Add($barraProgresso)

    # o Fill entra primeiro; as barras Top são adicionadas de baixo para cima
    [void]$form.Controls.Add($lista)
    [void]$form.Controls.Add($painelBotoes)
    [void]$form.Controls.Add($painelCaminho)
    [void]$form.Controls.Add($painelConexao)
    [void]$form.Controls.Add($status)

    $script:form = $form
    $script:lista = $lista
    $script:txtCaminho = $txtCaminho
    $script:txtMaquina = $txtMaquina
    $script:txtPorta = $txtPorta
    $script:botaoAcima = $botaoAcima
    $script:botaoAcima.Enabled = $false
    $script:rotuloConexao = $rotuloConexao
    $script:rotuloStatus = $rotuloStatus
    $script:barraProgresso = $barraProgresso

    # ---- eventos ----
    $botaoConectar.Add_Click({ Protegido { Acao-Conectar } })
    $botaoLigarServidor.Add_Click({ Protegido { Acao-LigarServidor } })
    $botaoIr.Add_Click({ Protegido { Acao-Navegar $script:txtCaminho.Text } })
    $botaoAcima.Add_Click({ Protegido { Acao-Subir } })
    $botaoAtualizar.Add_Click({ Protegido { Acao-Navegar $script:caminho } })
    $botaoBaixar.Add_Click({ Protegido { Acao-Baixar } })
    $botaoEnviar.Add_Click({ Protegido { Acao-Enviar } })
    $botaoNovaPasta.Add_Click({ Protegido { Acao-NovaPasta } })
    $botaoRenomear.Add_Click({ Protegido { Acao-Renomear } })
    $botaoApagar.Add_Click({ Protegido { Acao-Apagar } })
    $botaoRemoverPasta.Add_Click({ Protegido { Acao-RemoverPasta } })

    $lista.Add_DoubleClick({ Protegido { Acao-Abrir } })
    $lista.Add_SelectedIndexChanged({ Atualizar-Selecao })
    $lista.Add_KeyDown({
        param($remetente, $e)
        switch ($e.KeyCode) {
            'F5'     { Protegido { Acao-Navegar $script:caminho }; $e.Handled = $true }
            'Delete' { Protegido { Acao-Apagar }; $e.Handled = $true }
            'Return' { Protegido { Acao-Abrir }; $e.Handled = $true }
            'Back'   { Protegido { Acao-Subir }; $e.Handled = $true }
        }
    })
    $lista.Add_DragEnter({
        param($remetente, $e)
        if ($e.Data.GetDataPresent([System.Windows.Forms.DataFormats]::FileDrop)) {
            $e.Effect = [System.Windows.Forms.DragDropEffects]::Copy
        } else {
            $e.Effect = [System.Windows.Forms.DragDropEffects]::None
        }
    })
    $lista.Add_DragDrop({
        param($remetente, $e)
        $arquivos = $e.Data.GetData([System.Windows.Forms.DataFormats]::FileDrop)
        if ($arquivos) { Protegido { Enviar-Arquivos $arquivos } }
    })

    $txtCaminho.Add_KeyDown({
        param($remetente, $e)
        if ($e.KeyCode -eq [System.Windows.Forms.Keys]::Enter) {
            Protegido { Acao-Navegar $script:txtCaminho.Text }
            $e.SuppressKeyPress = $true
        }
    })
    $txtMaquina.Add_KeyDown({
        param($remetente, $e)
        if ($e.KeyCode -eq [System.Windows.Forms.Keys]::Enter) {
            Protegido { Acao-Conectar }
            $e.SuppressKeyPress = $true
        }
    })
    $txtPorta.Add_KeyDown({
        param($remetente, $e)
        if ($e.KeyCode -eq [System.Windows.Forms.Keys]::Enter) {
            Protegido { Acao-Conectar }
            $e.SuppressKeyPress = $true
        }
    })

    $form.Add_FormClosing({
        param($remetente, $e)
        if ($script:ocupado) { $e.Cancel = $true }
    })
    $form.Add_Shown({
        try { Testar-Conexao -Silencioso } catch { }
    })

    return $form
}

# ------------------------------------------------------------------ autoteste

function Checar {
    param([bool]$Condicao, [string]$Texto)
    if ($Condicao) {
        Write-Host "[AUTOTESTE] ok: $Texto"
    } else {
        Write-Host "[AUTOTESTE] FALHOU: $Texto"
        $script:falhas++
    }
}

function Teste-Automatico {
    $base = Join-Path $env:TEMP 'sagui-gui-autoteste'
    if (Test-Path -LiteralPath $base) { Remove-Item -LiteralPath $base -Recurse -Force }
    New-Item -ItemType Directory -Path $base | Out-Null

    Checar ((Normalizar-Caminho 'bin/../docs//x') -eq '/docs/x') 'Normalizar-Caminho com .. e //'
    Checar ((Caminho-Filho '/' 'docs') -eq '/docs') 'Caminho-Filho na raiz'
    Checar ((Caminho-Filho '/docs' 'nota.txt') -eq '/docs/nota.txt') 'Caminho-Filho numa pasta'
    Checar ((Caminho-Pai '/docs/nota.txt') -eq '/docs') 'Caminho-Pai'
    Checar ((Caminho-Pai '/docs') -eq '/') 'Caminho-Pai até a raiz'

    try {
        $entradas = Get-Lista '/'
        $n = @($entradas).Count
        Checar ($n -gt 0) "LIST / devolveu $n entradas"

        $leia = @($entradas) | Where-Object { $_.Nome -eq 'leia-me.txt' } | Select-Object -First 1
        Checar ($null -ne $leia) 'leia-me.txt aparece na raiz'

        $info = Get-Info '/leia-me.txt'
        Checar (($info.Tipo -eq 'arquivo') -and ($info.Tamanho -gt 0)) "INFO leia-me.txt: $($info.Tipo), $($info.Tamanho) bytes"

        $baixado = Join-Path $base 'leia-me.txt'
        $bytes = Baixar-Arquivo '/leia-me.txt' $baixado
        Checar (((Get-Item -LiteralPath $baixado).Length -eq $bytes) -and ($bytes -eq $info.Tamanho)) "GET leia-me.txt ($bytes bytes)"

        # PUT/GET de ida e volta (300 linhas, maior que a janela do TCP)
        $fonte = Join-Path $base 'ida-volta.txt'
        [string[]]$linhas = 1..300 | ForEach-Object { "linha $_ do teste da janela - sagui" }
        [System.IO.File]::WriteAllLines($fonte, $linhas, $Encoding)
        $enviado = Enviar-Arquivo $fonte '/autoteste-gui.txt'
        Checar ($enviado -eq (Get-Item -LiteralPath $fonte).Length) "PUT de $enviado bytes"
        $volta = Join-Path $base 'volta.txt'
        $null = Baixar-Arquivo '/autoteste-gui.txt' $volta
        $h1 = (Get-FileHash -LiteralPath $fonte -Algorithm SHA256).Hash
        $h2 = (Get-FileHash -LiteralPath $volta -Algorithm SHA256).Hash
        Checar ($h1 -eq $h2) 'PUT/GET ida e volta byte a byte'
        Remover-Arquivo '/autoteste-gui.txt'

        Criar-Pasta '/autoteste-gui'
        Renomear-Item '/autoteste-gui' '/autoteste-gui-2'
        $dentro = Join-Path $base 'dentro.txt'
        [System.IO.File]::WriteAllText($dentro, "dentro`n", $Encoding)
        $null = Enviar-Arquivo $dentro '/autoteste-gui-2/dentro.txt'
        $listaPasta = @(Get-Lista '/autoteste-gui-2')
        Checar (@($listaPasta | Where-Object { $_.Nome -eq 'dentro.txt' }).Count -eq 1) 'pasta renomeada recebeu o arquivo'
        Remover-Arquivo '/autoteste-gui-2/dentro.txt'
        Remover-Pasta '/autoteste-gui-2'
        $restou = @(Get-Lista '/') | Where-Object { $_.Nome -like 'autoteste-gui*' }
        Checar (@($restou).Count -eq 0) 'pasta removida da raiz'

        $erro = $null
        try { $null = Get-Info '/nao-existe-h49.txt' } catch { $erro = $_.Exception.Message }
        Checar (($null -ne $erro) -and ($erro -match 'ERRO')) "erro do servidor tratado: $erro"
    } catch {
        Write-Host "[AUTOTESTE] FALHOU (exceção): $($_.Exception.Message)"
        $script:falhas++
    } finally {
        if (Test-Path -LiteralPath $base) { Remove-Item -LiteralPath $base -Recurse -Force }
    }

    if ($script:falhas -eq 0) {
        Write-Host '[AUTOTESTE] TUDO OK'
    } else {
        Write-Host "[AUTOTESTE] $($script:falhas) falha(s)"
    }
}

# Constroi a janela, conecta e navega de verdade; sem dialogos. Precisa do
# servidor ligado no SaguiOS (comando "servidor").
function Teste-Da-Janela {
    $script:semCaixas = $true
    $script:falhas = 0
    $janela = Criar-Janela
    try {
        [void]$janela.Handle
        [void]$script:lista.Handle
        Checar $true 'janela construída'
        try { Testar-Conexao -Silencioso } catch { }
        Checar ($script:conectado) 'conectado ao servidor'
        Checar ($script:lista.Items.Count -gt 0) "lista com $($script:lista.Items.Count) entrada(s)"
        Checar ($script:txtCaminho.Text -eq '/') 'campo de caminho mostra /'
        Checar ($script:rotuloConexao.Text -eq 'conectado') 'rótulo diz conectado'

        $pasta = $null
        foreach ($it in $script:lista.Items) {
            if ($it.Tag.Tipo -eq 'pasta') { $pasta = $it; break }
        }
        Checar ($null -ne $pasta) 'há uma pasta na raiz'
        if ($pasta) {
            $nome = $pasta.Tag.Nome
            $pasta.Selected = $true
            Atualizar-Selecao
            Checar ($script:rotuloStatus.Text -match [regex]::Escape($nome)) "status mostra a seleção ($nome)"
            Acao-Abrir
            Checar ($script:caminho -eq "/$nome") "abrir entrou em /$nome"
            Checar ($script:txtCaminho.Text -eq "/$nome") 'campo de caminho acompanhou a pasta'
            Checar ($script:botaoAcima.Enabled) 'botão Pasta acima habilitado dentro da pasta'
            Acao-Subir
            Checar ($script:caminho -eq '/') 'botão Pasta acima voltou para a raiz'
            Checar (-not $script:botaoAcima.Enabled) 'botão Pasta acima desabilitado na raiz'
            Checar ($script:lista.Items.Count -gt 0) 'lista repovoada na volta'
        }
    } catch {
        Write-Host "[JANELA] FALHOU (exceção): $($_.Exception.Message)"
        $script:falhas++
    } finally {
        $janela.Dispose()
    }
    if ($script:falhas -eq 0) {
        Write-Host '[JANELA] TUDO OK'
    } else {
        Write-Host "[JANELA] $($script:falhas) falha(s)"
    }
}

# Constroi a janela, liga a VM e o servidor de verdade, e sai. Precisa do
# VirtualBox com a VM parada (ou ja ligada, sem o servidor).
function Teste-Do-Ligar {
    $script:semCaixas = $true
    $script:falhas = 0
    $janela = Criar-Janela
    try {
        [void]$janela.Handle
        [void]$script:lista.Handle
        Checar $true 'janela construída'
        Acao-LigarServidor
        Write-Host "[LIGAR] status: $($script:rotuloStatus.Text)"
        Checar ($script:conectado) 'servidor ligado e conectado'
        Checar ($script:lista.Items.Count -gt 0) "lista com $($script:lista.Items.Count) entrada(s)"
        Checar ($script:rotuloConexao.Text -eq 'conectado') 'rótulo diz conectado'
    } catch {
        Write-Host "[LIGAR] FALHOU (exceção): $($_.Exception.Message)"
        $script:falhas++
    } finally {
        $janela.Dispose()
    }
    if ($script:falhas -eq 0) {
        Write-Host '[LIGAR] TUDO OK'
    } else {
        Write-Host "[LIGAR] $($script:falhas) falha(s)"
    }
}

# ------------------------------------------------------------------ principal

if ($Autoteste) {
    Teste-Automatico
    exit $script:falhas
}

[System.Windows.Forms.Application]::EnableVisualStyles()

if ($AutotesteLigar) {
    Teste-Do-Ligar
    exit $script:falhas
}

if ($AutotesteJanela) {
    Teste-Da-Janela
    exit $script:falhas
}

$janela = Criar-Janela

if ($TesteJanela) {
    [void]$janela.Handle
    Write-Host '[JANELA] construída sem erros'
    $janela.Dispose()
    exit 0
}

[System.Windows.Forms.Application]::Run($janela)
