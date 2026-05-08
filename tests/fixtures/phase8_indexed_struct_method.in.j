library L
    struct Node
        integer value

        method get takes nothing returns integer
            return .value
        endmethod
    endstruct

    function Test takes nothing returns integer
        local Node array nodes
        set nodes[1] = Node.create()
        return nodes[1].get()
    endfunction
endlibrary
