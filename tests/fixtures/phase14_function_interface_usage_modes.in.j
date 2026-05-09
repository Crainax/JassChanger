//! zinc
library Phase14FunctionInterfaceUsageModes {
    type II extends function(unit, real, real);
    type III extends function(player, real, real) -> integer;
    type IV extends function();
    type IIV extends function(integer);

    public function ImpleII1(unit caster, real x, real y) {
        BJDebugMsg("II1");
    }

    public function ImpleII2(unit caster, real x, real y) {
        BJDebugMsg("II2");
    }

    public function ImpleIII1(player p, real x, real y) -> integer {
        return 1;
    }

    public function ImpleIII2(player p, real x, real y) -> integer {
        return 2;
    }

    public function ImpleIV1() {
        BJDebugMsg("IV1");
    }

    public function ImpleIV2() {
        BJDebugMsg("IV2");
    }

    public function ImpleIIV1(integer i) {
        BJDebugMsg(I2S(i));
    }

    public function ImpleIIV2(integer i) {
        BJDebugMsg(I2S(i));
    }

    public function Test(player p) {
        II a = II.ImpleII1;
        II b = II.ImpleII2;
        III c = III.ImpleIII1;
        III d = III.ImpleIII2;
        IV e = IV.ImpleIV1;
        IV f = IV.ImpleIV2;
        IIV g = IIV.ImpleIIV1;
        IIV h = IIV.ImpleIIV2;
        integer ggg;

        a.execute(null, 0, 0);
        ggg = c.evaluate(p, 0, 0);
        e.execute();
        f.evaluate();
    }
}
//! endzinc
