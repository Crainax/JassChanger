//! zinc
library Demo {
    public struct unitAttrObserver [] {
        public static unit argsU = null;
        public static trigger attackIntervalCB = null;
        public static method registerAttackInterval (code func) {
            TriggerAddCondition(attackIntervalCB, Condition(func));
        }
    }
}
//! endzinc
