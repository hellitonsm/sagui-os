-- require busca bibliotecas embutidas, nao arquivos externos.
local texto = require("string")
local matematica = require("math")
local sistema = require("sagui")
print(texto.upper("sagui"), matematica.max(3, 9))
assert(require("math") == matematica)
sistema.registrar("[LUA] require embutido")
