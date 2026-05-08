function interface BoolCheck takes integer x returns boolean

function IsPositive takes integer x returns boolean
    return x > 0
endfunction

function Test takes integer x returns boolean
    local BoolCheck check = BoolCheck.IsPositive
    return check.evaluate(x)
endfunction
