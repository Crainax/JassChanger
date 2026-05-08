//! zinc
library Demo {
    integer X = 0;

    function Add(integer a, integer b) -> integer {
        integer c = a + b;
        return c;
    }

    function Run() {
        integer i = 0;
        while (i < 3) {
            i += 1;
        }
        if (i == 3) {
            BJDebugMsg("ok");
        } else {
            BJDebugMsg("bad");
        }
    }
}
//! endzinc
