//! zinc
library Demo {
    private integer grid[2][3];
    public function Register(trigger t) {
        TriggerAddAction(t, function () {
            grid[1][2] = 7;
        });
    }
}
//! endzinc
