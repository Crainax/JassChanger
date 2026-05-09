//! zinc
library Phase14FunctionObjectEvaluateCycleArgs {
    public function TF(player p, unit u, integer a, real b) -> nothing {
        TB.execute(p, u, a, b);
    }

    public function TA(player p, unit u, integer a, real b) -> nothing {
        TB.evaluate(p, u, a, b);
    }

    public function TB(player p, unit u, integer a, real b) -> nothing {
        TA.evaluate(p, u, a, b);
    }
}
//! endzinc
