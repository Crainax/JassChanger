//! zinc
library Phase6Range {
    function Sum(integer n) -> integer {
        integer i;
        integer total;
        total = 0;
        for (1 <= i <= n) {
            total += i;
        }
        return total;
    }
}
//! endzinc
