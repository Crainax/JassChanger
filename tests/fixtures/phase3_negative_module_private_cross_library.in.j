library A
    private module M
        integer x
    endmodule
endlibrary

library B requires A
    struct S
        implement M
    endstruct
endlibrary

