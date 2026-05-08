library Phase9NestedReceiver
    struct Node
        integer value

        static method make takes integer value returns thistype
            local thistype this = thistype.create()
            set .value = value
            return this
        endmethod

        method get takes nothing returns integer
            return .value
        endmethod
    endstruct

    function TakeValue takes integer value returns integer
        return value + 1
    endfunction

    function Test takes nothing returns integer
        return TakeValue(Node.make(8).get())
    endfunction
endlibrary
