//! zinc
library Phase14FunctionObjectEvaluateArgs {
    struct keyshop[] {
        static unit playerU[];
    }

    function RefreshProduct(unit shopUnit, integer pos) -> boolean {
        return shopUnit != null && pos > 0;
    }

    public function Test(integer pid, integer idx) {
        RefreshProduct.evaluate(keyshop.playerU[pid], idx);
    }
}
//! endzinc
