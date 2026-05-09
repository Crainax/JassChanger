struct Phase15Counter
    integer value

    method bump takes nothing returns integer
        set value = value + 1
        return value
    endmethod
endstruct
