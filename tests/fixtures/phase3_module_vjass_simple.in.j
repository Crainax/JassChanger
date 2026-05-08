module M
    integer x
    method foo takes nothing returns nothing
        call BJDebugMsg("foo")
    endmethod
endmodule

struct A
    implement M
endstruct

function Test takes nothing returns nothing
    local A a = A.create()
    set a.x = 1
    call a.foo()
endfunction

