//! zinc
library Demo {
    /*
    integer leaked[3][4];
    function bad() {}
    */
    string text = "/* not a comment */";
    integer raw = 'A000';
    public function Use() {
        BJDebugMsg(text + I2S(raw));
    }
}
//! endzinc
