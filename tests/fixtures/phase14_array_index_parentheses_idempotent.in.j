globals
    integer array Phase14ParenArray
endglobals

function Phase14ArrayIndexParen takes nothing returns nothing
    call BJDebugMsg(I2S(Phase14ParenArray[(2)]))
endfunction
