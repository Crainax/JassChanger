//! zinc
library L {
    function Helper() {
        BJDebugMsg("helper");
    }

    function Test(trigger t) {
        TriggerAddAction(t, function() {
            Helper();
        });
    }
}
//! endzinc
