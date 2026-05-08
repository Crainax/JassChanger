function Test takes nothing returns nothing
    static if DEBUG_MODE then
        call BJDebugMsg("debug")
    else
        call BJDebugMsg("release")
    endif
endfunction

