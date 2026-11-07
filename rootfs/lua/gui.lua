-- gui.lua - demonstracao do kit grafico do SaguiOS rodando no Lua nativo.
-- Abra o Terminal e rode com:  lua /lua/gui.lua
-- Feche a janela pelo X para sair.
local j = gui.janela("Kit grafico do SaguiOS", 420, 300)
if not j then
    print("nao foi possivel abrir a janela")
    return
end
gui.rotulo(j, 16, 12, "Janela, rotulo, botao, caixa e lista.", gui.COR_TEXTO)

local cliques = 0
local botao = gui.botao(j, 16, 40, 150, 28, "Clique aqui")
gui.rotulo(j, 182, 46, "cliques: 0", gui.COR_TEXTO_FRACO)

local caixa = gui.caixa(j, 16, 84, 260, "Digite algo")
local lista = gui.lista(j, 16, 128, 220, 128,
    { "SaguiOS", "Lua nativo", "Kit grafico", "Sagui 3D", "Pendrive" })
gui.mostrar(j)
sagui.registrar("gui.lua: pronto")

while true do
    local e = gui.evento(true)
    if e.tipo == gui.EV_FECHAR then break end

    if gui.botao_clicado(botao, e) then
        cliques = cliques + 1
        gui.retangulo(j, 182, 46, 160, 16, gui.COR_FUNDO)
        gui.rotulo(j, 182, 46, "cliques: " .. cliques, gui.COR_TEXTO_FRACO)
        gui.mostrar_area(j, 182, 46, 160, 16)
        sagui.registrar("gui.lua: botao clicado " .. cliques)
    end

    if gui.caixa_tratar(caixa, e) then
        sagui.registrar("gui.lua: caixa enter: " .. gui.caixa_texto(caixa))
    end

    local r = gui.lista_tratar(lista, e)
    if r == gui.LISTA_ATIVOU then
        gui.mensagem("Lista", "Voce ativou o item " .. gui.lista_selecao(lista) .. ".")
    end
end
gui.fechar(j)
