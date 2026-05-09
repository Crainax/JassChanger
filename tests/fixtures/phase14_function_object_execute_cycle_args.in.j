//! zinc
library Phase14FunctionObjectExecuteCycleArgs {
    public function TE(player p, unit u, integer a, real b) -> nothing {
        TD.execute(p, u, a, b);
    }

    public function TC(player p, unit u, integer a, real b) -> nothing {
        TD.execute(p, u, a, b);
    }

    public function TD(player p, unit u, integer a, real b) -> nothing {
        TC.execute(p, u, a, b);
    }
}
//! endzinc
