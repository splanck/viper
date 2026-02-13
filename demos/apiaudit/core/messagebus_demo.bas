' messagebus_demo.bas
PRINT "=== Viper.Core.MessageBus Demo ==="
DIM bus AS OBJECT
bus = NEW Viper.Core.MessageBus()
PRINT bus.TotalSubscriptions
PRINT bus.SubscriberCount("test")
bus.ClearTopic("test")
bus.Clear()
PRINT bus.TotalSubscriptions
PRINT "done"
END
