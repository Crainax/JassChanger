//! zinc
library L {
    function Test(integer x) -> integer {
        if (x > 0) return x; else if (x < 0) return -x; else return 0;
    }
}
//! endzinc
