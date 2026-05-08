function interface Printer takes integer x returns nothing

struct S
    static method Print takes integer x returns nothing
        call BJDebugMsg(I2S(x))
    endmethod

    static method Test takes nothing returns nothing
        local Printer p = S.Print
        call p.execute(5)
    endmethod
endstruct
