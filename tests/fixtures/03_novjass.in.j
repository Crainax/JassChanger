function Verify takes nothing returns nothing
    local boolean b = true
//! novjass
    set b = false
//! endnovjass
    if b then
        call BJDebugMsg("vjass")
    endif
endfunction
