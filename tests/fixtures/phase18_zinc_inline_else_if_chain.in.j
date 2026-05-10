//! zinc
library Phase18InlineElseIfChain {
    public function Pick(integer pj) -> integer {
        integer cA; integer cB; integer cC;
        cA = 0; cB = 0; cC = 0;
        if (pj == 1) { cA += 1; }
        else if (pj == 2) { cB += 1; }
        else if (pj == 3) { cC += 1; }
        else { cC += 2; }
        return cA + cB + cC;
    }
}
//! endzinc
