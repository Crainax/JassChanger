//! zinc
library Phase12StructDeallocate {
    public struct Node {
        public method release() {
            this.deallocate();
        }
    }
}
//! endzinc
