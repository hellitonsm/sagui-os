-- Leitura e escrita de arquivos, e dofile.

local f = assert(io.open("/lua/saida.txt", "w"))
f:write("linha 1\n", "linha 2\n")
f:close()

local g = assert(io.open("/lua/saida.txt", "r"))
local a = g:read("l")
local b = g:read("l")
g:close()
print("arquivo:", a, b)

for l in io.lines("/lua/saida.txt") do
    print("linha:", l)
end

dofile("/lua/dofile.lua")
