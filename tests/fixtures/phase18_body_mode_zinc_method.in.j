//! zinc
library Phase18ZincMethod {
    struct Box {
        integer value;

        method add(integer x) {
            this.value += x;
        }
    }
}
//! endzinc
