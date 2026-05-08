struct Counter
    static integer size

    static method Inc takes nothing returns integer
        set this.size = this.size + 1
        return this.size
    endmethod
endstruct
