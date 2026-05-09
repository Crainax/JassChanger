struct Phase15Shadow
    integer value

    method keepLocal takes integer value returns integer
        local integer next = value + 1
        return next
    endmethod
endstruct
