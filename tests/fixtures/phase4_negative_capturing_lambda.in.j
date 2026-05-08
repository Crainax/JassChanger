//! zinc
library L {
    function Test() {
        integer base = 1;
        code c = function() {
            BJDebugMsg(I2S(base));
        };
    }
}
//! endzinc
