' monsters.bas - Monster definitions and factory
' 30+ monster types across different floor tiers

' ============================================================================
' MONSTER CLASS - Extends Entity with monster-specific data
' ============================================================================
CLASS Monster
    DIM baseEntity AS Entity
    DIM health AS HealthComponent
    DIM combat AS CombatComponent
    DIM ai AS AIComponent
    DIM stats AS StatsComponent
    DIM status AS StatusComponent

    DIM monsterType AS INTEGER
    DIM isAlive AS INTEGER
    DIM lastSeenPlayerX AS INTEGER
    DIM lastSeenPlayerY AS INTEGER
    DIM xpValue AS INTEGER
    DIM dropChance AS DOUBLE

    SUB Init(mType AS INTEGER, px AS INTEGER, py AS INTEGER)
        monsterType = mType
        isAlive = 1
        lastSeenPlayerX = -1
        lastSeenPlayerY = -1

        baseEntity = NEW Entity()
        health = NEW HealthComponent()
        combat = NEW CombatComponent()
        ai = NEW AIComponent()
        stats = NEW StatsComponent()
        status = NEW StatusComponent()

        baseEntity.Init(px, py, "?", CLR_WHITE, CLR_BLACK)
        health.Init(10, 0)
        combat.Init(1, 0, 1, 3, 5)
        ai.Init(AI_AGGRESSIVE)
        stats.Init(CLASS_WARRIOR)
        status.Init()

        ' Configure based on monster type
        Me.ConfigureMonster()
    END SUB

    SUB ConfigureMonster()
        ' Set stats based on monster type
        SELECT CASE monsterType
            ' ========== BASIC (Floors 1-5) ==========
            CASE MON_RAT
                baseEntity.SetSymbol("r") : baseEntity.SetFgColor(CLR_WHITE)
                health.SetMax(6) : health.SetCurrent(6)
                combat.Init(2, 0, 1, 2, 5)
                ai.Init(AI_COWARD)
                xpValue = 5 : dropChance = 0.1

            CASE MON_BAT
                baseEntity.SetSymbol("b") : baseEntity.SetFgColor(CLR_WHITE)
                health.SetMax(4) : health.SetCurrent(4)
                combat.Init(3, 0, 1, 2, 10)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 5 : dropChance = 0.05

            CASE MON_GOBLIN
                baseEntity.SetSymbol("g") : baseEntity.SetFgColor(CLR_GREEN)
                health.SetMax(12) : health.SetCurrent(12)
                combat.Init(4, 1, 2, 4, 5)
                ai.Init(AI_PACK)
                xpValue = 15 : dropChance = 0.3

            CASE MON_SKELETON
                baseEntity.SetSymbol("s") : baseEntity.SetFgColor(CLR_WHITE)
                health.SetMax(15) : health.SetCurrent(15)
                combat.Init(5, 2, 2, 5, 5)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 20 : dropChance = 0.25

            CASE MON_ZOMBIE
                baseEntity.SetSymbol("z") : baseEntity.SetFgColor(CLR_GREEN)
                health.SetMax(20) : health.SetCurrent(20)
                combat.Init(4, 1, 3, 6, 3)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 18 : dropChance = 0.15

            CASE MON_SNAKE
                baseEntity.SetSymbol("S") : baseEntity.SetFgColor(CLR_GREEN)
                health.SetMax(8) : health.SetCurrent(8)
                combat.Init(5, 0, 1, 3, 15)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 12 : dropChance = 0.1

            CASE MON_KOBOLD
                baseEntity.SetSymbol("k") : baseEntity.SetFgColor(CLR_YELLOW)
                health.SetMax(10) : health.SetCurrent(10)
                combat.Init(4, 1, 1, 4, 8)
                ai.Init(AI_PACK)
                xpValue = 12 : dropChance = 0.35

            ' ========== INTERMEDIATE (Floors 6-10) ==========
            CASE MON_ORC
                baseEntity.SetSymbol("o") : baseEntity.SetFgColor(CLR_GREEN)
                health.SetMax(25) : health.SetCurrent(25)
                combat.Init(7, 3, 3, 7, 5)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 35 : dropChance = 0.4

            CASE MON_TROLL
                baseEntity.SetSymbol("T") : baseEntity.SetFgColor(CLR_GREEN)
                health.SetMax(40) : health.SetCurrent(40)
                health.SetRegen(1)
                combat.Init(8, 2, 4, 10, 3)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 60 : dropChance = 0.35

            CASE MON_GHOST
                baseEntity.SetSymbol("G") : baseEntity.SetFgColor(CLR_BRIGHT_WHITE)
                health.SetMax(18) : health.SetCurrent(18)
                combat.Init(6, 5, 2, 6, 10)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 45 : dropChance = 0.2

            CASE MON_SPIDER
                baseEntity.SetSymbol("x") : baseEntity.SetFgColor(CLR_RED)
                health.SetMax(15) : health.SetCurrent(15)
                combat.Init(6, 1, 2, 5, 12)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 30 : dropChance = 0.25

            CASE MON_MUMMY
                baseEntity.SetSymbol("M") : baseEntity.SetFgColor(CLR_YELLOW)
                health.SetMax(30) : health.SetCurrent(30)
                combat.Init(6, 4, 3, 8, 4)
                ai.Init(AI_PATROL)
                xpValue = 50 : dropChance = 0.45

            CASE MON_OGRE
                baseEntity.SetSymbol("O") : baseEntity.SetFgColor(CLR_YELLOW)
                health.SetMax(45) : health.SetCurrent(45)
                combat.Init(9, 2, 5, 12, 3)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 65 : dropChance = 0.4

            CASE MON_HARPY
                baseEntity.SetSymbol("H") : baseEntity.SetFgColor(CLR_MAGENTA)
                health.SetMax(22) : health.SetCurrent(22)
                combat.Init(7, 1, 3, 6, 15)
                ai.Init(AI_RANGED)
                xpValue = 40 : dropChance = 0.3

            ' ========== ADVANCED (Floors 11-15) ==========
            CASE MON_VAMPIRE
                baseEntity.SetSymbol("V") : baseEntity.SetFgColor(CLR_RED)
                health.SetMax(50) : health.SetCurrent(50)
                combat.Init(10, 4, 4, 10, 12)
                combat.SetLifesteal(0.3)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 100 : dropChance = 0.5

            CASE MON_WEREWOLF
                baseEntity.SetSymbol("W") : baseEntity.SetFgColor(CLR_YELLOW)
                health.SetMax(55) : health.SetCurrent(55)
                combat.Init(11, 3, 5, 12, 10)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 95 : dropChance = 0.45

            CASE MON_DEMON
                baseEntity.SetSymbol("&") : baseEntity.SetFgColor(CLR_BRIGHT_RED)
                health.SetMax(60) : health.SetCurrent(60)
                combat.Init(12, 5, 6, 14, 8)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 120 : dropChance = 0.5

            CASE MON_GOLEM
                baseEntity.SetSymbol("#") : baseEntity.SetFgColor(CLR_WHITE)
                health.SetMax(80) : health.SetCurrent(80)
                combat.Init(10, 8, 5, 15, 2)
                ai.Init(AI_PATROL)
                xpValue = 110 : dropChance = 0.4

            CASE MON_LICH
                baseEntity.SetSymbol("L") : baseEntity.SetFgColor(CLR_BRIGHT_CYAN)
                health.SetMax(45) : health.SetCurrent(45)
                combat.Init(14, 4, 8, 16, 10)
                ai.Init(AI_RANGED)
                xpValue = 150 : dropChance = 0.6

            CASE MON_WRAITH
                baseEntity.SetSymbol("w") : baseEntity.SetFgColor(CLR_BRIGHT_BLACK)
                health.SetMax(35) : health.SetCurrent(35)
                combat.Init(12, 6, 5, 12, 12)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 90 : dropChance = 0.35

            CASE MON_MINOTAUR
                baseEntity.SetSymbol("m") : baseEntity.SetFgColor(CLR_RED)
                health.SetMax(70) : health.SetCurrent(70)
                combat.Init(13, 4, 6, 16, 5)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 130 : dropChance = 0.5

            ' ========== ELITE (Floors 16-19) ==========
            CASE MON_DRAGON
                baseEntity.SetSymbol("D") : baseEntity.SetFgColor(CLR_BRIGHT_RED)
                health.SetMax(120) : health.SetCurrent(120)
                combat.Init(16, 8, 10, 25, 8)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 300 : dropChance = 0.7

            CASE MON_ARCHDEMON
                baseEntity.SetSymbol("&") : baseEntity.SetFgColor(CLR_BRIGHT_MAGENTA)
                health.SetMax(100) : health.SetCurrent(100)
                combat.Init(18, 7, 12, 22, 10)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 350 : dropChance = 0.75

            CASE MON_DEATH_KNIGHT
                baseEntity.SetSymbol("K") : baseEntity.SetFgColor(CLR_BRIGHT_BLACK)
                health.SetMax(90) : health.SetCurrent(90)
                combat.Init(15, 10, 8, 20, 8)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 280 : dropChance = 0.65

            CASE MON_BEHOLDER
                baseEntity.SetSymbol("e") : baseEntity.SetFgColor(CLR_BRIGHT_MAGENTA)
                health.SetMax(75) : health.SetCurrent(75)
                combat.Init(16, 5, 10, 18, 12)
                ai.Init(AI_RANGED)
                xpValue = 320 : dropChance = 0.7

            CASE MON_HYDRA
                baseEntity.SetSymbol("Y") : baseEntity.SetFgColor(CLR_GREEN)
                health.SetMax(150) : health.SetCurrent(150)
                health.SetRegen(3)
                combat.Init(14, 6, 8, 20, 6)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 400 : dropChance = 0.8

            ' ========== BOSSES ==========
            CASE MON_GOBLIN_KING
                baseEntity.SetSymbol("G") : baseEntity.SetFgColor(CLR_BRIGHT_GREEN)
                health.SetMax(100) : health.SetCurrent(100)
                combat.Init(12, 5, 6, 14, 8)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 500 : dropChance = 1.0

            CASE MON_VAMPIRE_LORD
                baseEntity.SetSymbol("V") : baseEntity.SetFgColor(CLR_BRIGHT_RED)
                health.SetMax(150) : health.SetCurrent(150)
                combat.Init(16, 8, 10, 20, 12)
                combat.SetLifesteal(0.5)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 800 : dropChance = 1.0

            CASE MON_ANCIENT_LICH
                baseEntity.SetSymbol("L") : baseEntity.SetFgColor(CLR_BRIGHT_MAGENTA)
                health.SetMax(120) : health.SetCurrent(120)
                combat.Init(20, 6, 15, 30, 12)
                ai.Init(AI_RANGED)
                xpValue = 1000 : dropChance = 1.0

            CASE MON_DEMON_PRINCE
                baseEntity.SetSymbol("&") : baseEntity.SetFgColor(CLR_BRIGHT_YELLOW)
                health.SetMax(200) : health.SetCurrent(200)
                combat.Init(22, 10, 15, 35, 10)
                ai.Init(AI_AGGRESSIVE)
                xpValue = 1500 : dropChance = 1.0

            CASE ELSE
                baseEntity.SetSymbol("?") : baseEntity.SetFgColor(CLR_WHITE)
                health.SetMax(10) : health.SetCurrent(10)
                xpValue = 10 : dropChance = 0.1
        END SELECT

        baseEntity.SetHasHealth(1)
        baseEntity.SetHasCombat(1)
        baseEntity.SetHasAI(1)
    END SUB

    FUNCTION GetX() AS INTEGER
        GetX = baseEntity.GetX()
    END FUNCTION

    FUNCTION GetY() AS INTEGER
        GetY = baseEntity.GetY()
    END FUNCTION

    SUB SetPosition(px AS INTEGER, py AS INTEGER)
        baseEntity.SetPosition(px, py)
    END SUB

    FUNCTION GetSymbol() AS STRING
        GetSymbol = baseEntity.GetSymbol()
    END FUNCTION

    FUNCTION GetFgColor() AS INTEGER
        GetFgColor = baseEntity.GetFgColor()
    END FUNCTION

    FUNCTION GetType() AS INTEGER
        GetType = monsterType
    END FUNCTION

    FUNCTION IsAlive() AS INTEGER
        IsAlive = isAlive
    END FUNCTION

    SUB Kill()
        isAlive = 0
    END SUB

    FUNCTION GetHealth() AS HealthComponent
        GetHealth = health
    END FUNCTION

    FUNCTION GetCombat() AS CombatComponent
        GetCombat = combat
    END FUNCTION

    FUNCTION GetAI() AS AIComponent
        GetAI = ai
    END FUNCTION

    FUNCTION GetStats() AS StatsComponent
        GetStats = stats
    END FUNCTION

    FUNCTION GetStatus() AS StatusComponent
        GetStatus = status
    END FUNCTION

    FUNCTION GetXPValue() AS INTEGER
        GetXPValue = xpValue
    END FUNCTION

    FUNCTION GetDropChance() AS DOUBLE
        GetDropChance = dropChance
    END FUNCTION

    SUB RememberPlayerPos(px AS INTEGER, py AS INTEGER)
        lastSeenPlayerX = px
        lastSeenPlayerY = py
    END SUB

    FUNCTION GetLastSeenX() AS INTEGER
        GetLastSeenX = lastSeenPlayerX
    END FUNCTION

    FUNCTION GetLastSeenY() AS INTEGER
        GetLastSeenY = lastSeenPlayerY
    END FUNCTION

    ' Get monster name
    FUNCTION GetName() AS STRING
        SELECT CASE monsterType
            CASE MON_RAT : GetName = "Rat"
            CASE MON_BAT : GetName = "Bat"
            CASE MON_GOBLIN : GetName = "Goblin"
            CASE MON_SKELETON : GetName = "Skeleton"
            CASE MON_ZOMBIE : GetName = "Zombie"
            CASE MON_SNAKE : GetName = "Snake"
            CASE MON_KOBOLD : GetName = "Kobold"
            CASE MON_ORC : GetName = "Orc"
            CASE MON_TROLL : GetName = "Troll"
            CASE MON_GHOST : GetName = "Ghost"
            CASE MON_SPIDER : GetName = "Spider"
            CASE MON_MUMMY : GetName = "Mummy"
            CASE MON_OGRE : GetName = "Ogre"
            CASE MON_HARPY : GetName = "Harpy"
            CASE MON_VAMPIRE : GetName = "Vampire"
            CASE MON_WEREWOLF : GetName = "Werewolf"
            CASE MON_DEMON : GetName = "Demon"
            CASE MON_GOLEM : GetName = "Golem"
            CASE MON_LICH : GetName = "Lich"
            CASE MON_WRAITH : GetName = "Wraith"
            CASE MON_MINOTAUR : GetName = "Minotaur"
            CASE MON_DRAGON : GetName = "Dragon"
            CASE MON_ARCHDEMON : GetName = "Archdemon"
            CASE MON_DEATH_KNIGHT : GetName = "Death Knight"
            CASE MON_BEHOLDER : GetName = "Beholder"
            CASE MON_HYDRA : GetName = "Hydra"
            CASE MON_GOBLIN_KING : GetName = "Goblin King"
            CASE MON_VAMPIRE_LORD : GetName = "Vampire Lord"
            CASE MON_ANCIENT_LICH : GetName = "Ancient Lich"
            CASE MON_DEMON_PRINCE : GetName = "Demon Prince"
            CASE ELSE : GetName = "Unknown"
        END SELECT
    END FUNCTION
END CLASS

' ============================================================================
' MONSTER FACTORY - Creates monsters for dungeon floors
' ============================================================================
CLASS MonsterFactory
    SUB Init()
        ' Nothing to initialize
    END SUB

    ' Get a random monster type appropriate for the floor
    FUNCTION GetMonsterTypeForFloor(floorNum AS INTEGER) AS INTEGER
        DIM roll AS DOUBLE
        roll = RND()

        IF floorNum <= 5 THEN
            ' Basic monsters
            IF roll < 0.2 THEN GetMonsterTypeForFloor = MON_RAT
            ELSEIF roll < 0.35 THEN GetMonsterTypeForFloor = MON_BAT
            ELSEIF roll < 0.5 THEN GetMonsterTypeForFloor = MON_GOBLIN
            ELSEIF roll < 0.65 THEN GetMonsterTypeForFloor = MON_SKELETON
            ELSEIF roll < 0.8 THEN GetMonsterTypeForFloor = MON_ZOMBIE
            ELSEIF roll < 0.9 THEN GetMonsterTypeForFloor = MON_SNAKE
            ELSE GetMonsterTypeForFloor = MON_KOBOLD
            END IF

            ' Floor 5 boss chance
            IF floorNum = 5 THEN
                IF roll < 0.1 THEN GetMonsterTypeForFloor = MON_GOBLIN_KING
            END IF

        ELSEIF floorNum <= 10 THEN
            ' Intermediate + some basic
            IF roll < 0.1 THEN GetMonsterTypeForFloor = MON_GOBLIN
            ELSEIF roll < 0.2 THEN GetMonsterTypeForFloor = MON_SKELETON
            ELSEIF roll < 0.35 THEN GetMonsterTypeForFloor = MON_ORC
            ELSEIF roll < 0.5 THEN GetMonsterTypeForFloor = MON_TROLL
            ELSEIF roll < 0.6 THEN GetMonsterTypeForFloor = MON_GHOST
            ELSEIF roll < 0.7 THEN GetMonsterTypeForFloor = MON_SPIDER
            ELSEIF roll < 0.8 THEN GetMonsterTypeForFloor = MON_MUMMY
            ELSEIF roll < 0.9 THEN GetMonsterTypeForFloor = MON_OGRE
            ELSE GetMonsterTypeForFloor = MON_HARPY
            END IF

            ' Floor 10 boss chance
            IF floorNum = 10 THEN
                IF roll < 0.1 THEN GetMonsterTypeForFloor = MON_VAMPIRE_LORD
            END IF

        ELSEIF floorNum <= 15 THEN
            ' Advanced + some intermediate
            IF roll < 0.1 THEN GetMonsterTypeForFloor = MON_ORC
            ELSEIF roll < 0.2 THEN GetMonsterTypeForFloor = MON_TROLL
            ELSEIF roll < 0.35 THEN GetMonsterTypeForFloor = MON_VAMPIRE
            ELSEIF roll < 0.45 THEN GetMonsterTypeForFloor = MON_WEREWOLF
            ELSEIF roll < 0.55 THEN GetMonsterTypeForFloor = MON_DEMON
            ELSEIF roll < 0.65 THEN GetMonsterTypeForFloor = MON_GOLEM
            ELSEIF roll < 0.75 THEN GetMonsterTypeForFloor = MON_LICH
            ELSEIF roll < 0.85 THEN GetMonsterTypeForFloor = MON_WRAITH
            ELSE GetMonsterTypeForFloor = MON_MINOTAUR
            END IF

            ' Floor 15 boss chance
            IF floorNum = 15 THEN
                IF roll < 0.1 THEN GetMonsterTypeForFloor = MON_ANCIENT_LICH
            END IF

        ELSE
            ' Elite + some advanced
            IF roll < 0.15 THEN GetMonsterTypeForFloor = MON_VAMPIRE
            ELSEIF roll < 0.25 THEN GetMonsterTypeForFloor = MON_DEMON
            ELSEIF roll < 0.35 THEN GetMonsterTypeForFloor = MON_LICH
            ELSEIF roll < 0.5 THEN GetMonsterTypeForFloor = MON_DRAGON
            ELSEIF roll < 0.6 THEN GetMonsterTypeForFloor = MON_ARCHDEMON
            ELSEIF roll < 0.7 THEN GetMonsterTypeForFloor = MON_DEATH_KNIGHT
            ELSEIF roll < 0.85 THEN GetMonsterTypeForFloor = MON_BEHOLDER
            ELSE GetMonsterTypeForFloor = MON_HYDRA
            END IF

            ' Floor 20 final boss
            IF floorNum = 20 THEN
                IF roll < 0.2 THEN GetMonsterTypeForFloor = MON_DEMON_PRINCE
            END IF
        END IF
    END FUNCTION

    ' Create a monster of given type at position
    FUNCTION CreateMonster(mType AS INTEGER, px AS INTEGER, py AS INTEGER) AS Monster
        DIM m AS Monster
        m = NEW Monster()
        m.Init(mType, px, py)
        CreateMonster = m
    END FUNCTION

    ' Get number of monsters to spawn for floor
    FUNCTION GetMonsterCountForFloor(floorNum AS INTEGER) AS INTEGER
        DIM baseCount AS INTEGER
        baseCount = 5 + floorNum / 2
        IF baseCount > 20 THEN baseCount = 20
        GetMonsterCountForFloor = baseCount + INT(RND() * 5)
    END FUNCTION
END CLASS
