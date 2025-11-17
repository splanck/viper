REM Test calling methods on object parameters

CLASS Target
    value AS INTEGER

    SUB Init()
        ME.value = 100
    END SUB

    SUB Modify(amount AS INTEGER)
        ME.value = ME.value - amount
        PRINT "Modified to: "; ME.value
    END SUB
END CLASS

CLASS Actor
    SUB DoAction(t AS Target)
        PRINT "Calling method on parameter..."
        t.Modify(25)
    END SUB
END CLASS

REM Test
DIM obj AS Target
obj = NEW Target()
obj.Init()

DIM act AS Actor
act = NEW Actor()

PRINT "Initial value: "; obj.value
act.DoAction(obj)
PRINT "Final value: "; obj.value
