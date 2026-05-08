//! zinc
library ZA {}

library ZB {
    function Test() {
        static if (LIBRARY_ZA) { BJDebugMsg("zinc"); }
        static if (LIBRARY_Missing) {
            function interface Bad takes nothing returns nothing
        } else {
            BJDebugMsg("fallback");
        }
    }
}
//! endzinc

