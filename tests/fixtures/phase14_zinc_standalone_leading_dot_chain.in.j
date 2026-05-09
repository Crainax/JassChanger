//! zinc
library Phase14StandaloneLeadingDotChain {
    public struct Button {
        integer ui;
        static method create(integer ui) -> thistype {
            thistype this;
            this = thistype.allocate();
            this.ui = ui;
            return this;
        }
        method setText(string text) -> thistype {
            return this;
        }
        method show(boolean flag) -> thistype {
            return this;
        }
    }

    function Test() {
        Button.create(1)
            .setText("ok")
            .show(true);
    }
}
//! endzinc
