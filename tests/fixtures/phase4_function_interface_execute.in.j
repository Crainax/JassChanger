function interface Action takes unit u returns nothing

function KillTarget takes unit u returns nothing
    call KillUnit(u)
endfunction

function Test takes unit u returns nothing
    local Action a = Action.KillTarget
    call a.execute(u)
endfunction
