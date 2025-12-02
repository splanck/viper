' fov.bas - Field of View system using recursive shadowcasting
' Based on the classic roguelike shadowcasting algorithm

' ============================================================================
' FOV CALCULATOR CLASS
' ============================================================================
CLASS FOVCalculator
    ' Octant multipliers for shadowcasting
    ' xx, xy, yx, yy for each of 8 octants
    DIM mult_xx(7) AS INTEGER
    DIM mult_xy(7) AS INTEGER
    DIM mult_yx(7) AS INTEGER
    DIM mult_yy(7) AS INTEGER

    ' Reference to dungeon map
    DIM mapRef AS DungeonMap

    SUB Init()
        ' Octant 0: E  to NE
        mult_xx(0) = 1 : mult_xy(0) = 0 : mult_yx(0) = 0 : mult_yy(0) = -1
        ' Octant 1: NE to N
        mult_xx(1) = 0 : mult_xy(1) = -1 : mult_yx(1) = 1 : mult_yy(1) = 0
        ' Octant 2: N  to NW
        mult_xx(2) = 0 : mult_xy(2) = -1 : mult_yx(2) = -1 : mult_yy(2) = 0
        ' Octant 3: NW to W
        mult_xx(3) = -1 : mult_xy(3) = 0 : mult_yx(3) = 0 : mult_yy(3) = -1
        ' Octant 4: W  to SW
        mult_xx(4) = -1 : mult_xy(4) = 0 : mult_yx(4) = 0 : mult_yy(4) = 1
        ' Octant 5: SW to S
        mult_xx(5) = 0 : mult_xy(5) = 1 : mult_yx(5) = -1 : mult_yy(5) = 0
        ' Octant 6: S  to SE
        mult_xx(6) = 0 : mult_xy(6) = 1 : mult_yx(6) = 1 : mult_yy(6) = 0
        ' Octant 7: SE to E
        mult_xx(7) = 1 : mult_xy(7) = 0 : mult_yx(7) = 0 : mult_yy(7) = 1
    END SUB

    SUB SetMap(dm AS DungeonMap)
        mapRef = dm
    END SUB

    ' Calculate FOV from origin with given radius
    SUB Calculate(originX AS INTEGER, originY AS INTEGER, radius AS INTEGER)
        ' Clear all visibility
        mapRef.ClearVisible()

        ' Origin is always visible
        mapRef.SetVisible(originX, originY, 1)

        ' Cast light in all 8 octants
        DIM oct AS INTEGER
        FOR oct = 0 TO 7
            Me.CastLight(originX, originY, radius, 1, 1.0, 0.0, _
                         mult_xx(oct), mult_xy(oct), mult_yx(oct), mult_yy(oct))
        NEXT oct
    END SUB

    ' Recursive shadowcasting for one octant
    ' Uses iterative approach since Viper BASIC doesn't support deep recursion well
    SUB CastLight(ox AS INTEGER, oy AS INTEGER, radius AS INTEGER, row AS INTEGER, _
                  startSlope AS DOUBLE, endSlope AS DOUBLE, _
                  xx AS INTEGER, xy AS INTEGER, yx AS INTEGER, yy AS INTEGER)
        DIM newStart AS DOUBLE
        DIM blocked AS INTEGER
        DIM currentRow AS INTEGER
        DIM dx AS INTEGER
        DIM dy AS INTEGER
        DIM mapX AS INTEGER
        DIM mapY AS INTEGER
        DIM leftSlope AS DOUBLE
        DIM rightSlope AS DOUBLE
        DIM distSq AS INTEGER
        DIM radSq AS INTEGER

        IF startSlope < endSlope THEN EXIT SUB

        radSq = radius * radius
        blocked = 0
        newStart = startSlope

        currentRow = row
        DO WHILE currentRow <= radius
            IF blocked = 1 THEN EXIT DO

            dy = -currentRow
            dx = -currentRow - 1

            DO WHILE dx <= 0
                dx = dx + 1

                ' Transform to map coordinates
                mapX = ox + dx * xx + dy * xy
                mapY = oy + dx * yx + dy * yy

                ' Calculate slopes
                leftSlope = (dx - 0.5) / (dy + 0.5)
                rightSlope = (dx + 0.5) / (dy - 0.5)

                IF startSlope < rightSlope THEN
                    ' Continue to next cell
                ELSEIF endSlope > leftSlope THEN
                    EXIT DO
                ELSE
                    ' Check if within radius
                    distSq = dx * dx + dy * dy
                    IF distSq <= radSq THEN
                        mapRef.SetVisible(mapX, mapY, 1)
                    END IF

                    IF blocked = 1 THEN
                        IF mapRef.BlocksSight(mapX, mapY) = 1 THEN
                            newStart = rightSlope
                        ELSE
                            blocked = 0
                            startSlope = newStart
                        END IF
                    ELSE
                        IF mapRef.BlocksSight(mapX, mapY) = 1 THEN
                            IF distSq <= radSq THEN
                                blocked = 1
                                ' Recursively scan next row with narrowed scope
                                Me.CastLight(ox, oy, radius, currentRow + 1, startSlope, leftSlope, _
                                            xx, xy, yx, yy)
                                newStart = rightSlope
                            END IF
                        END IF
                    END IF
                END IF
            LOOP

            currentRow = currentRow + 1
        LOOP
    END SUB
END CLASS

' ============================================================================
' PATHFINDER CLASS - A* pathfinding
' ============================================================================
CLASS Pathfinder
    ' Open and closed lists as simple arrays
    ' Each node: x, y, g, h, f, parentIdx
    DIM nodeX(999) AS INTEGER
    DIM nodeY(999) AS INTEGER
    DIM nodeG(999) AS INTEGER
    DIM nodeH(999) AS INTEGER
    DIM nodeF(999) AS INTEGER
    DIM nodeParent(999) AS INTEGER
    DIM nodeOpen(999) AS INTEGER      ' 1 = open, 0 = closed
    DIM nodeCount AS INTEGER

    ' Path result
    DIM pathX(99) AS INTEGER
    DIM pathY(99) AS INTEGER
    DIM pathLen AS INTEGER

    ' Map reference
    DIM mapRef AS DungeonMap

    SUB Init()
        nodeCount = 0
        pathLen = 0
    END SUB

    SUB SetMap(dm AS DungeonMap)
        mapRef = dm
    END SUB

    ' Heuristic: Manhattan distance
    FUNCTION Heuristic(x1 AS INTEGER, y1 AS INTEGER, x2 AS INTEGER, y2 AS INTEGER) AS INTEGER
        DIM dx AS INTEGER
        DIM dy AS INTEGER
        dx = x1 - x2
        dy = y1 - y2
        IF dx < 0 THEN dx = -dx
        IF dy < 0 THEN dy = -dy
        Heuristic = dx + dy
    END FUNCTION

    ' Find node index for position, -1 if not found
    FUNCTION FindNode(x AS INTEGER, y AS INTEGER) AS INTEGER
        DIM i AS INTEGER
        FindNode = -1
        FOR i = 0 TO nodeCount - 1
            IF nodeX(i) = x THEN
                IF nodeY(i) = y THEN
                    FindNode = i
                    EXIT FUNCTION
                END IF
            END IF
        NEXT i
    END FUNCTION

    ' Find best open node (lowest F)
    FUNCTION FindBestOpen() AS INTEGER
        DIM i AS INTEGER
        DIM bestIdx AS INTEGER
        DIM bestF AS INTEGER

        bestIdx = -1
        bestF = 999999

        FOR i = 0 TO nodeCount - 1
            IF nodeOpen(i) = 1 THEN
                IF nodeF(i) < bestF THEN
                    bestF = nodeF(i)
                    bestIdx = i
                END IF
            END IF
        NEXT i

        FindBestOpen = bestIdx
    END FUNCTION

    ' Add a node
    FUNCTION AddNode(x AS INTEGER, y AS INTEGER, g AS INTEGER, h AS INTEGER, parent AS INTEGER) AS INTEGER
        IF nodeCount >= 1000 THEN
            AddNode = -1
            EXIT FUNCTION
        END IF

        nodeX(nodeCount) = x
        nodeY(nodeCount) = y
        nodeG(nodeCount) = g
        nodeH(nodeCount) = h
        nodeF(nodeCount) = g + h
        nodeParent(nodeCount) = parent
        nodeOpen(nodeCount) = 1

        AddNode = nodeCount
        nodeCount = nodeCount + 1
    END FUNCTION

    ' Find path from (sx, sy) to (tx, ty)
    ' Returns path length, 0 if no path
    FUNCTION FindPath(sx AS INTEGER, sy AS INTEGER, tx AS INTEGER, ty AS INTEGER) AS INTEGER
        DIM i AS INTEGER
        DIM current AS INTEGER
        DIM nx AS INTEGER
        DIM ny AS INTEGER
        DIM ng AS INTEGER
        DIM nh AS INTEGER
        DIM existingIdx AS INTEGER
        DIM dx AS INTEGER
        DIM dy AS INTEGER

        ' Reset
        nodeCount = 0
        pathLen = 0

        ' Add start node
        i = Me.AddNode(sx, sy, 0, Me.Heuristic(sx, sy, tx, ty), -1)

        ' Main loop
        DIM iterations AS INTEGER
        iterations = 0

        DO
            iterations = iterations + 1
            IF iterations > 2000 THEN EXIT DO

            current = Me.FindBestOpen()
            IF current < 0 THEN EXIT DO

            ' Close current node
            nodeOpen(current) = 0

            ' Check if reached target
            IF nodeX(current) = tx THEN
                IF nodeY(current) = ty THEN
                    ' Reconstruct path
                    Me.ReconstructPath(current)
                    FindPath = pathLen
                    EXIT FUNCTION
                END IF
            END IF

            ' Expand neighbors (4-directional)
            FOR dy = -1 TO 1
                FOR dx = -1 TO 1
                    ' Only cardinal directions
                    IF dx = 0 THEN
                        IF dy = 0 THEN GOTO ContinueNeighbor
                    END IF
                    IF dx <> 0 THEN
                        IF dy <> 0 THEN GOTO ContinueNeighbor
                    END IF

                    nx = nodeX(current) + dx
                    ny = nodeY(current) + dy

                    ' Check walkable
                    IF mapRef.IsWalkable(nx, ny) = 0 THEN GOTO ContinueNeighbor

                    ng = nodeG(current) + 1
                    nh = Me.Heuristic(nx, ny, tx, ty)

                    existingIdx = Me.FindNode(nx, ny)
                    IF existingIdx >= 0 THEN
                        ' Already in list - check if better path
                        IF ng < nodeG(existingIdx) THEN
                            nodeG(existingIdx) = ng
                            nodeF(existingIdx) = ng + nodeH(existingIdx)
                            nodeParent(existingIdx) = current
                            nodeOpen(existingIdx) = 1
                        END IF
                    ELSE
                        ' Add new node
                        i = Me.AddNode(nx, ny, ng, nh, current)
                    END IF

                    ContinueNeighbor:
                NEXT dx
            NEXT dy
        LOOP

        FindPath = 0
    END FUNCTION

    ' Reconstruct path from goal node
    SUB ReconstructPath(goalIdx AS INTEGER)
        DIM i AS INTEGER
        DIM current AS INTEGER
        DIM tempX(99) AS INTEGER
        DIM tempY(99) AS INTEGER
        DIM tempLen AS INTEGER

        tempLen = 0
        current = goalIdx

        DO WHILE current >= 0
            IF tempLen >= 100 THEN EXIT DO
            tempX(tempLen) = nodeX(current)
            tempY(tempLen) = nodeY(current)
            tempLen = tempLen + 1
            current = nodeParent(current)
        LOOP

        ' Reverse path (excluding start position)
        pathLen = 0
        FOR i = tempLen - 2 TO 0 STEP -1
            pathX(pathLen) = tempX(i)
            pathY(pathLen) = tempY(i)
            pathLen = pathLen + 1
        NEXT i
    END SUB

    ' Get path step
    FUNCTION GetPathX(idx AS INTEGER) AS INTEGER
        IF idx >= 0 THEN
            IF idx < pathLen THEN
                GetPathX = pathX(idx)
                EXIT FUNCTION
            END IF
        END IF
        GetPathX = -1
    END FUNCTION

    FUNCTION GetPathY(idx AS INTEGER) AS INTEGER
        IF idx >= 0 THEN
            IF idx < pathLen THEN
                GetPathY = pathY(idx)
                EXIT FUNCTION
            END IF
        END IF
        GetPathY = -1
    END FUNCTION

    FUNCTION GetPathLength() AS INTEGER
        GetPathLength = pathLen
    END FUNCTION
END CLASS
