struct Point
    real x
    real y

    method move takes real nx, real ny returns nothing
        set this.x = nx
        set this.y = ny
    endmethod
endstruct
