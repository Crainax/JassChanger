function interface Phase10F takes integer x returns nothing

function Phase10Target takes integer x returns nothing
endfunction

//! zinc
library Phase10Continuations {
    public struct Builder {
        string text;

        static method create() -> thistype {
            thistype this = thistype.allocate();
            return this;
        }

        method append(string left, string right) -> thistype {
            this.text = left + right;
            return this;
        }

        method value() -> string {
            return this.text;
        }
    }

    public function Use(Phase10F f) {
        f.execute(3);
    }

    public function Test() -> string {
        Use(Phase10F.Phase10Target);
        return Builder.create().append(
            "a",
            "b").value()
            + "-"
            + I2S(3);
    }
}
//! endzinc
