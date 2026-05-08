function interface F takes integer x returns integer

function Inc takes integer x returns integer
    return x + 1
endfunction

function Apply takes integer x, F f returns integer
    return f.evaluate(x)
endfunction

function Test takes nothing returns integer
    return Apply(1, Inc)
endfunction
