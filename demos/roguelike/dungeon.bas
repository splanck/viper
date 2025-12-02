' dungeon.bas - Dungeon generation for Roguelike RPG
' BSP + Room-based procedural generation

' ============================================================================
' ROOM CLASS - Represents a room in the dungeon
' ============================================================================
CLASS Room
    DIM x1 AS INTEGER
    DIM y1 AS INTEGER
    DIM x2 AS INTEGER
    DIM y2 AS INTEGER
    DIM centerX AS INTEGER
    DIM centerY AS INTEGER
    DIM roomType AS INTEGER     ' 0=normal, 1=treasure, 2=shrine, 3=boss
    DIM isConnected AS INTEGER

    SUB Init(rx AS INTEGER, ry AS INTEGER, rw AS INTEGER, rh AS INTEGER)
        x1 = rx
        y1 = ry
        x2 = rx + rw - 1
        y2 = ry + rh - 1
        centerX = rx + rw / 2
        centerY = ry + rh / 2
        roomType = 0
        isConnected = 0
    END SUB

    FUNCTION GetX1() AS INTEGER
        GetX1 = x1
    END FUNCTION

    FUNCTION GetY1() AS INTEGER
        GetY1 = y1
    END FUNCTION

    FUNCTION GetX2() AS INTEGER
        GetX2 = x2
    END FUNCTION

    FUNCTION GetY2() AS INTEGER
        GetY2 = y2
    END FUNCTION

    FUNCTION GetCenterX() AS INTEGER
        GetCenterX = centerX
    END FUNCTION

    FUNCTION GetCenterY() AS INTEGER
        GetCenterY = centerY
    END FUNCTION

    FUNCTION GetWidth() AS INTEGER
        GetWidth = x2 - x1 + 1
    END FUNCTION

    FUNCTION GetHeight() AS INTEGER
        GetHeight = y2 - y1 + 1
    END FUNCTION

    FUNCTION Intersects(other AS Room) AS INTEGER
        Intersects = 0
        IF x1 <= other.GetX2() + 1 THEN
            IF x2 >= other.GetX1() - 1 THEN
                IF y1 <= other.GetY2() + 1 THEN
                    IF y2 >= other.GetY1() - 1 THEN
                        Intersects = 1
                    END IF
                END IF
            END IF
        END IF
    END FUNCTION

    FUNCTION Contains(px AS INTEGER, py AS INTEGER) AS INTEGER
        Contains = 0
        IF px >= x1 THEN
            IF px <= x2 THEN
                IF py >= y1 THEN
                    IF py <= y2 THEN
                        Contains = 1
                    END IF
                END IF
            END IF
        END IF
    END FUNCTION

    SUB SetType(t AS INTEGER)
        roomType = t
    END SUB

    FUNCTION GetType() AS INTEGER
        GetType = roomType
    END FUNCTION

    SUB MarkConnected()
        isConnected = 1
    END SUB

    FUNCTION IsConnected() AS INTEGER
        IsConnected = isConnected
    END FUNCTION
END CLASS

' ============================================================================
' DUNGEON MAP CLASS
' ============================================================================
CLASS DungeonMap
    DIM tiles(2399) AS INTEGER      ' 60x40 = 2400 tiles
    DIM explored(2399) AS INTEGER   ' 1 = has been seen
    DIM visible(2399) AS INTEGER    ' 1 = currently visible
    DIM light(2399) AS INTEGER      ' Light level 0-10

    DIM mapWidth AS INTEGER
    DIM mapHeight AS INTEGER
    DIM floorLevel AS INTEGER

    ' Rooms
    DIM rooms(14) AS Room
    DIM roomCount AS INTEGER

    ' Special locations
    DIM stairsDownX AS INTEGER
    DIM stairsDownY AS INTEGER
    DIM stairsUpX AS INTEGER
    DIM stairsUpY AS INTEGER
    DIM startX AS INTEGER
    DIM startY AS INTEGER

    SUB Init(floorNum AS INTEGER)
        mapWidth = MAP_WIDTH
        mapHeight = MAP_HEIGHT
        floorLevel = floorNum
        roomCount = 0

        ' Initialize all tiles to walls
        DIM i AS INTEGER
        FOR i = 0 TO 2399
            tiles(i) = TILE_WALL
            explored(i) = 0
            visible(i) = 0
            light(i) = 0
        NEXT i
    END SUB

    FUNCTION GetIndex(x AS INTEGER, y AS INTEGER) AS INTEGER
        GetIndex = y * mapWidth + x
    END FUNCTION

    FUNCTION GetTile(x AS INTEGER, y AS INTEGER) AS INTEGER
        IF x < 0 THEN GetTile = TILE_WALL : EXIT FUNCTION
        IF x >= mapWidth THEN GetTile = TILE_WALL : EXIT FUNCTION
        IF y < 0 THEN GetTile = TILE_WALL : EXIT FUNCTION
        IF y >= mapHeight THEN GetTile = TILE_WALL : EXIT FUNCTION
        GetTile = tiles(Me.GetIndex(x, y))
    END FUNCTION

    SUB SetTile(x AS INTEGER, y AS INTEGER, t AS INTEGER)
        IF x < 0 THEN EXIT SUB
        IF x >= mapWidth THEN EXIT SUB
        IF y < 0 THEN EXIT SUB
        IF y >= mapHeight THEN EXIT SUB
        tiles(Me.GetIndex(x, y)) = t
    END SUB

    FUNCTION IsWalkable(x AS INTEGER, y AS INTEGER) AS INTEGER
        DIM t AS INTEGER
        t = Me.GetTile(x, y)
        IsWalkable = 0
        IF t = TILE_FLOOR THEN IsWalkable = 1
        IF t = TILE_DOOR_OPEN THEN IsWalkable = 1
        IF t = TILE_STAIRS_DOWN THEN IsWalkable = 1
        IF t = TILE_STAIRS_UP THEN IsWalkable = 1
        IF t = TILE_TRAP_HIDDEN THEN IsWalkable = 1
        IF t = TILE_TRAP_VISIBLE THEN IsWalkable = 1
        IF t = TILE_WATER THEN IsWalkable = 1
        IF t = TILE_SHRINE THEN IsWalkable = 1
    END FUNCTION

    FUNCTION BlocksSight(x AS INTEGER, y AS INTEGER) AS INTEGER
        DIM t AS INTEGER
        t = Me.GetTile(x, y)
        BlocksSight = 1
        IF t = TILE_FLOOR THEN BlocksSight = 0
        IF t = TILE_DOOR_OPEN THEN BlocksSight = 0
        IF t = TILE_STAIRS_DOWN THEN BlocksSight = 0
        IF t = TILE_STAIRS_UP THEN BlocksSight = 0
        IF t = TILE_TRAP_HIDDEN THEN BlocksSight = 0
        IF t = TILE_TRAP_VISIBLE THEN BlocksSight = 0
        IF t = TILE_WATER THEN BlocksSight = 0
        IF t = TILE_LAVA THEN BlocksSight = 0
        IF t = TILE_SHRINE THEN BlocksSight = 0
    END FUNCTION

    FUNCTION IsExplored(x AS INTEGER, y AS INTEGER) AS INTEGER
        IF x < 0 THEN IsExplored = 0 : EXIT FUNCTION
        IF x >= mapWidth THEN IsExplored = 0 : EXIT FUNCTION
        IF y < 0 THEN IsExplored = 0 : EXIT FUNCTION
        IF y >= mapHeight THEN IsExplored = 0 : EXIT FUNCTION
        IsExplored = explored(Me.GetIndex(x, y))
    END FUNCTION

    SUB SetExplored(x AS INTEGER, y AS INTEGER)
        IF x < 0 THEN EXIT SUB
        IF x >= mapWidth THEN EXIT SUB
        IF y < 0 THEN EXIT SUB
        IF y >= mapHeight THEN EXIT SUB
        explored(Me.GetIndex(x, y)) = 1
    END SUB

    FUNCTION IsVisible(x AS INTEGER, y AS INTEGER) AS INTEGER
        IF x < 0 THEN IsVisible = 0 : EXIT FUNCTION
        IF x >= mapWidth THEN IsVisible = 0 : EXIT FUNCTION
        IF y < 0 THEN IsVisible = 0 : EXIT FUNCTION
        IF y >= mapHeight THEN IsVisible = 0 : EXIT FUNCTION
        IsVisible = visible(Me.GetIndex(x, y))
    END FUNCTION

    SUB SetVisible(x AS INTEGER, y AS INTEGER, val AS INTEGER)
        IF x < 0 THEN EXIT SUB
        IF x >= mapWidth THEN EXIT SUB
        IF y < 0 THEN EXIT SUB
        IF y >= mapHeight THEN EXIT SUB
        visible(Me.GetIndex(x, y)) = val
        IF val = 1 THEN explored(Me.GetIndex(x, y)) = 1
    END SUB

    SUB ClearVisible()
        DIM i AS INTEGER
        FOR i = 0 TO 2399
            visible(i) = 0
        NEXT i
    END SUB

    SUB CreateRoom(rm AS Room)
        DIM x AS INTEGER
        DIM y AS INTEGER

        FOR y = rm.GetY1() TO rm.GetY2()
            FOR x = rm.GetX1() TO rm.GetX2()
                Me.SetTile(x, y, TILE_FLOOR)
            NEXT x
        NEXT y

        rooms(roomCount) = rm
        roomCount = roomCount + 1
    END SUB

    SUB CreateHTunnel(x1 AS INTEGER, x2 AS INTEGER, y AS INTEGER)
        DIM x AS INTEGER
        DIM minX AS INTEGER
        DIM maxX AS INTEGER

        IF x1 < x2 THEN
            minX = x1
            maxX = x2
        ELSE
            minX = x2
            maxX = x1
        END IF

        FOR x = minX TO maxX
            Me.SetTile(x, y, TILE_FLOOR)
        NEXT x
    END SUB

    SUB CreateVTunnel(y1 AS INTEGER, y2 AS INTEGER, x AS INTEGER)
        DIM y AS INTEGER
        DIM minY AS INTEGER
        DIM maxY AS INTEGER

        IF y1 < y2 THEN
            minY = y1
            maxY = y2
        ELSE
            minY = y2
            maxY = y1
        END IF

        FOR y = minY TO maxY
            Me.SetTile(x, y, TILE_FLOOR)
        NEXT y
    END SUB

    SUB ConnectRooms(r1 AS Room, r2 AS Room)
        DIM x1 AS INTEGER
        DIM y1 AS INTEGER
        DIM x2 AS INTEGER
        DIM y2 AS INTEGER

        x1 = r1.GetCenterX()
        y1 = r1.GetCenterY()
        x2 = r2.GetCenterX()
        y2 = r2.GetCenterY()

        ' Randomly choose L-shape direction
        IF RND() < 0.5 THEN
            Me.CreateHTunnel(x1, x2, y1)
            Me.CreateVTunnel(y1, y2, x2)
        ELSE
            Me.CreateVTunnel(y1, y2, x1)
            Me.CreateHTunnel(x1, x2, y2)
        END IF

        r1.MarkConnected()
        r2.MarkConnected()
    END SUB

    SUB Generate()
        DIM attempts AS INTEGER
        DIM i AS INTEGER

        ' Generate rooms
        attempts = 0
        DO WHILE roomCount < MAX_ROOMS
            IF attempts > 100 THEN EXIT DO
            attempts = attempts + 1

            ' Random room size
            DIM w AS INTEGER
            DIM h AS INTEGER
            w = MIN_ROOM_SIZE + INT(RND() * (MAX_ROOM_SIZE - MIN_ROOM_SIZE))
            h = MIN_ROOM_SIZE + INT(RND() * (MAX_ROOM_SIZE - MIN_ROOM_SIZE))

            ' Random position
            DIM x AS INTEGER
            DIM y AS INTEGER
            x = 1 + INT(RND() * (mapWidth - w - 2))
            y = 1 + INT(RND() * (mapHeight - h - 2))

            DIM newRoom AS Room
            newRoom = NEW Room()
            newRoom.Init(x, y, w, h)

            ' Check for intersections
            DIM canPlace AS INTEGER
            canPlace = 1
            FOR i = 0 TO roomCount - 1
                IF newRoom.Intersects(rooms(i)) = 1 THEN
                    canPlace = 0
                    EXIT FOR
                END IF
            NEXT i

            IF canPlace = 1 THEN
                Me.CreateRoom(newRoom)

                ' Connect to previous room
                IF roomCount > 1 THEN
                    Me.ConnectRooms(rooms(roomCount - 2), rooms(roomCount - 1))
                END IF
            END IF
        LOOP

        ' Place stairs
        IF roomCount > 0 THEN
            ' Up stairs in first room
            stairsUpX = rooms(0).GetCenterX()
            stairsUpY = rooms(0).GetCenterY()
            IF floorLevel > 1 THEN
                Me.SetTile(stairsUpX, stairsUpY, TILE_STAIRS_UP)
            END IF
            startX = stairsUpX
            startY = stairsUpY

            ' Down stairs in last room
            stairsDownX = rooms(roomCount - 1).GetCenterX()
            stairsDownY = rooms(roomCount - 1).GetCenterY()
            IF floorLevel < MAX_FLOORS THEN
                Me.SetTile(stairsDownX, stairsDownY, TILE_STAIRS_DOWN)
            END IF
        END IF

        ' Add special rooms
        IF roomCount >= 3 THEN
            ' Treasure room
            DIM treasureRoom AS INTEGER
            treasureRoom = 1 + INT(RND() * (roomCount - 2))
            rooms(treasureRoom).SetType(1)

            ' Maybe a shrine
            IF RND() < 0.3 THEN
                DIM shrineRoom AS INTEGER
                shrineRoom = 1 + INT(RND() * (roomCount - 2))
                IF shrineRoom <> treasureRoom THEN
                    rooms(shrineRoom).SetType(2)
                    Me.SetTile(rooms(shrineRoom).GetCenterX(), rooms(shrineRoom).GetCenterY(), TILE_SHRINE)
                END IF
            END IF
        END IF

        ' Add some traps
        Me.PlaceTraps()

        ' Add torches for light
        Me.PlaceTorches()
    END SUB

    SUB PlaceTraps()
        DIM i AS INTEGER
        DIM numTraps AS INTEGER
        numTraps = 3 + INT(RND() * (floorLevel + 2))

        FOR i = 1 TO numTraps
            DIM x AS INTEGER
            DIM y AS INTEGER
            DIM attempts AS INTEGER
            attempts = 0
            DO
                x = 1 + INT(RND() * (mapWidth - 2))
                y = 1 + INT(RND() * (mapHeight - 2))
                attempts = attempts + 1
                IF attempts > 50 THEN EXIT DO
            LOOP WHILE Me.GetTile(x, y) <> TILE_FLOOR

            IF Me.GetTile(x, y) = TILE_FLOOR THEN
                Me.SetTile(x, y, TILE_TRAP_HIDDEN)
            END IF
        NEXT i
    END SUB

    SUB PlaceTorches()
        DIM i AS INTEGER
        DIM r AS INTEGER

        FOR r = 0 TO roomCount - 1
            ' Place 1-2 torches per room
            DIM numTorches AS INTEGER
            numTorches = 1 + INT(RND() * 2)

            FOR i = 1 TO numTorches
                DIM x AS INTEGER
                DIM y AS INTEGER

                ' Place on a wall adjacent to floor
                DIM attempts AS INTEGER
                attempts = 0
                DO
                    x = rooms(r).GetX1() + INT(RND() * rooms(r).GetWidth())
                    y = rooms(r).GetY1() + INT(RND() * rooms(r).GetHeight())
                    attempts = attempts + 1
                    IF attempts > 20 THEN EXIT DO
                LOOP WHILE Me.GetTile(x, y) <> TILE_FLOOR

                ' Set light level around torch
                IF Me.GetTile(x, y) = TILE_FLOOR THEN
                    Me.AddLight(x, y, 4)
                END IF
            NEXT i
        NEXT r
    END SUB

    SUB AddLight(cx AS INTEGER, cy AS INTEGER, radius AS INTEGER)
        DIM x AS INTEGER
        DIM y AS INTEGER
        DIM dx AS INTEGER
        DIM dy AS INTEGER
        DIM dist AS INTEGER

        FOR dy = -radius TO radius
            FOR dx = -radius TO radius
                x = cx + dx
                y = cy + dy
                IF x >= 0 THEN
                    IF x < mapWidth THEN
                        IF y >= 0 THEN
                            IF y < mapHeight THEN
                                dist = Viper.Math.Abs(dx) + Viper.Math.Abs(dy)
                                IF dist <= radius THEN
                                    DIM idx AS INTEGER
                                    idx = Me.GetIndex(x, y)
                                    DIM newLight AS INTEGER
                                    newLight = radius - dist + 1
                                    IF newLight > light(idx) THEN light(idx) = newLight
                                END IF
                            END IF
                        END IF
                    END IF
                END IF
            NEXT dx
        NEXT dy
    END SUB

    FUNCTION GetLight(x AS INTEGER, y AS INTEGER) AS INTEGER
        IF x < 0 THEN GetLight = 0 : EXIT FUNCTION
        IF x >= mapWidth THEN GetLight = 0 : EXIT FUNCTION
        IF y < 0 THEN GetLight = 0 : EXIT FUNCTION
        IF y >= mapHeight THEN GetLight = 0 : EXIT FUNCTION
        GetLight = light(Me.GetIndex(x, y))
    END FUNCTION

    FUNCTION GetStartX() AS INTEGER
        GetStartX = startX
    END FUNCTION

    FUNCTION GetStartY() AS INTEGER
        GetStartY = startY
    END FUNCTION

    FUNCTION GetStairsDownX() AS INTEGER
        GetStairsDownX = stairsDownX
    END FUNCTION

    FUNCTION GetStairsDownY() AS INTEGER
        GetStairsDownY = stairsDownY
    END FUNCTION

    FUNCTION GetStairsUpX() AS INTEGER
        GetStairsUpX = stairsUpX
    END FUNCTION

    FUNCTION GetStairsUpY() AS INTEGER
        GetStairsUpY = stairsUpY
    END FUNCTION

    FUNCTION GetRoomCount() AS INTEGER
        GetRoomCount = roomCount
    END FUNCTION

    FUNCTION GetRoom(idx AS INTEGER) AS Room
        GetRoom = rooms(idx)
    END FUNCTION

    FUNCTION GetWidth() AS INTEGER
        GetWidth = mapWidth
    END FUNCTION

    FUNCTION GetHeight() AS INTEGER
        GetHeight = mapHeight
    END FUNCTION

    FUNCTION GetFloor() AS INTEGER
        GetFloor = floorLevel
    END FUNCTION

    ' Get tile symbol for rendering
    FUNCTION GetTileSymbol(x AS INTEGER, y AS INTEGER) AS STRING
        DIM t AS INTEGER
        t = Me.GetTile(x, y)

        IF t = TILE_VOID THEN GetTileSymbol = " "
        IF t = TILE_FLOOR THEN GetTileSymbol = "."
        IF t = TILE_WALL THEN GetTileSymbol = "#"
        IF t = TILE_DOOR_CLOSED THEN GetTileSymbol = "+"
        IF t = TILE_DOOR_OPEN THEN GetTileSymbol = "/"
        IF t = TILE_DOOR_LOCKED THEN GetTileSymbol = "+"
        IF t = TILE_STAIRS_DOWN THEN GetTileSymbol = ">"
        IF t = TILE_STAIRS_UP THEN GetTileSymbol = "<"
        IF t = TILE_WATER THEN GetTileSymbol = "~"
        IF t = TILE_LAVA THEN GetTileSymbol = "~"
        IF t = TILE_TRAP_HIDDEN THEN GetTileSymbol = "."
        IF t = TILE_TRAP_VISIBLE THEN GetTileSymbol = "^"
        IF t = TILE_SECRET_WALL THEN GetTileSymbol = "#"
        IF t = TILE_TORCH THEN GetTileSymbol = "*"
        IF t = TILE_SHRINE THEN GetTileSymbol = "_"
        IF t = TILE_CHEST THEN GetTileSymbol = "="
    END FUNCTION

    ' Get tile color for rendering
    FUNCTION GetTileColor(x AS INTEGER, y AS INTEGER) AS INTEGER
        DIM t AS INTEGER
        t = Me.GetTile(x, y)

        IF t = TILE_VOID THEN GetTileColor = CLR_BLACK
        IF t = TILE_FLOOR THEN GetTileColor = CLR_WHITE
        IF t = TILE_WALL THEN GetTileColor = CLR_WHITE
        IF t = TILE_DOOR_CLOSED THEN GetTileColor = CLR_YELLOW
        IF t = TILE_DOOR_OPEN THEN GetTileColor = CLR_YELLOW
        IF t = TILE_DOOR_LOCKED THEN GetTileColor = CLR_RED
        IF t = TILE_STAIRS_DOWN THEN GetTileColor = CLR_BRIGHT_WHITE
        IF t = TILE_STAIRS_UP THEN GetTileColor = CLR_BRIGHT_WHITE
        IF t = TILE_WATER THEN GetTileColor = CLR_BLUE
        IF t = TILE_LAVA THEN GetTileColor = CLR_BRIGHT_RED
        IF t = TILE_TRAP_HIDDEN THEN GetTileColor = CLR_WHITE
        IF t = TILE_TRAP_VISIBLE THEN GetTileColor = CLR_RED
        IF t = TILE_SECRET_WALL THEN GetTileColor = CLR_WHITE
        IF t = TILE_TORCH THEN GetTileColor = CLR_BRIGHT_YELLOW
        IF t = TILE_SHRINE THEN GetTileColor = CLR_BRIGHT_CYAN
        IF t = TILE_CHEST THEN GetTileColor = CLR_BRIGHT_YELLOW
    END FUNCTION

    ' Reveal a trap
    SUB RevealTrap(x AS INTEGER, y AS INTEGER)
        IF Me.GetTile(x, y) = TILE_TRAP_HIDDEN THEN
            Me.SetTile(x, y, TILE_TRAP_VISIBLE)
        END IF
    END SUB
END CLASS
