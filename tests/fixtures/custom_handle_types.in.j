type rootgroup extends agent

library GroupConsumer requires GroupTypes
    globals
        dzeffectgroup lastGroup = null
        dzeffectgroup array groups
    endglobals
    native DzEffectGroupCreate takes nothing returns dzeffectgroup
    native DzEffectGroupDestroy takes dzeffectgroup g returns boolean
endlibrary

library GroupTypes
    scope Types
        type dzeffectgroup extends rootgroup
    endscope
endlibrary

function main takes nothing returns nothing
    local dzeffectgroup g = null
    set g = DzEffectGroupCreate()
    set groups[0] = g
    call BJDebugMsg(I2S(GetHandleId(g)))
    call DzEffectGroupDestroy(g)
endfunction
