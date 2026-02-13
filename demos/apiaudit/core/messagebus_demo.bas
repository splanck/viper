' =============================================================================
' API Audit: Viper.Core.MessageBus - Pub/Sub Message Bus
' =============================================================================
' Tests: New, SubscriberCount, TotalSubscriptions, Topics, ClearTopic, Clear
' NOTE: Subscribe needs a callback object - testing creation and query APIs
' =============================================================================

PRINT "=== API Audit: Viper.Core.MessageBus ==="

' --- New ---
PRINT "--- New ---"
DIM bus AS OBJECT
bus = Viper.Core.MessageBus.New()
PRINT "MessageBus.New() created"

' --- TotalSubscriptions (empty) ---
PRINT "--- TotalSubscriptions (empty) ---"
PRINT "TotalSubscriptions: "; Viper.Core.MessageBus.get_TotalSubscriptions(bus)

' --- SubscriberCount (no topic) ---
PRINT "--- SubscriberCount ---"
PRINT "SubscriberCount('test-topic'): "; Viper.Core.MessageBus.SubscriberCount(bus, "test-topic")

' --- Topics (empty) ---
PRINT "--- Topics ---"
DIM topics AS OBJECT
topics = Viper.Core.MessageBus.Topics(bus)
PRINT "Topics() returned (empty bus)"

' --- ClearTopic ---
PRINT "--- ClearTopic ---"
Viper.Core.MessageBus.ClearTopic(bus, "nonexistent-topic")
PRINT "ClearTopic('nonexistent-topic') - no crash"

' --- Clear ---
PRINT "--- Clear ---"
Viper.Core.MessageBus.Clear(bus)
PRINT "Clear() - no crash"
PRINT "TotalSubscriptions after Clear: "; Viper.Core.MessageBus.get_TotalSubscriptions(bus)

PRINT "=== MessageBus Demo Complete ==="
END
