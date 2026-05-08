struct Point
    real x
    real y
endstruct

function Test takes nothing returns nothing
    local Point p = Point.create()
    set p.x = 1.0
    set p.y = 2.0
endfunction
