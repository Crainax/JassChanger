//! zinc
library Phase12LambdaDefaultReturn {
    function interface Check takes integer value returns boolean

    public function Test() {
        Check check;
        check = function(integer value) -> boolean {
            if (value > 0) {
                return true;
            }
        };
    }
}
//! endzinc
