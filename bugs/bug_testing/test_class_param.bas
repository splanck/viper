CLASS Foo
    DIM value AS INTEGER

    SUB SetValue(v AS INTEGER)
        value = v
    END SUB
END CLASS

SUB TestSub(f AS Foo)
    PRINT "In TestSub"
    f.SetValue(42)
END SUB

DIM obj AS Foo
obj = NEW Foo()
obj.SetValue(10)
PRINT "Before: "; obj.value

TestSub(obj)
PRINT "After: "; obj.value

END
