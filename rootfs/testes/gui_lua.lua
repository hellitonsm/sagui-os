-- gui_lua.lua - fumaca do kit grafico a partir do Lua, para os testes.
-- Abre uma janela, cria um botao e um rotulo, fecha e imprime o resultado.
-- Nao espera interacao: roda sozinho no boot de teste.
local j = gui.janela("Smoke", 140, 90)
assert(j, "janela nao abriu")
local b = gui.botao(j, 14, 12, 110, 26, "OK")
assert(b, "botao nao criado")
gui.texto(j, 14, 48, "gui ok", gui.COR_TEXTO)
gui.mostrar(j)
gui.fechar(j)
print("gui lua ok")
