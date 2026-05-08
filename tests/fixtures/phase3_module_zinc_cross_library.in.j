//! zinc
library ModuleLib {
    public module M {
        integer x;
        method foo() {
            BJDebugMsg("foo");
        }
    }
}

library UseLib requires ModuleLib {
    public struct A {
        module M;
    }

    function Test() {
        A a = A.create();
        a.foo();
    }
}
//! endzinc

