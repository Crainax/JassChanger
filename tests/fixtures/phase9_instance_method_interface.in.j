function interface NodeHandler takes Node node, integer value returns nothing

struct Node
    integer value

    method setValue takes integer value returns nothing
        set .value = value
    endmethod

    static method Test takes nothing returns nothing
        local NodeHandler handler = Node.setValue
        local Node node = Node.create()
        call handler.execute(node, 7)
    endmethod
endstruct
