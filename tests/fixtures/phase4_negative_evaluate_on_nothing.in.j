function interface F takes nothing returns nothing

function A takes nothing returns nothing
endfunction

function Test takes nothing returns integer
    local F f = F.A
    return f.evaluate()
endfunction
