# IDE do SaguiOS

No Terminal, abra `ide` ou `ide /go/ola.go`. Os botões **Exemplo Go** e
**Exemplo Lua** carregam modelos editáveis; salve-os com extensão `.go` ou
`.lua` (por exemplo, `/tmp/meu.go`). **Executar** salva as alterações antes
 de executar, e pede um nome para arquivos novos. **Parar** interrompe o
processo em andamento. A saída e os erros aparecem no painel inferior; o
código de saída é exibido ao final. Ctrl+S salva. Há mais exemplos em `/go`
e `/lua`; use Abrir para carregá-los.

Go é compilado localmente pelo `gosagui` para um ELF temporário em `/tmp`,
executado somente após compilação bem-sucedida e removido ao terminar. Lua é
interpretado localmente por `/bin/lua`: não produz executável. Ambas são
implementações de subconjuntos, não as ferramentas oficiais; veja
[Go](go.md) e [Lua](lua.md) para sintaxe e limites. A IDE é um editor simples,
sem realce de sintaxe ou depurador; o painel mostra apenas as últimas linhas,
não aceita entrada interativa, e é indicado para programas não interativos.
O texto do editor usa Latin-1; prefira ASCII em código Go. Arquivos no SGFS e
em `/tmp` não persistem após reiniciar; use um volume FAT32 para persistência.
