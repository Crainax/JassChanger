globals
    integer array paramI
    integer array a
    integer array b
    integer array c
endglobals

//! zinc
library Phase23ArrayStructNestedIndexReceiver {
    public struct music[] {
        integer value;

        method play() {
            value = value + 1;
        }

        method playFor(integer volume) {
            value = volume;
        }
    }

    public function Test() {
        music[paramI[1]].play();
        music[a[b[c[paramI[2]]]]].playFor(paramI[a[b[3]]]);
        music[1006].value = music[paramI[1]].value + music[a[b[c[paramI[2]]]]].value;
    }
}
//! endzinc
