struct Slot
    method release takes nothing returns nothing
        call deallocate()
    endmethod
endstruct
