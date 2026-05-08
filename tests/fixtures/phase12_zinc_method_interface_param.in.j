//! zinc
library Phase12MethodInterfaceParam {
    public type OnPick extends function(player, integer, Picker);

    public struct Picker {
        private OnPick cb;

        public static method create(player p, OnPick cb) -> thistype {
            thistype this = thistype.allocate();
            this.cb = cb;
            return this;
        }

        public method fire(player p, integer value) {
            this.cb.execute(p, value, this);
        }
    }

    public function Test(player p) {
        Picker.create(p, function(player p2, integer value, Picker picker) {
            BJDebugMsg(I2S(value));
        });
    }
}
//! endzinc
