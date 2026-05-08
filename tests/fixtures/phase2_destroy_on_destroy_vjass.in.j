struct Box
    unit u

    method onDestroy takes nothing returns nothing
        set this.u = null
    endmethod
endstruct

function Test takes nothing returns nothing
    local Box b = Box.create()
    call b.destroy()
endfunction
