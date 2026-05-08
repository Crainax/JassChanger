function interface IntProvider takes nothing returns integer

function FortyTwo takes nothing returns integer
    return 42
endfunction

function Test takes nothing returns integer
    local IntProvider provider = IntProvider.FortyTwo
    return provider.evaluate()
endfunction
