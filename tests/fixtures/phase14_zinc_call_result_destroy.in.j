//! zinc
library Phase14CallResultDestroy {
    public struct Box {
        integer value;

        static method make(integer value) -> thistype {
            thistype this = thistype.allocate();
            this.value = value;
            return this;
        }
    }

    public function MakeBox(integer value) -> Box {
        return Box.make(value);
    }

    public function Test() {
        MakeBox(7).destroy();
    }
}
//! endzinc
