function Test takes nothing returns nothing
    static if LIBRARY_A then
        call BJDebugMsg("bad")
endfunction

