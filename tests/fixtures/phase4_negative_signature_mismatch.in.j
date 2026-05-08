function interface F takes integer x returns integer

function Bad takes real x returns integer
    return 0
endfunction

function Test takes nothing returns nothing
    local F f = F.Bad
endfunction
