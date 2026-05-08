//! zinc
library L {
    function Test(integer x) -> integer {
        integer y = 0;
        if (x > 0) y += x;
        return y;
    }
}
//! endzinc
