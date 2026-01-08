' Test: OOP - Inheritance
' Syntax: CLASS Child : Parent

' Base class
CLASS Animal
    DIM name AS STRING

    SUB New(n AS STRING)
        name = n
    END SUB

    FUNCTION GetName$() AS STRING
        GetName$ = name
    END FUNCTION

    VIRTUAL FUNCTION Speak$() AS STRING
        Speak$ = "..."
    END FUNCTION
END CLASS

' Derived class using : syntax
CLASS Dog : Animal
    DIM breed AS STRING

    SUB New(n AS STRING, b AS STRING)
        name = n
        breed = b
    END SUB

    FUNCTION GetBreed$() AS STRING
        GetBreed$ = breed
    END FUNCTION

    OVERRIDE FUNCTION Speak$() AS STRING
        Speak$ = "Woof!"
    END FUNCTION
END CLASS

' Another derived class
CLASS Cat : Animal
    SUB New(n AS STRING)
        name = n
    END SUB

    OVERRIDE FUNCTION Speak$() AS STRING
        Speak$ = "Meow!"
    END FUNCTION
END CLASS

' Main program
PRINT "=== OOP Inheritance Test ==="

' Test basic inheritance
PRINT ""
PRINT "--- Basic Inheritance ---"
DIM dog AS Dog
dog = NEW Dog("Buddy", "Golden Retriever")
PRINT "Dog name: "; dog.GetName$()
PRINT "Dog breed: "; dog.GetBreed$()
PRINT "Dog speaks: "; dog.Speak$()

PRINT ""
DIM cat AS Cat
cat = NEW Cat("Whiskers")
PRINT "Cat name: "; cat.GetName$()
PRINT "Cat speaks: "; cat.Speak$()

' Test polymorphism
PRINT ""
PRINT "--- Polymorphism ---"
DIM animal AS Animal

animal = dog
PRINT "As Animal (dog): "; animal.Speak$()

animal = cat
PRINT "As Animal (cat): "; animal.Speak$()

PRINT ""
PRINT "=== OOP Inheritance test complete ==="
