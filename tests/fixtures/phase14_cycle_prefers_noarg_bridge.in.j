function RegisterSync takes string tag, code cb returns nothing
    if false then
        call InitSync()
    endif
endfunction

function InitSync takes nothing returns nothing
    call RegisterSync("OOS", function OnSync)
endfunction

function OnSync takes nothing returns boolean
    return true
endfunction
