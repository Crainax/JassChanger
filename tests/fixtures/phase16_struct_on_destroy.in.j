struct Token
    integer value

    method onDestroy takes nothing returns nothing
        set value = 0
endmethod
endstruct

function KillToken takes nothing returns nothing
    local Token token = Token.create()
    call token.destroy()
endfunction
