//! zinc
library Demo {
    public struct Bag {
        integer items[10];
        method setItem(integer i, integer v) {
            this.items[i] = v;
        }
    }
}
//! endzinc
