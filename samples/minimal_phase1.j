library Hello initializer InitHello
globals
    private integer Count = 0
endglobals

private function InitHello takes nothing returns nothing
    set Count = Count + 1
endfunction
endlibrary

//! zinc
library Math {
    function Add(integer a, integer b) -> integer {
        integer c = a + b;
        return c;
    }
}
//! endzinc

function main takes nothing returns nothing
    call InitBlizzard()
endfunction
