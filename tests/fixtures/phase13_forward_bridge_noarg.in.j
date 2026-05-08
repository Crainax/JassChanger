function Phase13ForwardA takes nothing returns nothing
    call Phase13ForwardB()
endfunction

function Phase13ForwardB takes nothing returns nothing
    call Phase13ForwardA()
endfunction
