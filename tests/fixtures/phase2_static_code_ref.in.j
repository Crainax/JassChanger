struct Tick
    static method run takes nothing returns nothing
    endmethod
endstruct

function Test takes trigger t returns nothing
    call TriggerAddAction(t, function Tick.run)
endfunction
