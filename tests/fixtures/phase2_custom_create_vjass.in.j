struct Node
    integer v

    static method create takes integer x returns thistype
        local thistype this = thistype.allocate()
        set this.v = x
        return this
    endmethod
endstruct
