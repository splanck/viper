' magic.bas - Magic and spell system
' 8 schools of magic with multiple spells each

' ============================================================================
' SPELL IDS
' ============================================================================
' Fire spells (10-19)
CONST SPELL_FIREBALL AS INTEGER = 10
CONST SPELL_FLAME_BURST AS INTEGER = 11
CONST SPELL_INFERNO AS INTEGER = 12
CONST SPELL_FIRE_SHIELD AS INTEGER = 13

' Ice spells (20-29)
CONST SPELL_ICE_BOLT AS INTEGER = 20
CONST SPELL_FREEZE AS INTEGER = 21
CONST SPELL_BLIZZARD AS INTEGER = 22
CONST SPELL_ICE_ARMOR AS INTEGER = 23

' Lightning spells (30-39)
CONST SPELL_LIGHTNING AS INTEGER = 30
CONST SPELL_CHAIN_LIGHTNING AS INTEGER = 31
CONST SPELL_THUNDERSTORM AS INTEGER = 32
CONST SPELL_STATIC_SHIELD AS INTEGER = 33

' Death spells (40-49)
CONST SPELL_DRAIN_LIFE AS INTEGER = 40
CONST SPELL_POISON_CLOUD AS INTEGER = 41
CONST SPELL_CURSE AS INTEGER = 42
CONST SPELL_DEATH_BOLT AS INTEGER = 43

' Life spells (50-59)
CONST SPELL_HEAL AS INTEGER = 50
CONST SPELL_CURE_POISON AS INTEGER = 51
CONST SPELL_REGENERATE AS INTEGER = 52
CONST SPELL_HOLY_LIGHT AS INTEGER = 53

' Force spells (60-69)
CONST SPELL_MAGIC_MISSILE AS INTEGER = 60
CONST SPELL_FORCE_PUSH AS INTEGER = 61
CONST SPELL_TELEPORT AS INTEGER = 62
CONST SPELL_BLINK AS INTEGER = 63

' Illusion spells (70-79)
CONST SPELL_INVISIBILITY AS INTEGER = 70
CONST SPELL_BLIND AS INTEGER = 71
CONST SPELL_CONFUSE AS INTEGER = 72
CONST SPELL_PHANTOM AS INTEGER = 73

' Summoning spells (80-89)
CONST SPELL_SUMMON_IMP AS INTEGER = 80
CONST SPELL_SUMMON_SKELETON AS INTEGER = 81
CONST SPELL_SUMMON_GOLEM AS INTEGER = 82
CONST SPELL_BANISH AS INTEGER = 83

' ============================================================================
' SPELL CLASS
' ============================================================================
CLASS Spell
    DIM spellID AS INTEGER
    DIM spellName AS STRING
    DIM school AS INTEGER
    DIM manaCost AS INTEGER
    DIM damage AS INTEGER
    DIM duration AS INTEGER
    DIM range AS INTEGER
    DIM areaRadius AS INTEGER
    DIM levelRequired AS INTEGER

    SUB Init(id AS INTEGER)
        spellID = id
        areaRadius = 0
        duration = 0

        SELECT CASE id
            ' ========== FIRE ==========
            CASE SPELL_FIREBALL
                spellName = "Fireball"
                school = SCHOOL_FIRE
                manaCost = 15
                damage = 20
                range = 6
                areaRadius = 2
                levelRequired = 3

            CASE SPELL_FLAME_BURST
                spellName = "Flame Burst"
                school = SCHOOL_FIRE
                manaCost = 8
                damage = 12
                range = 4
                levelRequired = 1

            CASE SPELL_INFERNO
                spellName = "Inferno"
                school = SCHOOL_FIRE
                manaCost = 35
                damage = 40
                range = 5
                areaRadius = 3
                levelRequired = 10

            CASE SPELL_FIRE_SHIELD
                spellName = "Fire Shield"
                school = SCHOOL_FIRE
                manaCost = 20
                damage = 5
                duration = 20
                range = 0
                levelRequired = 5

            ' ========== ICE ==========
            CASE SPELL_ICE_BOLT
                spellName = "Ice Bolt"
                school = SCHOOL_ICE
                manaCost = 10
                damage = 15
                range = 5
                levelRequired = 2

            CASE SPELL_FREEZE
                spellName = "Freeze"
                school = SCHOOL_ICE
                manaCost = 18
                damage = 8
                duration = 5
                range = 4
                levelRequired = 4

            CASE SPELL_BLIZZARD
                spellName = "Blizzard"
                school = SCHOOL_ICE
                manaCost = 40
                damage = 25
                areaRadius = 4
                range = 6
                levelRequired = 12

            CASE SPELL_ICE_ARMOR
                spellName = "Ice Armor"
                school = SCHOOL_ICE
                manaCost = 25
                damage = 0
                duration = 30
                range = 0
                levelRequired = 6

            ' ========== LIGHTNING ==========
            CASE SPELL_LIGHTNING
                spellName = "Lightning Bolt"
                school = SCHOOL_LIGHTNING
                manaCost = 12
                damage = 18
                range = 8
                levelRequired = 2

            CASE SPELL_CHAIN_LIGHTNING
                spellName = "Chain Lightning"
                school = SCHOOL_LIGHTNING
                manaCost = 25
                damage = 15
                range = 6
                areaRadius = 3
                levelRequired = 7

            CASE SPELL_THUNDERSTORM
                spellName = "Thunderstorm"
                school = SCHOOL_LIGHTNING
                manaCost = 50
                damage = 35
                areaRadius = 5
                range = 0
                levelRequired = 15

            CASE SPELL_STATIC_SHIELD
                spellName = "Static Shield"
                school = SCHOOL_LIGHTNING
                manaCost = 22
                damage = 8
                duration = 25
                range = 0
                levelRequired = 5

            ' ========== DEATH ==========
            CASE SPELL_DRAIN_LIFE
                spellName = "Drain Life"
                school = SCHOOL_DEATH
                manaCost = 15
                damage = 12
                range = 4
                levelRequired = 3

            CASE SPELL_POISON_CLOUD
                spellName = "Poison Cloud"
                school = SCHOOL_DEATH
                manaCost = 20
                damage = 5
                duration = 10
                areaRadius = 2
                range = 5
                levelRequired = 5

            CASE SPELL_CURSE
                spellName = "Curse"
                school = SCHOOL_DEATH
                manaCost = 18
                damage = 0
                duration = 20
                range = 5
                levelRequired = 4

            CASE SPELL_DEATH_BOLT
                spellName = "Death Bolt"
                school = SCHOOL_DEATH
                manaCost = 35
                damage = 50
                range = 6
                levelRequired = 12

            ' ========== LIFE ==========
            CASE SPELL_HEAL
                spellName = "Heal"
                school = SCHOOL_LIFE
                manaCost = 10
                damage = -20      ' Negative = healing
                range = 0
                levelRequired = 1

            CASE SPELL_CURE_POISON
                spellName = "Cure Poison"
                school = SCHOOL_LIFE
                manaCost = 8
                damage = 0
                range = 0
                levelRequired = 2

            CASE SPELL_REGENERATE
                spellName = "Regenerate"
                school = SCHOOL_LIFE
                manaCost = 25
                damage = -5       ' Per turn
                duration = 15
                range = 0
                levelRequired = 6

            CASE SPELL_HOLY_LIGHT
                spellName = "Holy Light"
                school = SCHOOL_LIFE
                manaCost = 30
                damage = 35       ' Extra vs undead
                range = 5
                levelRequired = 8

            ' ========== FORCE ==========
            CASE SPELL_MAGIC_MISSILE
                spellName = "Magic Missile"
                school = SCHOOL_FORCE
                manaCost = 5
                damage = 10
                range = 8
                levelRequired = 1

            CASE SPELL_FORCE_PUSH
                spellName = "Force Push"
                school = SCHOOL_FORCE
                manaCost = 12
                damage = 5
                range = 4
                levelRequired = 3

            CASE SPELL_TELEPORT
                spellName = "Teleport"
                school = SCHOOL_FORCE
                manaCost = 40
                damage = 0
                range = 20
                levelRequired = 10

            CASE SPELL_BLINK
                spellName = "Blink"
                school = SCHOOL_FORCE
                manaCost = 15
                damage = 0
                range = 5
                levelRequired = 4

            ' ========== ILLUSION ==========
            CASE SPELL_INVISIBILITY
                spellName = "Invisibility"
                school = SCHOOL_ILLUSION
                manaCost = 30
                damage = 0
                duration = 15
                range = 0
                levelRequired = 6

            CASE SPELL_BLIND
                spellName = "Blind"
                school = SCHOOL_ILLUSION
                manaCost = 15
                damage = 0
                duration = 8
                range = 5
                levelRequired = 3

            CASE SPELL_CONFUSE
                spellName = "Confuse"
                school = SCHOOL_ILLUSION
                manaCost = 18
                damage = 0
                duration = 10
                range = 5
                levelRequired = 4

            CASE SPELL_PHANTOM
                spellName = "Phantom"
                school = SCHOOL_ILLUSION
                manaCost = 25
                damage = 0
                duration = 20
                range = 3
                levelRequired = 7

            ' ========== SUMMONING ==========
            CASE SPELL_SUMMON_IMP
                spellName = "Summon Imp"
                school = SCHOOL_SUMMONING
                manaCost = 20
                damage = 0
                duration = 30
                range = 2
                levelRequired = 4

            CASE SPELL_SUMMON_SKELETON
                spellName = "Summon Skeleton"
                school = SCHOOL_SUMMONING
                manaCost = 25
                damage = 0
                duration = 40
                range = 2
                levelRequired = 6

            CASE SPELL_SUMMON_GOLEM
                spellName = "Summon Golem"
                school = SCHOOL_SUMMONING
                manaCost = 50
                damage = 0
                duration = 50
                range = 2
                levelRequired = 12

            CASE SPELL_BANISH
                spellName = "Banish"
                school = SCHOOL_SUMMONING
                manaCost = 30
                damage = 100     ' Instant kill vs summoned
                range = 6
                levelRequired = 8

            CASE ELSE
                spellName = "Unknown"
                school = SCHOOL_FORCE
                manaCost = 10
                damage = 5
                range = 4
                levelRequired = 1
        END SELECT
    END SUB

    FUNCTION GetID() AS INTEGER
        GetID = spellID
    END FUNCTION

    FUNCTION GetName() AS STRING
        GetName = spellName
    END FUNCTION

    FUNCTION GetSchool() AS INTEGER
        GetSchool = school
    END FUNCTION

    FUNCTION GetManaCost() AS INTEGER
        GetManaCost = manaCost
    END FUNCTION

    FUNCTION GetDamage() AS INTEGER
        GetDamage = damage
    END FUNCTION

    FUNCTION GetDuration() AS INTEGER
        GetDuration = duration
    END FUNCTION

    FUNCTION GetRange() AS INTEGER
        GetRange = range
    END FUNCTION

    FUNCTION GetAreaRadius() AS INTEGER
        GetAreaRadius = areaRadius
    END FUNCTION

    FUNCTION GetLevelRequired() AS INTEGER
        GetLevelRequired = levelRequired
    END FUNCTION

    FUNCTION IsAreaSpell() AS INTEGER
        IF areaRadius > 0 THEN
            IsAreaSpell = 1
        ELSE
            IsAreaSpell = 0
        END IF
    END FUNCTION

    FUNCTION IsSelfTarget() AS INTEGER
        IF range = 0 THEN
            IsSelfTarget = 1
        ELSE
            IsSelfTarget = 0
        END IF
    END FUNCTION

    ' Get school name
    FUNCTION GetSchoolName() AS STRING
        SELECT CASE school
            CASE SCHOOL_FIRE : GetSchoolName = "Fire"
            CASE SCHOOL_ICE : GetSchoolName = "Ice"
            CASE SCHOOL_LIGHTNING : GetSchoolName = "Lightning"
            CASE SCHOOL_DEATH : GetSchoolName = "Death"
            CASE SCHOOL_LIFE : GetSchoolName = "Life"
            CASE SCHOOL_FORCE : GetSchoolName = "Force"
            CASE SCHOOL_ILLUSION : GetSchoolName = "Illusion"
            CASE SCHOOL_SUMMONING : GetSchoolName = "Summoning"
            CASE ELSE : GetSchoolName = "Unknown"
        END SELECT
    END FUNCTION
END CLASS

' ============================================================================
' SPELLBOOK CLASS - Player's known spells
' ============================================================================
CLASS Spellbook
    DIM spells(31) AS Spell
    DIM spellCount AS INTEGER
    DIM maxSpells AS INTEGER

    SUB Init()
        spellCount = 0
        maxSpells = 32
    END SUB

    ' Learn a new spell
    FUNCTION LearnSpell(spellID AS INTEGER) AS INTEGER
        ' Check if already known
        DIM i AS INTEGER
        FOR i = 0 TO spellCount - 1
            IF spells(i).GetID() = spellID THEN
                LearnSpell = 0
                EXIT FUNCTION
            END IF
        NEXT i

        IF spellCount >= maxSpells THEN
            LearnSpell = 0
            EXIT FUNCTION
        END IF

        DIM newSpell AS Spell
        newSpell = NEW Spell()
        newSpell.Init(spellID)
        spells(spellCount) = newSpell
        spellCount = spellCount + 1
        LearnSpell = 1
    END FUNCTION

    FUNCTION GetSpellCount() AS INTEGER
        GetSpellCount = spellCount
    END FUNCTION

    FUNCTION GetSpell(idx AS INTEGER) AS Spell
        IF idx >= 0 THEN
            IF idx < spellCount THEN
                GetSpell = spells(idx)
                EXIT FUNCTION
            END IF
        END IF
        ' Return a dummy spell
        DIM dummy AS Spell
        dummy = NEW Spell()
        dummy.Init(0)
        GetSpell = dummy
    END FUNCTION

    ' Check if player knows a spell
    FUNCTION KnowsSpell(spellID AS INTEGER) AS INTEGER
        DIM i AS INTEGER
        KnowsSpell = 0
        FOR i = 0 TO spellCount - 1
            IF spells(i).GetID() = spellID THEN
                KnowsSpell = 1
                EXIT FUNCTION
            END IF
        NEXT i
    END FUNCTION

    ' Get starting spells for a class
    SUB LearnClassSpells(playerClass AS INTEGER)
        SELECT CASE playerClass
            CASE CLASS_WARRIOR
                ' Warriors get minimal magic
                i = Me.LearnSpell(SPELL_HEAL)

            CASE CLASS_MAGE
                ' Mages get arcane and elemental
                i = Me.LearnSpell(SPELL_MAGIC_MISSILE)
                i = Me.LearnSpell(SPELL_FLAME_BURST)
                i = Me.LearnSpell(SPELL_ICE_BOLT)
                i = Me.LearnSpell(SPELL_LIGHTNING)
                i = Me.LearnSpell(SPELL_HEAL)

            CASE CLASS_ROGUE
                ' Rogues get illusion
                i = Me.LearnSpell(SPELL_BLINK)
                i = Me.LearnSpell(SPELL_INVISIBILITY)

            CASE CLASS_RANGER
                ' Rangers get nature and healing
                i = Me.LearnSpell(SPELL_HEAL)
                i = Me.LearnSpell(SPELL_CURE_POISON)
                i = Me.LearnSpell(SPELL_MAGIC_MISSILE)
        END SELECT
    END SUB
END CLASS
