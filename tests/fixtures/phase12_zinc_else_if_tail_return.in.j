//! zinc
library Phase12ElseIfTailReturn {
    public function Pick(integer value) -> integer {
        if (value == 1) {
            return 10;
        } else if (value == 2) {
            return 20;
        } else {
            return 30;
        }
    }
}
//! endzinc
