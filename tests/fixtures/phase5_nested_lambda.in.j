//! zinc
library L {
    function Test() {
        group g = CreateGroup();
        ForGroup(g, function() {
            TimerStart(CreateTimer(), 1.0, false, function() {
                BJDebugMsg("nested");
            });
        });
    }
}
//! endzinc
