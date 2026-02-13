' statemachine_demo.bas
PRINT "=== Viper.Game.StateMachine Demo ==="
DIM sm AS OBJECT
sm = NEW Viper.Game.StateMachine()
PRINT sm.AddState(0)
PRINT sm.AddState(1)
PRINT sm.AddState(2)
PRINT sm.StateCount
PRINT sm.SetInitial(0)
PRINT sm.Current
PRINT sm.Transition(1)
PRINT sm.Current
PRINT sm.Previous
PRINT sm.HasState(2)
PRINT "done"
END
