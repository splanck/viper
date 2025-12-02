' combat.bas - Combat and AI systems
' Handles attack resolution, damage calculation, and monster AI

' ============================================================================
' COMBAT RESULT CLASS
' ============================================================================
CLASS CombatResult
    DIM hit AS INTEGER
    DIM damage AS INTEGER
    DIM critical AS INTEGER
    DIM killed AS INTEGER
    DIM message AS STRING
    DIM lifestealAmount AS INTEGER

    SUB Init()
        hit = 0
        damage = 0
        critical = 0
        killed = 0
        message = ""
        lifestealAmount = 0
    END SUB

    SUB SetHit(h AS INTEGER)
        hit = h
    END SUB

    SUB SetDamage(d AS INTEGER)
        damage = d
    END SUB

    SUB SetCritical(c AS INTEGER)
        critical = c
    END SUB

    SUB SetKilled(k AS INTEGER)
        killed = k
    END SUB

    SUB SetMessage(m AS STRING)
        message = m
    END SUB

    SUB SetLifesteal(ls AS INTEGER)
        lifestealAmount = ls
    END SUB

    FUNCTION DidHit() AS INTEGER
        DidHit = hit
    END FUNCTION

    FUNCTION GetDamage() AS INTEGER
        GetDamage = damage
    END FUNCTION

    FUNCTION WasCritical() AS INTEGER
        WasCritical = critical
    END FUNCTION

    FUNCTION TargetKilled() AS INTEGER
        TargetKilled = killed
    END FUNCTION

    FUNCTION GetMessage() AS STRING
        GetMessage = message
    END FUNCTION

    FUNCTION GetLifesteal() AS INTEGER
        GetLifesteal = lifestealAmount
    END FUNCTION
END CLASS

' ============================================================================
' COMBAT SYSTEM CLASS
' ============================================================================
CLASS CombatSystem
    DIM lastResult AS CombatResult

    SUB Init()
        lastResult = NEW CombatResult()
        lastResult.Init()
    END SUB

    ' Calculate hit chance
    FUNCTION CalculateHitChance(attackerAtk AS INTEGER, defenderDef AS INTEGER) AS DOUBLE
        DIM baseChance AS DOUBLE
        DIM diff AS INTEGER

        diff = attackerAtk - defenderDef
        baseChance = 0.7 + diff * 0.05

        IF baseChance < 0.2 THEN baseChance = 0.2
        IF baseChance > 0.95 THEN baseChance = 0.95

        CalculateHitChance = baseChance
    END FUNCTION

    ' Calculate damage
    FUNCTION CalculateDamage(minDmg AS INTEGER, maxDmg AS INTEGER, defense AS INTEGER) AS INTEGER
        DIM baseDmg AS INTEGER
        DIM finalDmg AS INTEGER

        baseDmg = minDmg + INT(RND() * (maxDmg - minDmg + 1))
        finalDmg = baseDmg - defense / 2

        IF finalDmg < 1 THEN finalDmg = 1
        CalculateDamage = finalDmg
    END FUNCTION

    ' Resolve melee attack
    FUNCTION MeleeAttack(attackerCombat AS CombatComponent, attackerName AS STRING, _
                          defenderHealth AS HealthComponent, defenderCombat AS CombatComponent, _
                          defenderName AS STRING) AS CombatResult
        DIM result AS CombatResult
        result = NEW CombatResult()
        result.Init()

        DIM sb AS Viper.Text.StringBuilder
        sb = NEW Viper.Text.StringBuilder()

        ' Roll to hit
        DIM hitChance AS DOUBLE
        hitChance = Me.CalculateHitChance(attackerCombat.GetAttack(), defenderCombat.GetDefense())

        IF RND() > hitChance THEN
            result.SetHit(0)
            sb.Append(attackerName)
            sb.Append(" misses ")
            sb.Append(defenderName)
            sb.Append("!")
            result.SetMessage(sb.ToString())
            MeleeAttack = result
            EXIT FUNCTION
        END IF

        result.SetHit(1)

        ' Calculate damage
        DIM dmg AS INTEGER
        dmg = Me.CalculateDamage(attackerCombat.GetMinDamage(), attackerCombat.GetMaxDamage(), _
                                  defenderCombat.GetDefense())

        ' Check for critical hit
        IF RND() * 100 < attackerCombat.GetCritChance() THEN
            dmg = dmg * 2
            result.SetCritical(1)
        END IF

        result.SetDamage(dmg)

        ' Apply damage
        defenderHealth.TakeDamage(dmg)

        ' Check for lifesteal
        DIM lifesteal AS INTEGER
        lifesteal = INT(dmg * attackerCombat.GetLifesteal())
        IF lifesteal > 0 THEN
            result.SetLifesteal(lifesteal)
        END IF

        ' Build message
        sb.Clear()
        sb.Append(attackerName)
        IF result.WasCritical() = 1 THEN
            sb.Append(" CRITICALLY hits ")
        ELSE
            sb.Append(" hits ")
        END IF
        sb.Append(defenderName)
        sb.Append(" for ")
        sb.Append(Viper.Convert.ToString(dmg))
        sb.Append(" damage")

        IF lifesteal > 0 THEN
            sb.Append(" (heals ")
            sb.Append(Viper.Convert.ToString(lifesteal))
            sb.Append(")")
        END IF

        IF defenderHealth.IsDead() = 1 THEN
            result.SetKilled(1)
            sb.Append(" - KILLED!")
        ELSE
            sb.Append("!")
        END IF

        result.SetMessage(sb.ToString())
        MeleeAttack = result
    END FUNCTION

    FUNCTION GetLastResult() AS CombatResult
        GetLastResult = lastResult
    END FUNCTION
END CLASS

' ============================================================================
' AI SYSTEM CLASS
' ============================================================================
CLASS AISystem
    DIM pathfinder AS Pathfinder

    SUB Init()
        pathfinder = NEW Pathfinder()
        pathfinder.Init()
    END SUB

    SUB SetMap(dm AS DungeonMap)
        pathfinder.SetMap(dm)
    END SUB

    ' Get direction toward target
    FUNCTION GetDirectionToward(fromX AS INTEGER, fromY AS INTEGER, _
                                 toX AS INTEGER, toY AS INTEGER) AS INTEGER
        DIM dx AS INTEGER
        DIM dy AS INTEGER

        dx = toX - fromX
        dy = toY - fromY

        ' Normalize
        IF dx > 0 THEN dx = 1
        IF dx < 0 THEN dx = -1
        IF dy > 0 THEN dy = 1
        IF dy < 0 THEN dy = -1

        ' Convert to direction
        IF dx = 0 THEN
            IF dy = -1 THEN GetDirectionToward = DIR_N
            IF dy = 1 THEN GetDirectionToward = DIR_S
            IF dy = 0 THEN GetDirectionToward = DIR_NONE
        ELSEIF dx = 1 THEN
            IF dy = -1 THEN GetDirectionToward = DIR_NE
            IF dy = 0 THEN GetDirectionToward = DIR_E
            IF dy = 1 THEN GetDirectionToward = DIR_SE
        ELSE
            IF dy = -1 THEN GetDirectionToward = DIR_NW
            IF dy = 0 THEN GetDirectionToward = DIR_W
            IF dy = 1 THEN GetDirectionToward = DIR_SW
        END IF
    END FUNCTION

    ' Get direction away from target
    FUNCTION GetDirectionAway(fromX AS INTEGER, fromY AS INTEGER, _
                               toX AS INTEGER, toY AS INTEGER) AS INTEGER
        DIM dx AS INTEGER
        DIM dy AS INTEGER

        dx = fromX - toX
        dy = fromY - toY

        ' Normalize
        IF dx > 0 THEN dx = 1
        IF dx < 0 THEN dx = -1
        IF dy > 0 THEN dy = 1
        IF dy < 0 THEN dy = -1

        ' Convert to direction
        IF dx = 0 THEN
            IF dy = -1 THEN GetDirectionAway = DIR_N
            IF dy = 1 THEN GetDirectionAway = DIR_S
            IF dy = 0 THEN GetDirectionAway = DIR_NONE
        ELSEIF dx = 1 THEN
            IF dy = -1 THEN GetDirectionAway = DIR_NE
            IF dy = 0 THEN GetDirectionAway = DIR_E
            IF dy = 1 THEN GetDirectionAway = DIR_SE
        ELSE
            IF dy = -1 THEN GetDirectionAway = DIR_NW
            IF dy = 0 THEN GetDirectionAway = DIR_W
            IF dy = 1 THEN GetDirectionAway = DIR_SW
        END IF
    END FUNCTION

    ' Get a random direction
    FUNCTION GetRandomDirection() AS INTEGER
        GetRandomDirection = 1 + INT(RND() * 8)
    END FUNCTION

    ' Get move deltas for direction
    SUB GetDirectionDeltas(dir AS INTEGER, BYREF dx AS INTEGER, BYREF dy AS INTEGER)
        dx = 0
        dy = 0
        SELECT CASE dir
            CASE DIR_N  : dy = -1
            CASE DIR_NE : dx = 1 : dy = -1
            CASE DIR_E  : dx = 1
            CASE DIR_SE : dx = 1 : dy = 1
            CASE DIR_S  : dy = 1
            CASE DIR_SW : dx = -1 : dy = 1
            CASE DIR_W  : dx = -1
            CASE DIR_NW : dx = -1 : dy = -1
        END SELECT
    END SUB

    ' Calculate Manhattan distance
    FUNCTION Distance(x1 AS INTEGER, y1 AS INTEGER, x2 AS INTEGER, y2 AS INTEGER) AS INTEGER
        DIM dx AS INTEGER
        DIM dy AS INTEGER
        dx = x1 - x2
        dy = y1 - y2
        IF dx < 0 THEN dx = -dx
        IF dy < 0 THEN dy = -dy
        Distance = dx + dy
    END FUNCTION

    ' Get AI move for monster
    FUNCTION GetAIMove(monster AS Monster, playerX AS INTEGER, playerY AS INTEGER, _
                        canSeePlayer AS INTEGER, dm AS DungeonMap) AS INTEGER
        DIM aiComp AS AIComponent
        DIM healthComp AS HealthComponent
        DIM behavior AS INTEGER
        DIM state AS INTEGER
        DIM dir AS INTEGER
        DIM dist AS INTEGER

        aiComp = monster.GetAI()
        healthComp = monster.GetHealth()
        behavior = aiComp.GetBehavior()
        state = aiComp.GetState()

        dist = Me.Distance(monster.GetX(), monster.GetY(), playerX, playerY)

        ' Update state based on situation
        IF canSeePlayer = 1 THEN
            monster.RememberPlayerPos(playerX, playerY)
            aiComp.SetAlertness(10)

            ' Check health for fleeing
            IF healthComp.GetCurrent() < healthComp.GetMax() / 4 THEN
                IF behavior = AI_COWARD THEN
                    aiComp.SetState(STATE_FLEEING)
                END IF
            END IF

            ' Aggressive monsters attack
            IF behavior = AI_AGGRESSIVE THEN
                IF state <> STATE_FLEEING THEN
                    aiComp.SetState(STATE_HUNTING)
                END IF
            END IF

            ' Pack monsters alert others (simplified - just go aggressive)
            IF behavior = AI_PACK THEN
                aiComp.SetState(STATE_HUNTING)
            END IF

            ' Ranged monsters keep distance
            IF behavior = AI_RANGED THEN
                IF dist < 3 THEN
                    aiComp.SetState(STATE_FLEEING)
                ELSE
                    aiComp.SetState(STATE_ATTACKING)
                END IF
            END IF

            ' Cowards flee
            IF behavior = AI_COWARD THEN
                aiComp.SetState(STATE_FLEEING)
            END IF
        ELSE
            ' Can't see player - decay alertness
            aiComp.DecayAlertness()
            IF aiComp.GetAlertness() <= 0 THEN
                aiComp.SetState(STATE_IDLE)
            END IF
        END IF

        ' Get move based on state
        state = aiComp.GetState()

        SELECT CASE state
            CASE STATE_IDLE
                ' Patrol behavior - random movement
                IF behavior = AI_PATROL THEN
                    IF RND() < 0.3 THEN
                        dir = Me.GetRandomDirection()
                    ELSE
                        dir = DIR_NONE
                    END IF
                ELSE
                    ' Stand still or occasional wander
                    IF RND() < 0.1 THEN
                        dir = Me.GetRandomDirection()
                    ELSE
                        dir = DIR_NONE
                    END IF
                END IF

            CASE STATE_HUNTING
                ' Move toward player
                IF dist > 1 THEN
                    ' Use pathfinding for smarter movement
                    pathfinder.SetMap(dm)
                    DIM pathLen AS INTEGER
                    pathLen = pathfinder.FindPath(monster.GetX(), monster.GetY(), playerX, playerY)

                    IF pathLen > 0 THEN
                        DIM nextX AS INTEGER
                        DIM nextY AS INTEGER
                        nextX = pathfinder.GetPathX(0)
                        nextY = pathfinder.GetPathY(0)
                        dir = Me.GetDirectionToward(monster.GetX(), monster.GetY(), nextX, nextY)
                    ELSE
                        ' Direct approach if no path
                        dir = Me.GetDirectionToward(monster.GetX(), monster.GetY(), playerX, playerY)
                    END IF
                ELSE
                    ' Adjacent - attack (return special code)
                    dir = -1
                END IF

            CASE STATE_FLEEING
                ' Move away from player
                dir = Me.GetDirectionAway(monster.GetX(), monster.GetY(), playerX, playerY)

            CASE STATE_ATTACKING
                ' Ranged attack position
                IF dist > 5 THEN
                    dir = Me.GetDirectionToward(monster.GetX(), monster.GetY(), playerX, playerY)
                ELSEIF dist < 3 THEN
                    dir = Me.GetDirectionAway(monster.GetX(), monster.GetY(), playerX, playerY)
                ELSE
                    dir = -2    ' Ranged attack
                END IF

            CASE ELSE
                dir = DIR_NONE
        END SELECT

        GetAIMove = dir
    END FUNCTION
END CLASS
