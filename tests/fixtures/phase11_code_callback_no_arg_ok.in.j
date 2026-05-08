//! zinc
library Phase11CodeOk {
    public function Test(trigger t) {
        TriggerAddAction(t, function() {
            BJDebugMsg("ok");
        });
    }
}
//! endzinc
