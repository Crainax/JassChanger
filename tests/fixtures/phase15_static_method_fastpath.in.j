struct Phase15Static
    static method ping takes nothing returns integer
        return 7
    endmethod

    method callPing takes nothing returns integer
        return ping() + thistype.ping()
    endmethod
endstruct
