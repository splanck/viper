' entity.bas - Entity Component System for Roguelike RPG
' Core entity class and all component types

' ============================================================================
' BASE ENTITY CLASS
' ============================================================================
CLASS Entity
    DIM id AS INTEGER
    DIM x AS INTEGER
    DIM y AS INTEGER
    DIM symbol AS STRING
    DIM foreColor AS INTEGER
    DIM backColor AS INTEGER
    DIM entName AS STRING
    DIM entType AS INTEGER
    DIM isAlive AS INTEGER
    DIM blocksMove AS INTEGER
    DIM blocksSight AS INTEGER

    ' Component references (stored as integers for simplicity)
    DIM hasHealth AS INTEGER
    DIM hasCombat AS INTEGER
    DIM hasAI AS INTEGER
    DIM hasInventory AS INTEGER
    DIM hasStats AS INTEGER
    DIM hasStatus AS INTEGER

    SUB Init(newId AS INTEGER, px AS INTEGER, py AS INTEGER, sym AS STRING, fg AS INTEGER)
        id = newId
        x = px
        y = py
        symbol = sym
        foreColor = fg
        backColor = CLR_BLACK
        entName = "Entity"
        entType = ENT_NONE
        isAlive = 1
        blocksMove = 1
        blocksSight = 0
        hasHealth = 0
        hasCombat = 0
        hasAI = 0
        hasInventory = 0
        hasStats = 0
        hasStatus = 0
    END SUB

    FUNCTION GetX() AS INTEGER
        GetX = x
    END FUNCTION

    FUNCTION GetY() AS INTEGER
        GetY = y
    END FUNCTION

    SUB SetPos(px AS INTEGER, py AS INTEGER)
        x = px
        y = py
    END SUB

    FUNCTION GetSymbol() AS STRING
        GetSymbol = symbol
    END FUNCTION

    FUNCTION GetForeColor() AS INTEGER
        GetForeColor = foreColor
    END FUNCTION

    FUNCTION GetName() AS STRING
        GetName = entName
    END FUNCTION

    SUB SetName(nm AS STRING)
        entName = nm
    END SUB

    FUNCTION IsBlocking() AS INTEGER
        IsBlocking = blocksMove
    END FUNCTION

    FUNCTION GetId() AS INTEGER
        GetId = id
    END FUNCTION

    FUNCTION GetType() AS INTEGER
        GetType = entType
    END FUNCTION

    SUB SetType(t AS INTEGER)
        entType = t
    END SUB
END CLASS

' ============================================================================
' HEALTH COMPONENT
' ============================================================================
CLASS HealthComponent
    DIM current AS INTEGER
    DIM maximum AS INTEGER
    DIM regenRate AS INTEGER      ' HP regen per 10 turns
    DIM regenCounter AS INTEGER

    SUB Init(maxHp AS INTEGER)
        maximum = maxHp
        current = maxHp
        regenRate = 0
        regenCounter = 0
    END SUB

    FUNCTION GetCurrent() AS INTEGER
        GetCurrent = current
    END FUNCTION

    FUNCTION GetMax() AS INTEGER
        GetMax = maximum
    END FUNCTION

    SUB TakeDamage(amount AS INTEGER)
        current = current - amount
        IF current < 0 THEN current = 0
    END SUB

    SUB Heal(amount AS INTEGER)
        current = current + amount
        IF current > maximum THEN current = maximum
    END SUB

    SUB SetMax(newMax AS INTEGER)
        maximum = newMax
        IF current > maximum THEN current = maximum
    END SUB

    FUNCTION IsDead() AS INTEGER
        IsDead = 0
        IF current <= 0 THEN IsDead = 1
    END FUNCTION

    FUNCTION GetPercent() AS INTEGER
        IF maximum > 0 THEN
            GetPercent = (current * 100) / maximum
        ELSE
            GetPercent = 0
        END IF
    END FUNCTION

    SUB Tick()
        IF regenRate > 0 THEN
            regenCounter = regenCounter + regenRate
            IF regenCounter >= 10 THEN
                regenCounter = regenCounter - 10
                Me.Heal(1)
            END IF
        END IF
    END SUB

    SUB SetRegen(rate AS INTEGER)
        regenRate = rate
    END SUB
END CLASS

' ============================================================================
' COMBAT COMPONENT
' ============================================================================
CLASS CombatComponent
    DIM attack AS INTEGER
    DIM defense AS INTEGER
    DIM damageMin AS INTEGER
    DIM damageMax AS INTEGER
    DIM critChance AS INTEGER     ' percent
    DIM critMultiplier AS INTEGER ' x100 (200 = 2.0x)
    DIM accuracy AS INTEGER       ' bonus to hit
    DIM evasion AS INTEGER        ' bonus to dodge

    SUB Init(atk AS INTEGER, def AS INTEGER, dmin AS INTEGER, dmax AS INTEGER)
        attack = atk
        defense = def
        damageMin = dmin
        damageMax = dmax
        critChance = 5
        critMultiplier = 200
        accuracy = 0
        evasion = 0
    END SUB

    FUNCTION GetAttack() AS INTEGER
        GetAttack = attack
    END FUNCTION

    FUNCTION GetDefense() AS INTEGER
        GetDefense = defense
    END FUNCTION

    FUNCTION GetDamageMin() AS INTEGER
        GetDamageMin = damageMin
    END FUNCTION

    FUNCTION GetDamageMax() AS INTEGER
        GetDamageMax = damageMax
    END FUNCTION

    FUNCTION RollDamage() AS INTEGER
        DIM range AS INTEGER
        DIM roll AS INTEGER
        range = damageMax - damageMin + 1
        IF range < 1 THEN range = 1
        roll = INT(RND() * range) + damageMin
        RollDamage = roll
    END FUNCTION

    FUNCTION RollCrit() AS INTEGER
        DIM roll AS INTEGER
        roll = INT(RND() * 100)
        RollCrit = 0
        IF roll < critChance THEN RollCrit = 1
    END FUNCTION

    FUNCTION GetCritMultiplier() AS INTEGER
        GetCritMultiplier = critMultiplier
    END FUNCTION

    SUB SetAttack(atk AS INTEGER)
        attack = atk
    END SUB

    SUB SetDefense(def AS INTEGER)
        defense = def
    END SUB

    SUB SetDamage(dmin AS INTEGER, dmax AS INTEGER)
        damageMin = dmin
        damageMax = dmax
    END SUB

    SUB SetCrit(chance AS INTEGER, mult AS INTEGER)
        critChance = chance
        critMultiplier = mult
    END SUB
END CLASS

' ============================================================================
' AI COMPONENT
' ============================================================================
CLASS AIComponent
    DIM behavior AS INTEGER
    DIM aiState AS INTEGER
    DIM targetId AS INTEGER
    DIM targetX AS INTEGER
    DIM targetY AS INTEGER
    DIM alertness AS INTEGER
    DIM patrolX AS INTEGER
    DIM patrolY AS INTEGER
    DIM fleeThreshold AS INTEGER  ' HP percent to flee

    SUB Init(behav AS INTEGER)
        behavior = behav
        aiState = STATE_IDLE
        targetId = -1
        targetX = -1
        targetY = -1
        alertness = 0
        patrolX = -1
        patrolY = -1
        fleeThreshold = 20
    END SUB

    FUNCTION GetBehavior() AS INTEGER
        GetBehavior = behavior
    END FUNCTION

    FUNCTION GetState() AS INTEGER
        GetState = aiState
    END FUNCTION

    SUB SetState(st AS INTEGER)
        aiState = st
    END SUB

    SUB SetTarget(tid AS INTEGER, tx AS INTEGER, ty AS INTEGER)
        targetId = tid
        targetX = tx
        targetY = ty
    END SUB

    FUNCTION GetTargetId() AS INTEGER
        GetTargetId = targetId
    END FUNCTION

    FUNCTION GetTargetX() AS INTEGER
        GetTargetX = targetX
    END FUNCTION

    FUNCTION GetTargetY() AS INTEGER
        GetTargetY = targetY
    END FUNCTION

    SUB ClearTarget()
        targetId = -1
        targetX = -1
        targetY = -1
    END SUB

    SUB Alert()
        alertness = 10
        aiState = STATE_HUNTING
    END SUB

    SUB Tick()
        IF alertness > 0 THEN alertness = alertness - 1
        IF alertness = 0 THEN
            IF aiState = STATE_HUNTING THEN aiState = STATE_IDLE
        END IF
    END SUB

    FUNCTION ShouldFlee(hpPercent AS INTEGER) AS INTEGER
        ShouldFlee = 0
        IF hpPercent <= fleeThreshold THEN ShouldFlee = 1
    END FUNCTION
END CLASS

' ============================================================================
' STATS COMPONENT (for player and special monsters)
' ============================================================================
CLASS StatsComponent
    DIM strength AS INTEGER
    DIM dexterity AS INTEGER
    DIM constitution AS INTEGER
    DIM intelligence AS INTEGER
    DIM wisdom AS INTEGER
    DIM charisma AS INTEGER

    DIM level AS INTEGER
    DIM experience AS INTEGER
    DIM expToNext AS INTEGER
    DIM statPoints AS INTEGER

    DIM mana AS INTEGER
    DIM manaMax AS INTEGER
    DIM manaRegen AS INTEGER

    DIM hunger AS INTEGER
    DIM gold AS INTEGER

    DIM playerClass AS INTEGER

    SUB Init(pclass AS INTEGER)
        playerClass = pclass
        level = 1
        experience = 0
        expToNext = XP_BASE
        statPoints = 0
        hunger = HUNGER_MAX
        gold = 0

        ' Set base stats by class
        IF pclass = CLASS_WARRIOR THEN
            strength = 16
            dexterity = 12
            constitution = 14
            intelligence = 8
            wisdom = 10
            charisma = 10
            manaMax = 20
        END IF
        IF pclass = CLASS_MAGE THEN
            strength = 8
            dexterity = 12
            constitution = 10
            intelligence = 16
            wisdom = 14
            charisma = 10
            manaMax = 100
        END IF
        IF pclass = CLASS_ROGUE THEN
            strength = 10
            dexterity = 16
            constitution = 12
            intelligence = 12
            wisdom = 10
            charisma = 10
            manaMax = 40
        END IF
        IF pclass = CLASS_RANGER THEN
            strength = 12
            dexterity = 14
            constitution = 12
            intelligence = 10
            wisdom = 14
            charisma = 8
            manaMax = 50
        END IF

        mana = manaMax
        manaRegen = 1
    END SUB

    FUNCTION GetStr() AS INTEGER
        GetStr = strength
    END FUNCTION

    FUNCTION GetDex() AS INTEGER
        GetDex = dexterity
    END FUNCTION

    FUNCTION GetCon() AS INTEGER
        GetCon = constitution
    END FUNCTION

    FUNCTION GetInt() AS INTEGER
        GetInt = intelligence
    END FUNCTION

    FUNCTION GetWis() AS INTEGER
        GetWis = wisdom
    END FUNCTION

    FUNCTION GetCha() AS INTEGER
        GetCha = charisma
    END FUNCTION

    FUNCTION GetLevel() AS INTEGER
        GetLevel = level
    END FUNCTION

    FUNCTION GetExp() AS INTEGER
        GetExp = experience
    END FUNCTION

    FUNCTION GetExpToNext() AS INTEGER
        GetExpToNext = expToNext
    END FUNCTION

    SUB AddExp(amount AS INTEGER)
        experience = experience + amount
        DO WHILE experience >= expToNext
            experience = experience - expToNext
            Me.LevelUp()
        LOOP
    END SUB

    SUB LevelUp()
        IF level >= MAX_LEVEL THEN EXIT SUB
        level = level + 1
        expToNext = XP_BASE + (level * XP_FACTOR)
        statPoints = statPoints + 2

        ' Class-based bonuses
        IF playerClass = CLASS_WARRIOR THEN
            strength = strength + 1
            constitution = constitution + 1
        END IF
        IF playerClass = CLASS_MAGE THEN
            intelligence = intelligence + 1
            wisdom = wisdom + 1
            manaMax = manaMax + 10
        END IF
        IF playerClass = CLASS_ROGUE THEN
            dexterity = dexterity + 1
            IF level MOD 2 = 0 THEN strength = strength + 1
        END IF
        IF playerClass = CLASS_RANGER THEN
            dexterity = dexterity + 1
            wisdom = wisdom + 1
        END IF
    END SUB

    FUNCTION GetMana() AS INTEGER
        GetMana = mana
    END FUNCTION

    FUNCTION GetManaMax() AS INTEGER
        GetManaMax = manaMax
    END FUNCTION

    SUB UseMana(amount AS INTEGER)
        mana = mana - amount
        IF mana < 0 THEN mana = 0
    END SUB

    SUB RestoreMana(amount AS INTEGER)
        mana = mana + amount
        IF mana > manaMax THEN mana = manaMax
    END SUB

    FUNCTION GetHunger() AS INTEGER
        GetHunger = hunger
    END FUNCTION

    SUB TickHunger()
        hunger = hunger - 1
        IF hunger < 0 THEN hunger = 0
    END SUB

    SUB Feed(amount AS INTEGER)
        hunger = hunger + amount
        IF hunger > HUNGER_MAX THEN hunger = HUNGER_MAX
    END SUB

    FUNCTION IsStarving() AS INTEGER
        IsStarving = 0
        IF hunger <= HUNGER_STARVING THEN IsStarving = 1
    END FUNCTION

    FUNCTION GetGold() AS INTEGER
        GetGold = gold
    END FUNCTION

    SUB AddGold(amount AS INTEGER)
        gold = gold + amount
    END SUB

    FUNCTION SpendGold(amount AS INTEGER) AS INTEGER
        SpendGold = 0
        IF gold >= amount THEN
            gold = gold - amount
            SpendGold = 1
        END IF
    END FUNCTION

    FUNCTION GetClass() AS INTEGER
        GetClass = playerClass
    END FUNCTION

    FUNCTION GetStatPoints() AS INTEGER
        GetStatPoints = statPoints
    END FUNCTION

    SUB SpendStatPoint(stat AS INTEGER)
        IF statPoints <= 0 THEN EXIT SUB
        statPoints = statPoints - 1
        IF stat = STAT_STR THEN strength = strength + 1
        IF stat = STAT_DEX THEN dexterity = dexterity + 1
        IF stat = STAT_CON THEN constitution = constitution + 1
        IF stat = STAT_INT THEN intelligence = intelligence + 1
        IF stat = STAT_WIS THEN wisdom = wisdom + 1
        IF stat = STAT_CHA THEN charisma = charisma + 1
    END SUB

    ' Stat modifiers (D&D style: (stat-10)/2)
    FUNCTION GetStrMod() AS INTEGER
        GetStrMod = (strength - 10) / 2
    END FUNCTION

    FUNCTION GetDexMod() AS INTEGER
        GetDexMod = (dexterity - 10) / 2
    END FUNCTION

    FUNCTION GetConMod() AS INTEGER
        GetConMod = (constitution - 10) / 2
    END FUNCTION

    FUNCTION GetIntMod() AS INTEGER
        GetIntMod = (intelligence - 10) / 2
    END FUNCTION

    FUNCTION GetWisMod() AS INTEGER
        GetWisMod = (wisdom - 10) / 2
    END FUNCTION

    FUNCTION GetChaMod() AS INTEGER
        GetChaMod = (charisma - 10) / 2
    END FUNCTION
END CLASS

' ============================================================================
' STATUS EFFECTS COMPONENT
' ============================================================================
CLASS StatusComponent
    ' Each status has a duration (0 = not active)
    DIM poisonDur AS INTEGER
    DIM burningDur AS INTEGER
    DIM frozenDur AS INTEGER
    DIM stunnedDur AS INTEGER
    DIM blindDur AS INTEGER
    DIM invisibleDur AS INTEGER
    DIM hasteDur AS INTEGER
    DIM slowDur AS INTEGER
    DIM regenDur AS INTEGER
    DIM berserkDur AS INTEGER

    SUB Init()
        poisonDur = 0
        burningDur = 0
        frozenDur = 0
        stunnedDur = 0
        blindDur = 0
        invisibleDur = 0
        hasteDur = 0
        slowDur = 0
        regenDur = 0
        berserkDur = 0
    END SUB

    SUB Apply(status AS INTEGER, duration AS INTEGER)
        IF status = STATUS_POISONED THEN poisonDur = duration
        IF status = STATUS_BURNING THEN burningDur = duration
        IF status = STATUS_FROZEN THEN frozenDur = duration
        IF status = STATUS_STUNNED THEN stunnedDur = duration
        IF status = STATUS_BLIND THEN blindDur = duration
        IF status = STATUS_INVISIBLE THEN invisibleDur = duration
        IF status = STATUS_HASTE THEN hasteDur = duration
        IF status = STATUS_SLOW THEN slowDur = duration
        IF status = STATUS_REGEN THEN regenDur = duration
        IF status = STATUS_BERSERK THEN berserkDur = duration
    END SUB

    FUNCTION Has(status AS INTEGER) AS INTEGER
        Has = 0
        IF status = STATUS_POISONED THEN IF poisonDur > 0 THEN Has = 1
        IF status = STATUS_BURNING THEN IF burningDur > 0 THEN Has = 1
        IF status = STATUS_FROZEN THEN IF frozenDur > 0 THEN Has = 1
        IF status = STATUS_STUNNED THEN IF stunnedDur > 0 THEN Has = 1
        IF status = STATUS_BLIND THEN IF blindDur > 0 THEN Has = 1
        IF status = STATUS_INVISIBLE THEN IF invisibleDur > 0 THEN Has = 1
        IF status = STATUS_HASTE THEN IF hasteDur > 0 THEN Has = 1
        IF status = STATUS_SLOW THEN IF slowDur > 0 THEN Has = 1
        IF status = STATUS_REGEN THEN IF regenDur > 0 THEN Has = 1
        IF status = STATUS_BERSERK THEN IF berserkDur > 0 THEN Has = 1
    END FUNCTION

    SUB Tick()
        IF poisonDur > 0 THEN poisonDur = poisonDur - 1
        IF burningDur > 0 THEN burningDur = burningDur - 1
        IF frozenDur > 0 THEN frozenDur = frozenDur - 1
        IF stunnedDur > 0 THEN stunnedDur = stunnedDur - 1
        IF blindDur > 0 THEN blindDur = blindDur - 1
        IF invisibleDur > 0 THEN invisibleDur = invisibleDur - 1
        IF hasteDur > 0 THEN hasteDur = hasteDur - 1
        IF slowDur > 0 THEN slowDur = slowDur - 1
        IF regenDur > 0 THEN regenDur = regenDur - 1
        IF berserkDur > 0 THEN berserkDur = berserkDur - 1
    END SUB

    SUB Clear(status AS INTEGER)
        IF status = STATUS_POISONED THEN poisonDur = 0
        IF status = STATUS_BURNING THEN burningDur = 0
        IF status = STATUS_FROZEN THEN frozenDur = 0
        IF status = STATUS_STUNNED THEN stunnedDur = 0
        IF status = STATUS_BLIND THEN blindDur = 0
        IF status = STATUS_INVISIBLE THEN invisibleDur = 0
        IF status = STATUS_HASTE THEN hasteDur = 0
        IF status = STATUS_SLOW THEN slowDur = 0
        IF status = STATUS_REGEN THEN regenDur = 0
        IF status = STATUS_BERSERK THEN berserkDur = 0
    END SUB

    SUB ClearAll()
        poisonDur = 0
        burningDur = 0
        frozenDur = 0
        stunnedDur = 0
        blindDur = 0
        invisibleDur = 0
        hasteDur = 0
        slowDur = 0
        regenDur = 0
        berserkDur = 0
    END SUB

    FUNCTION IsStunned() AS INTEGER
        IsStunned = 0
        IF stunnedDur > 0 THEN IsStunned = 1
        IF frozenDur > 0 THEN IsStunned = 1
    END FUNCTION

    FUNCTION IsInvisible() AS INTEGER
        IsInvisible = 0
        IF invisibleDur > 0 THEN IsInvisible = 1
    END FUNCTION

    FUNCTION GetPoisonDamage() AS INTEGER
        GetPoisonDamage = 0
        IF poisonDur > 0 THEN GetPoisonDamage = 2
    END FUNCTION

    FUNCTION GetBurnDamage() AS INTEGER
        GetBurnDamage = 0
        IF burningDur > 0 THEN GetBurnDamage = 3
    END FUNCTION
END CLASS
