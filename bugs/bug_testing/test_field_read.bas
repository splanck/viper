CLASS Foo
    DIM value AS INTEGER
END CLASS

DIM x AS INTEGER
DIM obj AS Foo
obj = NEW Foo()
obj.value = 42
x = obj.value
PRINT "X: "; x

END
