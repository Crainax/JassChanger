library Phase9Receiver
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

    function MakeNode takes integer value returns Node
        return Node.make(value)
    endfunction

    function Test takes nothing returns integer
        return MakeNode(7).get()
    endfunction
endlibrary
