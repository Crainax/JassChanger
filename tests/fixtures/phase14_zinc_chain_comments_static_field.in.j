//! zinc
library Phase14ChainComments {
    public struct Icon [] {
        static thistype icons[];

        static method create() -> thistype {
            return 1;
        }

        method setSize(integer value) -> thistype {
            return this;
        }

        method setPadding(integer value) -> thistype {
            return this;
        }

        method setPoint(integer value) -> thistype {
            return this;
        }

        method setTexture(string value) -> thistype {
            return this;
        }

        static method test() {
            integer i = 1;
            icons[i] = Icon.create()
                .setSize(1)
            // .setIgnored(0)
                .setPadding(2)
                .setPoint(3)
                .setTexture("x");
        }
    }
}
//! endzinc
