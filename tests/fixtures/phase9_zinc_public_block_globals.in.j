//! zinc
library Phase9PublicBlock {
    public {
        integer HASH_TIMER = 1;
        integer HASH_ABILITY = 2;
    }

    function Test() -> integer {
        return HASH_TIMER + HASH_ABILITY;
    }
}
//! endzinc
