-- Um passeio pelas funcoes, tabelas e bibliotecas do interpretador.

local function fatorial(n)
    if n <= 1 then return 1 end
    return n * fatorial(n - 1)
end
print("fatorial(6) =", fatorial(6))

local function soma(...)
    local s = 0
    for _, v in ipairs({...}) do s = s + v end
    return s
end
print("soma(1,2,3,4) =", soma(1, 2, 3, 4))

-- Fechamento: a funcao interna guarda a variavel local da externa.
local function contador()
    local n = 0
    return function() n = n + 1 return n end
end
local c = contador()
print("closure:", c(), c(), c())

local t = {10, 20, 30}
t.nome = "sagui"
t[4] = 40
print("t[2] =", t[2], "nome =", t.nome, "#t =", #t)

table.insert(t, 2, 15)
print("insert:", table.concat(t, ","))
table.remove(t, 1)
table.sort(t)
print("sort:", table.concat(t, ","))

print("string:", string.upper("sagui"), string.sub("SaguiOS", 1, 5))
print("format:", string.format("%s=%03d", "x", 7))
print("find:", string.find("abcabc", "ca"))
print("math:", math.max(3, 9, 2), math.min(3, 9, 2), math.abs(-5))

local pares = 0
for i = 1, 20 do
    if i % 2 == 0 then pares = pares + 1 end
end
print("pares:", pares)

local n = 0
repeat
    n = n + 1
until n == 3
print("repeat:", n)

local w = 0
while w < 5 do
    w = w + 2
    if w == 4 then break end
end
print("while/break:", w)

local d = {a = 1, b = 2}
local chaves = ""
for k, v in pairs(d) do chaves = chaves .. k .. v end
print("pairs:", chaves)

local a, b = (function() return 1, 2 end)()
print("multiplos:", a, b)

print("tostring:", tostring(42), type({}), type("x"), type(nil))

-- Ligacao com o sistema: escreve no log (serial) sem sujar a tela.
sagui.registrar("[LUA] extensao sagui chamada do script")
