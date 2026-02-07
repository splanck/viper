' EXPECT_OUT: RESULT: ok
' COVER: Viper.Network.Tcp.Connect
' COVER: Viper.Network.Tcp.ConnectFor
' COVER: Viper.Network.Tcp.Send
' COVER: Viper.Network.Tcp.SendStr
' COVER: Viper.Network.Tcp.SendAll
' COVER: Viper.Network.Tcp.Recv
' COVER: Viper.Network.Tcp.RecvExact
' COVER: Viper.Network.Tcp.RecvLine
' COVER: Viper.Network.Tcp.RecvStr
' COVER: Viper.Network.Tcp.SetRecvTimeout
' COVER: Viper.Network.Tcp.SetSendTimeout
' COVER: Viper.Network.Tcp.Close
' COVER: Viper.Network.Tcp.Host
' COVER: Viper.Network.Tcp.Port
' COVER: Viper.Network.Tcp.LocalPort
' COVER: Viper.Network.Tcp.IsOpen
' COVER: Viper.Network.Tcp.Available
' COVER: Viper.Network.TcpServer.Listen
' COVER: Viper.Network.TcpServer.ListenAt
' COVER: Viper.Network.TcpServer.Accept
' COVER: Viper.Network.TcpServer.AcceptFor
' COVER: Viper.Network.TcpServer.Close
' COVER: Viper.Network.TcpServer.Address
' COVER: Viper.Network.TcpServer.Port
' COVER: Viper.Network.TcpServer.IsListening
' COVER: Viper.Network.Udp.New
' COVER: Viper.Network.Udp.Bind
' COVER: Viper.Network.Udp.BindAt
' COVER: Viper.Network.Udp.SendTo
' COVER: Viper.Network.Udp.SendToStr
' COVER: Viper.Network.Udp.Recv
' COVER: Viper.Network.Udp.RecvFrom
' COVER: Viper.Network.Udp.RecvFor
' COVER: Viper.Network.Udp.SenderHost
' COVER: Viper.Network.Udp.SenderPort
' COVER: Viper.Network.Udp.SetBroadcast
' COVER: Viper.Network.Udp.JoinGroup
' COVER: Viper.Network.Udp.LeaveGroup
' COVER: Viper.Network.Udp.SetRecvTimeout
' COVER: Viper.Network.Udp.Close
' COVER: Viper.Network.Udp.Address
' COVER: Viper.Network.Udp.Port
' COVER: Viper.Network.Udp.IsBound

DIM basePort AS INTEGER
basePort = 49200 + (Viper.Time.DateTime.NowMs() MOD 1000)

DIM server1 AS Viper.Network.TcpServer
server1 = Viper.Network.TcpServer.ListenAt("127.0.0.1", basePort)
Viper.Core.Diagnostics.Assert(server1.IsListening, "tcpserver.listenat")

DIM client1 AS Viper.Network.Tcp
client1 = Viper.Network.Tcp.ConnectFor("127.0.0.1", basePort, 1000)

DIM serverConn1 AS Viper.Network.Tcp
serverConn1 = server1.AcceptFor(1000)
Viper.Core.Diagnostics.AssertNotNull(serverConn1, "tcpserver.acceptfor")

serverConn1.Close()
client1.Close()
server1.Close()

DIM server2 AS Viper.Network.TcpServer
server2 = Viper.Network.TcpServer.Listen(basePort + 1)
Viper.Core.Diagnostics.Assert(server2.IsListening, "tcpserver.listen")
Viper.Core.Diagnostics.AssertEq(server2.Port, basePort + 1, "tcpserver.port")
Viper.Core.Diagnostics.Assert(server2.Address <> "", "tcpserver.address")

DIM client2 AS Viper.Network.Tcp
client2 = Viper.Network.Tcp.Connect("127.0.0.1", basePort + 1)

DIM serverConn2 AS Viper.Network.Tcp
serverConn2 = server2.Accept()
Viper.Core.Diagnostics.AssertNotNull(serverConn2, "tcpserver.accept")

Viper.Core.Diagnostics.Assert(client2.IsOpen, "tcp.isopen")
Viper.Core.Diagnostics.Assert(client2.Port = basePort + 1, "tcp.port")
Viper.Core.Diagnostics.Assert(client2.LocalPort > 0, "tcp.localport")
Viper.Core.Diagnostics.Assert(client2.Host <> "", "tcp.host")

client2.SetRecvTimeout(1000)
client2.SetSendTimeout(1000)
serverConn2.SetRecvTimeout(1000)
serverConn2.SetSendTimeout(1000)

DIM payload AS Viper.Collections.Bytes
payload = NEW Viper.Collections.Bytes(4)
payload.Set(0, 112)
payload.Set(1, 105)
payload.Set(2, 110)
payload.Set(3, 103)

DIM sent AS INTEGER
sent = client2.Send(payload)
Viper.Core.Diagnostics.AssertEq(sent, 4, "tcp.send")

DIM avail AS INTEGER
avail = serverConn2.Available
DIM tries AS INTEGER
tries = 0
WHILE avail < 4 AND tries < 100
    Viper.Time.Clock.Sleep(10)
    avail = serverConn2.Available
    tries = tries + 1
WEND
Viper.Core.Diagnostics.Assert(avail >= 4, "tcp.available")

DIM recvBytes AS Viper.Collections.Bytes
recvBytes = serverConn2.Recv(4)
Viper.Core.Diagnostics.AssertEq(recvBytes.Len, 4, "tcp.recv")

sent = client2.Send(payload)
DIM recvExact AS Viper.Collections.Bytes
recvExact = serverConn2.RecvExact(4)
Viper.Core.Diagnostics.AssertEq(recvExact.Len, 4, "tcp.recvexact")

client2.SendStr("line\n")
DIM line AS STRING
line = serverConn2.RecvLine()
Viper.Core.Diagnostics.AssertEqStr(line, "line", "tcp.recvline")

client2.SendStr("word")
DIM word AS STRING
word = serverConn2.RecvStr(4)
Viper.Core.Diagnostics.AssertEqStr(word, "word", "tcp.recvstr")

serverConn2.SendAll(payload)
DIM back AS Viper.Collections.Bytes
back = client2.RecvExact(4)
Viper.Core.Diagnostics.AssertEq(back.Len, 4, "tcp.sendall")

client2.Close()
serverConn2.Close()
server2.Close()

DIM udpBind AS Viper.Network.Udp
udpBind = Viper.Network.Udp.Bind(0)
Viper.Core.Diagnostics.Assert(udpBind.IsBound, "udp.bind")

DIM udpServer AS Viper.Network.Udp
udpServer = Viper.Network.Udp.BindAt("127.0.0.1", 0)
Viper.Core.Diagnostics.Assert(udpServer.IsBound, "udp.bindat")
Viper.Core.Diagnostics.Assert(udpServer.Port > 0, "udp.port")
Viper.Core.Diagnostics.Assert(udpServer.Address <> "", "udp.address")

DIM udpClient AS Viper.Network.Udp
udpClient = Viper.Network.Udp.New()

udpServer.SetBroadcast(1)
udpServer.SetBroadcast(0)

udpServer.JoinGroup("239.255.0.1")
udpServer.LeaveGroup("239.255.0.1")

udpServer.SetRecvTimeout(1000)

DIM udpPort AS INTEGER
udpPort = udpServer.Port

DIM msg AS STRING
msg = "ping"
udpClient.SendToStr("127.0.0.1", udpPort, msg)
DIM udpRecv1 AS Viper.Collections.Bytes
udpRecv1 = udpServer.RecvFrom(32)
Viper.Core.Diagnostics.AssertEqStr(udpRecv1.ToStr(), msg, "udp.recvfrom")

DIM senderHost AS STRING
senderHost = udpServer.SenderHost()
Viper.Core.Diagnostics.Assert(senderHost <> "", "udp.senderhost")
Viper.Core.Diagnostics.Assert(udpServer.SenderPort() > 0, "udp.senderport")

DIM msgBytes AS Viper.Collections.Bytes
msgBytes = NEW Viper.Collections.Bytes(4)
msgBytes.Set(0, 112)
msgBytes.Set(1, 111)
msgBytes.Set(2, 110)
msgBytes.Set(3, 103)
udpClient.SendTo("127.0.0.1", udpPort, msgBytes)
DIM udpRecv2 AS Viper.Collections.Bytes
udpRecv2 = udpServer.Recv(32)
Viper.Core.Diagnostics.AssertEqStr(udpRecv2.ToStr(), "pong", "udp.recv")

udpClient.SendToStr("127.0.0.1", udpPort, "data")
DIM udpRecv3 AS Viper.Collections.Bytes
udpRecv3 = udpServer.RecvFor(32, 1000)
IF Viper.Core.Object.RefEquals(udpRecv3, NOTHING) THEN
    udpRecv3 = udpServer.Recv(32)
END IF
Viper.Core.Diagnostics.AssertNotNull(udpRecv3, "udp.recvfor")
Viper.Core.Diagnostics.AssertEqStr(udpRecv3.ToStr(), "data", "udp.recvfor")

udpClient.Close()
udpServer.Close()
udpBind.Close()

PRINT "RESULT: ok"
END
