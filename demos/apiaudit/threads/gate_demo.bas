' gate_demo.bas
PRINT "=== Viper.Threads.Gate Demo ==="
DIM g AS OBJECT
g = NEW Viper.Threads.Gate(3)
PRINT g.Permits
PRINT g.TryEnter()
PRINT g.Permits
g.Leave()
PRINT g.Permits
PRINT "done"
END
