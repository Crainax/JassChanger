function Test takes nothing returns nothing
    static if (LIBRARY_A and) then
        call BJDebugMsg("bad")
    endif
endfunction

