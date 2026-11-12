# sagui-fs.ps1 - cliente de arquivos do SaguiOS, rodando no Windows.
#
# Conecta na porta 2500 do convidado (encaminhada pela NAT do VirtualBox
# em tools/windows/build-e-rodar.ps1, ou pelo hostfwd do QEMU) e fala o
# protocolo SAGUI/1 com o servidor de arquivos do sistema (ver
# sagui-os/kernel/rede/servidor.c). No convidado o servidor precisa estar
# ligado: o comando "servidor" do shell (rode na frente, ou com &).
#
# Uso (powershell -ExecutionPolicy Bypass -File sagui-fs.ps1 ...):
#   sagui-fs.ps1 listar [caminho]            # ou ls
#   sagui-fs.ps1 info <caminho>
#   sagui-fs.ps1 baixar <remoto> [local]      # ou get
#   sagui-fs.ps1 enviar <local> <remoto>     # ou put
#   sagui-fs.ps1 apagar <caminho>             # ou rm
#   sagui-fs.ps1 criar-pasta <caminho>        # ou mkdir
#   sagui-fs.ps1 remover-pasta <caminho>      # ou rmdir
#   sagui-fs.ps1 renomear <origem> <destino>  # ou ren (origem sem espaços)
#
# Opções: -Maquina <ip> (padrão 127.0.0.1) e -Porta <n> (padrão 2500).
# Os caminhos remotos são os do SaguiOS ("bin", "/docs", "../leia-me.txt");
# os locais, os do Windows.

param(
    [string]$Maquina = '127.0.0.1',
    [int]$Porta = 2500,
    [Parameter(Position = 0, Mandatory = $true)][string]$Comando,
    [Parameter(Position = 1, ValueFromRemainingArguments = $true)][string[]]$Argumentos
)

$Encoding = [System.Text.Encoding]::GetEncoding(28591)   # Latin-1, o idioma do SaguiOS

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

function Ler-Exato {
    param($Fluxo, [int]$N)
    $buf = New-Object byte[] $N
    $lido = 0
    while ($lido -lt $N) {
        $k = $Fluxo.Read($buf, $lido, $N - $lido)
        if ($k -le 0) { throw 'a conexão caiu no meio dos dados' }
        $lido += $k
    }
    return ,$buf
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
    # Os grupos de $Matches não valem para quem chamou (o -match rodou aqui
    # dentro): a tabela é devolvida, e o chamador lê o grupo por [n].
    return $Matches
}

function Sair-Do-Servidor {
    param($Fluxo)
    try {
        Mandar-Linha $Fluxo 'SAIR'
        $null = Ler-Linha $Fluxo        # TCHAU
    } catch { }
}

$uso = @'
Uso: sagui-fs.ps1 [-Maquina ip] [-Porta n] <comando> [argumentos]

Comandos:
  listar [caminho]             (ls)    lista os arquivos e pastas
  info <caminho>               (i)     mostra o tipo e o tamanho
  baixar <remoto> [local]      (get)   copia um arquivo do SaguiOS para o Windows
  enviar <local> <remoto>      (put)   copia um arquivo do Windows para o SaguiOS
  apagar <caminho>             (rm)    apaga um arquivo
  criar-pasta <caminho>        (mkdir) cria uma pasta
  remover-pasta <caminho>      (rmdir) remove uma pasta vazia
  renomear <origem> <destino>  (ren)   renomeia ou move (origem sem espaços)

Padrões: -Maquina 127.0.0.1 -Porta 2500 (o redirecionamento da NAT do
VirtualBox aponta para o convidado). No convidado, o servidor precisa
estar ligado: comando "servidor" no shell do SaguiOS.
'@

if (-not $Comando) {
    Write-Host $uso
    exit 2
}

# ------------------------------------------------------------------ conectar
$cliente = New-Object System.Net.Sockets.TcpClient
$cliente.ReceiveTimeout = 20000
$cliente.SendTimeout = 20000
try {
    $cliente.Connect($Maquina, $Porta)
} catch {
    Write-Host "sagui-fs: não conectou em ${Maquina}:${Porta}." -ForegroundColor Red
    Write-Host '  - O servidor está ligado no convidado? (comando "servidor" no shell)' -ForegroundColor Red
    Write-Host '  - A máquina virtual está rodando? O redirecionamento de porta é' -ForegroundColor Red
    Write-Host '    aplicado por tools/windows/build-e-rodar.ps1 a cada boot.' -ForegroundColor Red
    exit 1
}

$fluxo = $cliente.GetStream()
try {
    $banner = Ler-Linha $fluxo
    if ($banner -ne 'SAGUI/1') { throw "banner inesperado: '$banner' (não é o servidor de arquivos?)" }

    switch ($Comando) {

        { $_ -in 'listar', 'ls' } {
            $caminho = '/'
            if ($Argumentos) { $caminho = $Argumentos[0] }
            Mandar-Linha $fluxo "LIST $caminho"
            $m = Esperar $fluxo '^ENTRADAS (\d+)$'
            $n = [int]$m[1]
            $entradas = @()
            for ($i = 0; $i -lt $n; $i++) {
                $e = Esperar $fluxo '^([APX]) (\d+) (.*)$'
                $tipo = switch ($e[1]) { 'P' { 'pasta' } 'X' { 'programa' } default { 'arquivo' } }
                $entradas += [pscustomobject]@{
                    Nome    = $e[3]
                    Tipo    = $tipo
                    Tamanho = [int]$e[2]
                }
            }
            $entradas | Format-Table -AutoSize
        }

        { $_ -in 'info', 'i' } {
            if (-not $Argumentos) { throw 'uso: info <caminho>' }
            Mandar-Linha $fluxo "INFO $($Argumentos[0])"
            $m = Esperar $fluxo '^TIPO ([APX]) (\d+)$'
            $tipo = switch ($m[1]) { 'P' { 'pasta' } 'X' { 'programa' } default { 'arquivo' } }
            [pscustomobject]@{ Caminho = $Argumentos[0]; Tipo = $tipo; Tamanho = [int]$m[2] } | Format-Table -AutoSize
        }

        { $_ -in 'baixar', 'get' } {
            if (-not $Argumentos) { throw 'uso: baixar <remoto> [local]' }
            $remoto = $Argumentos[0]
            $local = if ($Argumentos.Count -ge 2) { $Argumentos[1] } else { Split-Path -Leaf ($remoto -replace '[\\/]', '_') }
            Mandar-Linha $fluxo "GET $remoto"
            $m = Esperar $fluxo '^DADOS (\d+)$'
            $n = [int]$m[1]
            $dados = Ler-Exato $fluxo $n
            [System.IO.File]::WriteAllBytes((Join-Path (Get-Location) $local), $dados)
            Write-Host "sagui-fs: $remoto -> $local ($n bytes)"
        }

        { $_ -in 'enviar', 'put' } {
            if ($Argumentos.Count -lt 2) { throw 'uso: enviar <local> <remoto>' }
            $local, $remoto = $Argumentos[0], $Argumentos[1]
            if (-not (Test-Path -LiteralPath $local)) { throw "arquivo não encontrado: $local" }
            $dados = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $local))
            if ($dados.Length -gt 8MB) { throw "o servidor aceita até 8 MiB (o arquivo tem $($dados.Length) bytes)" }
            Mandar-Linha $fluxo "PUT $remoto $($dados.Length)"
            $null = Esperar $fluxo '^PRONTO$'
            $fluxo.Write($dados, 0, $dados.Length)
            $fluxo.Flush()
            $null = Esperar $fluxo '^OK$'
            Write-Host "sagui-fs: $local -> $remoto ($($dados.Length) bytes)"
        }

        { $_ -in 'apagar', 'rm' } {
            if (-not $Argumentos) { throw 'uso: apagar <caminho>' }
            Mandar-Linha $fluxo "RM $($Argumentos[0])"
            $null = Esperar $fluxo '^OK$'
            Write-Host "sagui-fs: apagado $($Argumentos[0])"
        }

        { $_ -in 'criar-pasta', 'mkdir' } {
            if (-not $Argumentos) { throw 'uso: criar-pasta <caminho>' }
            Mandar-Linha $fluxo "MKDIR $($Argumentos[0])"
            $null = Esperar $fluxo '^OK$'
            Write-Host "sagui-fs: criada $($Argumentos[0])"
        }

        { $_ -in 'remover-pasta', 'rmdir' } {
            if (-not $Argumentos) { throw 'uso: remover-pasta <caminho>' }
            Mandar-Linha $fluxo "RMDIR $($Argumentos[0])"
            $null = Esperar $fluxo '^OK$'
            Write-Host "sagui-fs: removida $($Argumentos[0])"
        }

        { $_ -in 'renomear', 'ren' } {
            if ($Argumentos.Count -lt 2) { throw 'uso: renomear <origem> <destino> (origem sem espaços)' }
            if ($Argumentos[0] -match ' ') { throw 'a origem não pode ter espaços (limite do protocolo)' }
            Mandar-Linha $fluxo "REN $($Argumentos[0]) $($Argumentos[1])"
            $null = Esperar $fluxo '^OK$'
            Write-Host "sagui-fs: renomeado para $($Argumentos[1])"
        }

        default {
            Write-Host "sagui-fs: comando desconhecido: $Comando" -ForegroundColor Red
            Write-Host $uso
            Sair-Do-Servidor $fluxo
            exit 2
        }
    }

    Sair-Do-Servidor $fluxo
} catch {
    Write-Host "sagui-fs: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
} finally {
    if ($fluxo) { $fluxo.Dispose() }
    $cliente.Dispose()
}
