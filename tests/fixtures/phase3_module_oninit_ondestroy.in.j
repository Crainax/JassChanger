module Lifecycle
    static method onInit takes nothing returns nothing
        call BJDebugMsg("module init")
    endmethod
    method onDestroy takes nothing returns nothing
        call BJDebugMsg("module destroy")
    endmethod
endmodule

struct A
    implement Lifecycle
    method onDestroy takes nothing returns nothing
        call BJDebugMsg("struct destroy")
    endmethod
endstruct

