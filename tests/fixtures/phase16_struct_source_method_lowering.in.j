struct Counter
    integer value
    static integer total

    method bump takes integer delta returns integer
        local integer localValue = delta
        set value = value + localValue
        set thistype.total = thistype.total + 1
        return value
    endmethod
endstruct
