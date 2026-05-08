module M1
    integer x
endmodule

module M2
    implement M1
    method foo takes nothing returns nothing
        set .x = .x + 1
    endmethod
endmodule

struct A
    implement M2
endstruct

