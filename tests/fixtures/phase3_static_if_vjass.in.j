library A
endlibrary

function Test takes nothing returns nothing
    static if LIBRARY_A and not LIBRARY_Missing then
        call BJDebugMsg("A")
    elseif DEBUG_MODE then
        call BJDebugMsg("debug")
    else
        call BJDebugMsg("fallback")
    endif
endfunction

