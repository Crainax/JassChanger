struct Counter
    static integer value = 0

    static method add takes integer x returns nothing
        set value = value + x
    endmethod
endstruct
