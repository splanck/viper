REM Test BUG-073: Cannot Call Methods on Object Parameters

CLASS Target
    value AS INTEGER

    SUB Init(v AS INTEGER)
        ME.value = v
    END SUB

    SUB Modify(amount AS INTEGER)
        ME.value = ME.value + amount
    END SUB

    SUB Show()
        PRINT "Target value: "; ME.value
    END SUB
END CLASS

CLASS Actor
    name AS STRING

    SUB Init(n AS STRING)
        ME.name = n
    END SUB

    SUB DoAction(t AS Target)
        PRINT ME.name; " is modifying target..."
        t.Modify(25)
        t.Show()
    END SUB
END CLASS

REM Test the fix
PRINT "Testing BUG-073 fix: Object parameters with method calls"
PRINT

DIM target AS Target
DIM actor AS Actor

target = NEW Target()
actor = NEW Actor()

target.Init(100)
actor.Init("Hero")

PRINT "Before action:"
target.Show()
PRINT

actor.DoAction(target)
PRINT

PRINT "After action:"
target.Show()
PRINT

PRINT "SUCCESS: Object parameter method calls work!"
