-- relogio.lua - relogio de uptime numa janela do SaguiOS.
-- Rode com:  lua /lua/relogio.lua
-- A cada meio segundo o texto e atualizado; feche pelo X para sair.
local j = gui.janela("Relogio do SaguiOS", 280, 110)
if not j then
    print("nao foi possivel abrir a janela")
    return
end
gui.texto(j, 16, 14, "Tempo ligado:", gui.COR_TEXTO_FRACO)
while true do
    local e = gui.evento(false)
    if e and e.tipo == gui.EV_FECHAR then break end
    local s = sagui.tempo() // 1000
    gui.retangulo(j, 16, 40, 248, 24, gui.COR_FUNDO)
    gui.texto(j, 16, 44, s .. " segundos", gui.COR_TEXTO)
    gui.mostrar_area(j, 16, 40, 248, 24)
    sagui.dormir(500)
end
gui.fechar(j)
