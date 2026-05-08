library L
    struct Node
        integer value
    endstruct

    function Test takes nothing returns integer
        local Node array nodes
        set nodes[1] = Node.create()
        set nodes[1].value = 7
        return nodes[1].value
    endfunction
endlibrary
