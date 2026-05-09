//! zinc
library Phase14FunctionObjectEvaluateCycle {
    public function TA() -> nothing {
        TB.evaluate();
    }

    public function TB() -> nothing {
        TA.evaluate();
    }
}
//! endzinc
