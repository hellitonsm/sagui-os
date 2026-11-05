package main

import "fmt"
import "sagui"

// O bootstrap executa init() antes de main().
func init() {
    print("[init] ")
}

func main() {
    fmt.Print("Ola, ")
    fmt.Println("bibliotecas!")
    fmt.Println("com", "importacao")
    sagui.Registrar("[GO] biblioteca importada")
}
