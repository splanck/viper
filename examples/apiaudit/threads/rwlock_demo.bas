' =============================================================================
' API Audit: Viper.Threads.RwLock (BASIC)
' =============================================================================
' Tests: New, ReadEnter, ReadExit, WriteEnter, WriteExit, TryReadEnter,
'        TryWriteEnter, Readers, IsWriteLocked
' =============================================================================

PRINT "=== API Audit: Viper.Threads.RwLock ==="

' --- New ---
PRINT "--- New ---"
DIM lock AS OBJECT = Viper.Threads.RwLock.New()
PRINT "Created RwLock"

' --- Readers (initial) ---
PRINT "--- Readers (initial) ---"
PRINT "Readers: "; lock.Readers

' --- IsWriteLocked (initial) ---
PRINT "--- IsWriteLocked (initial) ---"
PRINT "IsWriteLocked: "; lock.IsWriteLocked

' --- ReadEnter / ReadExit ---
PRINT "--- ReadEnter ---"
lock.ReadEnter()
PRINT "Readers after ReadEnter: "; lock.Readers
PRINT "IsWriteLocked: "; lock.IsWriteLocked

PRINT "--- ReadExit ---"
lock.ReadExit()
PRINT "Readers after ReadExit: "; lock.Readers

' --- WriteEnter / WriteExit ---
PRINT "--- WriteEnter ---"
lock.WriteEnter()
PRINT "IsWriteLocked after WriteEnter: "; lock.IsWriteLocked
PRINT "Readers: "; lock.Readers

PRINT "--- WriteExit ---"
lock.WriteExit()
PRINT "IsWriteLocked after WriteExit: "; lock.IsWriteLocked

' --- TryReadEnter ---
PRINT "--- TryReadEnter ---"
DIM ok1 AS INTEGER = lock.TryReadEnter()
PRINT "TryReadEnter: "; ok1
PRINT "Readers: "; lock.Readers
lock.ReadExit()

' --- TryWriteEnter ---
PRINT "--- TryWriteEnter ---"
DIM ok2 AS INTEGER = lock.TryWriteEnter()
PRINT "TryWriteEnter: "; ok2
PRINT "IsWriteLocked: "; lock.IsWriteLocked
lock.WriteExit()

' --- Multiple readers ---
PRINT "--- Multiple readers ---"
lock.ReadEnter()
lock.ReadEnter()
PRINT "Readers (2 entered): "; lock.Readers
lock.ReadExit()
PRINT "Readers (1 exited): "; lock.Readers
lock.ReadExit()
PRINT "Readers (all exited): "; lock.Readers

' --- TryWriteEnter while read-locked ---
PRINT "--- TryWriteEnter while read-locked ---"
lock.ReadEnter()
DIM ok3 AS INTEGER = lock.TryWriteEnter()
PRINT "TryWriteEnter (read held): "; ok3
lock.ReadExit()

' --- TryReadEnter while write-locked ---
PRINT "--- TryReadEnter while write-locked ---"
lock.WriteEnter()
DIM ok4 AS INTEGER = lock.TryReadEnter()
PRINT "TryReadEnter (write held): "; ok4
lock.WriteExit()

' --- Re-entrant write lock ---
PRINT "--- Re-entrant write lock ---"
lock.WriteEnter()
DIM ok5 AS INTEGER = lock.TryWriteEnter()
PRINT "TryWriteEnter (re-entrant): "; ok5
lock.WriteExit()
lock.WriteExit()
PRINT "IsWriteLocked after double exit: "; lock.IsWriteLocked

PRINT "=== RwLock Audit Complete ==="
END
