//! zinc
library L {
    function Test(trigger t) {
        TriggerAddAction(t, function() {
            BJDebugMsg("x");
        });
    }
}
//! endzinc
