native TakesCode takes code c returns nothing

//! zinc
library Phase11CodeParam {
    public function Test() {
        TakesCode(function(integer value) {
            BJDebugMsg(I2S(value));
        });
    }
}
//! endzinc
