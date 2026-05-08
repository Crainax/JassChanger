//! zinc
library Phase12ArrayStructReceiver {
    public struct Row[] {
        integer value;
    }

    public function Test(integer pos) -> integer {
        Row row;
        row = Row[pos];
        row.value = 7;
        return Row[pos].value + row.value;
    }
}
//! endzinc
