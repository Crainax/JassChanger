//! zinc
library Demo {
    struct Node {
        integer value;
    }
    private Node nodes[2][3];
    private integer fixed[10];
    public function Get(integer i, integer j) -> integer {
        nodes[i][j] = Node.create();
        fixed[i] = nodes[i][j];
        return fixed[i];
    }
}
//! endzinc
