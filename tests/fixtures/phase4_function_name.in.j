library L
    public function Foo takes nothing returns nothing
    endfunction

    function Test takes nothing returns nothing
        call ExecuteFunc(Foo.name)
    endfunction
endlibrary
