//! textmacro MakeFunc takes NAME, VALUE
function $NAME$ takes nothing returns integer
    return $VALUE$
endfunction
//! endtextmacro

//! runtextmacro MakeFunc("GetA", "1")
//! runtextmacro MakeFunc("GetB", "2")
