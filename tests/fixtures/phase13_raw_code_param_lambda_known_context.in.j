//! zinc
library Phase13RawIface {
    public function interface AfterBuffTime takes timer t, unit u returns nothing

    public function CreateBuff(unit u, AfterBuffTime cb) -> timer {
        return null;
    }
}

library Phase13RawWrapper requires Phase13RawIface {
    public function Wrap(unit u, AfterBuffTime cb) -> timer {
        return CreateBuff(u, cb);
    }
}

library Phase13RawUse requires Phase13RawWrapper {
    public function Test(unit u) {
        Wrap(u, function(timer t, unit u2) {
            BJDebugMsg("ok");
        });
    }
}
//! endzinc
