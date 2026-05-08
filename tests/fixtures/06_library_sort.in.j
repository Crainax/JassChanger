library B requires A
function BFunc takes nothing returns nothing
    call AFunc()
endfunction
endlibrary

library A
function AFunc takes nothing returns nothing
endfunction
endlibrary
