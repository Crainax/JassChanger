//! zinc
library L {
    function Accept(code first, code second) {
    }

    function Test() {
        Accept(function() {
            BJDebugMsg("first");
        }, function() {
            BJDebugMsg("second");
        });
    }
}
//! endzinc
