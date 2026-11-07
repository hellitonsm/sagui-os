package main

// Compilado dentro do SaguiOS, sem ferramentas do hospedeiro.
// Rode com:  gosagui build -o /go/sobre /go/sobre.go
//            /go/sobre
import "fmt"
import s "sagui"

func main() {
    fmt.Println("SaguiOS: um sistema de 32 bits escrito do zero")
    fmt.Println("Kernel, shell, Lua e Go nativos, tudo no proprio sistema.")
    s.Registrar("[GO] sobre.go executado")
}
