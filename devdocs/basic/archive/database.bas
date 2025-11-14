REM Simple Contact Database - Version 0.4
REM File-based database (string arrays not supported)

DIM dbFile$ AS STRING
dbFile$ = "/tmp/contacts.dat"

PRINT "=== Contact Database ==="
PRINT "Version 0.4 - File Based"
PRINT ""

REM Initialize database file with a few contacts
OPEN dbFile$ FOR OUTPUT AS #1
PRINT #1, "Alice Smith|555-1234"
PRINT #1, "Bob Jones|555-5678"
PRINT #1, "Carol White|555-9012"
CLOSE #1

PRINT "Database initialized with 3 contacts"
PRINT ""

REM List all contacts
PRINT "=== All Contacts ==="
DIM line$ AS STRING
DIM count AS INTEGER
count = 0

OPEN dbFile$ FOR INPUT AS #1
DO WHILE EOF(#1) = 0
    LINE INPUT #1, line$
    count = count + 1
    PRINT STR$(count) + ". " + line$
LOOP
CLOSE #1

PRINT ""
PRINT "Total: " + STR$(count) + " contacts"
PRINT ""

REM Add a new contact
PRINT "=== Adding New Contact ==="
DIM newName$ AS STRING
DIM newPhone$ AS STRING
newName$ = "David Brown"
newPhone$ = "555-3456"

OPEN dbFile$ FOR APPEND AS #1
PRINT #1, newName$ + "|" + newPhone$
CLOSE #1

PRINT "Added: " + newName$ + " - " + newPhone$
PRINT ""

REM List all contacts again to verify
PRINT "=== All Contacts (After Add) ==="
count = 0

OPEN dbFile$ FOR INPUT AS #1
DO WHILE EOF(#1) = 0
    LINE INPUT #1, line$
    count = count + 1
    PRINT STR$(count) + ". " + line$
LOOP
CLOSE #1

PRINT ""
PRINT "Total: " + STR$(count) + " contacts"

REM Search for contacts
PRINT ""
PRINT "=== Searching for 'Smith' ==="
DIM searchTerm$ AS STRING
searchTerm$ = "Smith"
DIM found AS INTEGER
found = 0

OPEN dbFile$ FOR INPUT AS #1
DO WHILE EOF(#1) = 0
    LINE INPUT #1, line$
    IF INSTR(line$, searchTerm$) > 0 THEN
        found = found + 1
        PRINT "Found: " + line$
    END IF
LOOP
CLOSE #1

IF found = 0 THEN
    PRINT "No matches found"
ELSE
    PRINT "Found " + STR$(found) + " match(es)"
END IF

REM Parse a contact record
PRINT ""
PRINT "=== Parsing Contact Record ==="
DIM record$ AS STRING
DIM pipePos AS INTEGER
DIM contactName$ AS STRING
DIM contactPhone$ AS STRING

record$ = "Alice Smith|555-1234"
pipePos = INSTR(record$, "|")

IF pipePos > 0 THEN
    contactName$ = LEFT$(record$, pipePos - 1)
    contactPhone$ = RIGHT$(record$, LEN(record$) - pipePos)
    PRINT "Name: " + contactName$
    PRINT "Phone: " + contactPhone$
ELSE
    PRINT "Invalid record format"
END IF

REM Delete a contact by name
PRINT ""
PRINT "=== Deleting Contact 'Bob Jones' ==="
DIM deleteTarget$ AS STRING
DIM tempFile$ AS STRING
deleteTarget$ = "Bob Jones"
tempFile$ = "/tmp/contacts_temp.dat"
DIM deleted AS INTEGER
deleted = 0

REM Read from original, write to temp (excluding target)
OPEN dbFile$ FOR INPUT AS #1
OPEN tempFile$ FOR OUTPUT AS #2
DO WHILE EOF(#1) = 0
    LINE INPUT #1, line$
    IF INSTR(line$, deleteTarget$) = 0 THEN
        PRINT #2, line$
    ELSE
        deleted = 1
        PRINT "Deleted: " + line$
    END IF
LOOP
CLOSE #1
CLOSE #2

REM Now copy temp back to original
OPEN tempFile$ FOR INPUT AS #1
OPEN dbFile$ FOR OUTPUT AS #2
DO WHILE EOF(#1) = 0
    LINE INPUT #1, line$
    PRINT #2, line$
LOOP
CLOSE #1
CLOSE #2

IF deleted = 0 THEN
    PRINT "Contact not found"
END IF

REM Final listing to verify delete
PRINT ""
PRINT "=== Final Contact List ==="
count = 0

OPEN dbFile$ FOR INPUT AS #1
DO WHILE EOF(#1) = 0
    LINE INPUT #1, line$
    count = count + 1
    PRINT STR$(count) + ". " + line$
LOOP
CLOSE #1

PRINT ""
PRINT "Total: " + STR$(count) + " contacts remaining"
