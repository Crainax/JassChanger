//! zinc
library Demo {
    struct Bag {
        integer value;
        integer items[4];
        method reset() {
            value = 0;
        }
        method clear(integer i) {
            reset();
            items[i] = value;
        }
    }
}
//! endzinc
