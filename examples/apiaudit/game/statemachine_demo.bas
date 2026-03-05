' statemachine_demo.bas - Comprehensive API audit for Viper.Game.StateMachine
' Tests: New, AddState, SetInitial, Current, Previous, IsState, Transition,
'        JustEntered, JustExited, ClearFlags, FramesInState, Update,
'        HasState, StateCount

PRINT "=== StateMachine API Audit ==="

' State IDs: 0 = Idle, 1 = Walking, 2 = Running, 3 = Jumping

' --- New ---
PRINT "--- New ---"
DIM sm AS OBJECT
sm = Viper.Game.StateMachine.New()
PRINT sm.StateCount      ' 0

' --- AddState ---
PRINT "--- AddState ---"
DIM a1 AS INTEGER
a1 = sm.AddState(0)
PRINT a1                 ' 1
DIM a2 AS INTEGER
a2 = sm.AddState(1)
PRINT a2                 ' 1
DIM a3 AS INTEGER
a3 = sm.AddState(2)
PRINT a3                 ' 1
DIM a4 AS INTEGER
a4 = sm.AddState(3)
PRINT a4                 ' 1
PRINT sm.StateCount      ' 4

' --- HasState ---
PRINT "--- HasState ---"
PRINT sm.HasState(0)     ' 1
PRINT sm.HasState(1)     ' 1
PRINT sm.HasState(99)    ' 0

' --- SetInitial ---
PRINT "--- SetInitial ---"
DIM init AS INTEGER
init = sm.SetInitial(0)
PRINT init               ' 1
PRINT sm.Current         ' 0

' --- IsState ---
PRINT "--- IsState ---"
PRINT sm.IsState(0)      ' 1
PRINT sm.IsState(1)      ' 0

' --- JustEntered / JustExited ---
PRINT "--- JustEntered / JustExited ---"
PRINT sm.JustEntered     ' 1
PRINT sm.JustExited      ' 0

' --- ClearFlags ---
PRINT "--- ClearFlags ---"
sm.ClearFlags()
PRINT sm.JustEntered     ' 0
PRINT sm.JustExited      ' 0

' --- Transition ---
PRINT "--- Transition ---"
DIM t1 AS INTEGER
t1 = sm.Transition(1)
PRINT t1                 ' 1
PRINT sm.Current         ' 1
PRINT sm.Previous        ' 0
PRINT sm.JustEntered     ' 1
PRINT sm.JustExited      ' 1

' --- Transition to same state ---
PRINT "--- Transition same ---"
sm.ClearFlags()
DIM t2 AS INTEGER
t2 = sm.Transition(1)
PRINT t2                 ' 0
PRINT sm.Current         ' 1

' --- Transition to invalid state ---
PRINT "--- Transition invalid ---"
DIM t3 AS INTEGER
t3 = sm.Transition(99)
PRINT t3                 ' 0
PRINT sm.Current         ' 1

' --- Update / FramesInState ---
PRINT "--- Update / FramesInState ---"
sm.ClearFlags()
sm.Update()
sm.Update()
sm.Update()
PRINT sm.FramesInState   ' 3

' --- Transition resets frames ---
PRINT "--- Transition resets frames ---"
sm.Transition(2)
PRINT sm.Current         ' 2
PRINT sm.FramesInState   ' 0

' --- AddState duplicate ---
PRINT "--- AddState duplicate ---"
DIM dup AS INTEGER
dup = sm.AddState(0)
PRINT dup                ' 0
PRINT sm.StateCount      ' 4

PRINT "=== StateMachine audit complete ==="
END
