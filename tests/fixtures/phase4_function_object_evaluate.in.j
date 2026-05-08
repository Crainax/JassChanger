function Inc takes integer x returns integer
    return x + 1
endfunction

function Test takes nothing returns integer
    return Inc.evaluate(1)
endfunction
