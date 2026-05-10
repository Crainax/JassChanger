//! zinc
library Phase18ElseIfAfterComment {
    public function Pick(string cmd) -> integer {
        integer result = 0;
        if (cmd == "atk") {
            result = 1;
        }
        // command group separator
        else if (cmd == "def") {
            result = 2;
        } else {
            result = 3;
        }
        return result;
    }
}
//! endzinc
