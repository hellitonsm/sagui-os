package main

import (
    "fmt"
    registro "sagui/log"
)

func main() {
    fmt.Println("Ola da biblioteca fmt!")
    registro.Print("aviso: ")
    registro.Println("biblioteca", "embutida")
}
