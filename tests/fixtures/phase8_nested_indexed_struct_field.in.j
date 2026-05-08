library L
    struct Inner
        integer value
    endstruct

    struct Outer
        Inner child
    endstruct

    function Test takes nothing returns integer
        local Outer array nodes
        set nodes[1] = Outer.create()
        set nodes[1].child = Inner.create()
        set nodes[1].child.value = 9
        return nodes[1].child.value
    endfunction
endlibrary
