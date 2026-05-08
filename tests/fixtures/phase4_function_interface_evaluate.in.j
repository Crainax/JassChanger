function interface RealFunc takes real x returns real

function Double takes real x returns real
    return x * 2.0
endfunction

function Test takes real x returns real
    local RealFunc f = RealFunc.Double
    return f.evaluate(x)
endfunction
