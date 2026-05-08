//! zinc
library Phase6TypeId {
    public struct Entry [] {
        integer value;
        static integer seen;
        static method bind(integer index) {
            thistype[index].value = thistype.typeid;
            seen = thistype[index].value;
        }
    }
}
//! endzinc
