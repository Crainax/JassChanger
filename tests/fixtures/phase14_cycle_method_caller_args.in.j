function Phase14CycleArgA takes integer x, unit u returns nothing
    call Phase14CycleArgB(x, u)
endfunction

function Phase14CycleArgB takes integer x, unit u returns nothing
    call Phase14CycleArgA(x, u)
endfunction
