library A
    globals
        public integer X = 1
        private integer Y = 2
    endglobals

    public function Foo takes nothing returns nothing
        set X = X + 1
        set Y = Y + 1
    endfunction

    private function Bar takes nothing returns nothing
    endfunction
endlibrary
