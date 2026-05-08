//! zinc
library L {
    function interface Pred takes integer x returns boolean

    function Test() {
        Pred p = function(integer x) -> boolean {
            return x > 0;
        };
    }
}
//! endzinc
