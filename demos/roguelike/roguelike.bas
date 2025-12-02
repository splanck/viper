' roguelike.bas - Main game file for Viper Roguelike Dungeon
' A full roguelike RPG demonstrating all Viper.* runtime features
'
' Features:
' - 20 dungeon floors with BSP room generation
' - 30+ monster types with AI behaviors
' - 100+ items (weapons, armor, potions, scrolls, food)
' - 4 character classes (Warrior, Mage, Rogue, Ranger)
' - FOV shadowcasting and A* pathfinding
' - Entity Component System architecture
' - Magic system with 8 schools
' - Hunger and status effects
' - Save/Load with permadeath
' - High score table
'
' Uses: Viper.Collections.List, Viper.Text.StringBuilder, Viper.Terminal,
'       Viper.Random, Viper.Time, Viper.Math, Viper.String, Viper.IO.File,
'       Viper.Convert, Viper.Console

' Include all game modules
AddFile "constants.bas"
AddFile "entity.bas"
AddFile "items.bas"
AddFile "dungeon.bas"
AddFile "fov.bas"
AddFile "monsters.bas"
AddFile "magic.bas"
AddFile "combat.bas"
AddFile "display.bas"
AddFile "saveload.bas"

' ============================================================================
' GAME CLASS - Main game controller
' ============================================================================
CLASS Game
    ' Core systems
    DIM display AS DisplaySystem
    DIM fov AS FOVCalculator
    DIM combatSys AS CombatSystem
    DIM aiSys AS AISystem
    DIM messages AS MessageLog
    DIM saveManager AS SaveLoadManager
    DIM highScores AS HighScoreTable

    ' Player
    DIM playerEntity AS Entity
    DIM playerHealth AS HealthComponent
    DIM playerCombat AS CombatComponent
    DIM playerStats AS StatsComponent
    DIM playerStatus AS StatusComponent
    DIM playerInventory AS InventoryComponent
    DIM playerSpellbook AS Spellbook
    DIM playerClass AS INTEGER

    ' Dungeon
    DIM dungeon AS DungeonMap

    ' Monsters
    DIM monsters(49) AS Monster
    DIM monsterCount AS INTEGER

    ' Items on ground
    DIM groundItems(99) AS Item
    DIM groundItemCount AS INTEGER

    ' Factories
    DIM itemFactory AS ItemFactory
    DIM monsterFactory AS MonsterFactory

    ' Game state
    DIM gameState AS INTEGER
    DIM floorLevel AS INTEGER
    DIM turnCount AS INTEGER
    DIM isRunning AS INTEGER
    DIM playerWon AS INTEGER

    SUB Init()
        ' Initialize systems
        display = NEW DisplaySystem()
        display.Init()

        fov = NEW FOVCalculator()
        fov.Init()

        combatSys = NEW CombatSystem()
        combatSys.Init()

        aiSys = NEW AISystem()
        aiSys.Init()

        messages = NEW MessageLog()
        messages.Init()

        saveManager = NEW SaveLoadManager()
        saveManager.Init()

        highScores = NEW HighScoreTable()
        highScores.Init()

        itemFactory = NEW ItemFactory()
        itemFactory.Init()

        monsterFactory = NEW MonsterFactory()
        monsterFactory.Init()

        ' Initialize game state
        gameState = GAME_MENU
        floorLevel = 1
        turnCount = 0
        isRunning = 1
        playerWon = 0
        monsterCount = 0
        groundItemCount = 0

        ' Seed random number generator
        DIM seed AS INTEGER
        seed = 12345
        RANDOMIZE seed
    END SUB

    ' Create a new player
    SUB CreatePlayer(chosenClass AS INTEGER)
        playerClass = chosenClass

        playerEntity = NEW Entity()
        playerEntity.Init(0, 0, "@", CLR_BRIGHT_WHITE, CLR_BLACK)
        playerEntity.SetHasHealth(1)
        playerEntity.SetHasCombat(1)

        playerHealth = NEW HealthComponent()
        playerCombat = NEW CombatComponent()
        playerStats = NEW StatsComponent()
        playerStatus = NEW StatusComponent()
        playerInventory = NEW InventoryComponent()
        playerSpellbook = NEW Spellbook()

        playerStats.Init(chosenClass)
        playerStatus.Init()
        playerInventory.Init()
        playerSpellbook.Init()
        playerSpellbook.LearnClassSpells(chosenClass)

        ' Set HP based on class and constitution
        DIM baseHP AS INTEGER
        SELECT CASE chosenClass
            CASE CLASS_WARRIOR : baseHP = 120
            CASE CLASS_MAGE : baseHP = 60
            CASE CLASS_ROGUE : baseHP = 80
            CASE CLASS_RANGER : baseHP = 90
        END SELECT
        baseHP = baseHP + playerStats.GetStat(STAT_CON) * 5

        playerHealth.Init(baseHP, 1)

        ' Set combat stats based on class
        SELECT CASE chosenClass
            CASE CLASS_WARRIOR
                playerCombat.Init(10, 5, 3, 8, 5)
            CASE CLASS_MAGE
                playerCombat.Init(5, 2, 2, 5, 8)
            CASE CLASS_ROGUE
                playerCombat.Init(8, 3, 2, 6, 15)
            CASE CLASS_RANGER
                playerCombat.Init(7, 4, 2, 7, 10)
        END SELECT

        ' Give starting equipment
        Me.GiveStartingEquipment()
    END SUB

    ' Give starting equipment based on class
    SUB GiveStartingEquipment()
        DIM weapon AS Item
        DIM armor AS Item
        DIM potion AS Item

        weapon = itemFactory.CreateWeapon(WPN_DAGGER, MAT_IRON, 1, ENCH_NONE)
        i = playerInventory.AddItem(weapon)
        playerInventory.Equip(0, SLOT_WEAPON)

        ' Give some starting potions
        potion = itemFactory.CreatePotion(1, 1)    ' Health potion
        i = playerInventory.AddItem(potion)
        potion = itemFactory.CreatePotion(1, 1)    ' Another health potion
        i = playerInventory.AddItem(potion)

        ' Give some starting food
        DIM food AS Item
        food = itemFactory.CreateFood(1)
        i = playerInventory.AddItem(food)
    END SUB

    ' Generate a new dungeon floor
    SUB GenerateFloor()
        dungeon = NEW DungeonMap()
        dungeon.Init(floorLevel)
        dungeon.Generate()

        fov.SetMap(dungeon)
        aiSys.SetMap(dungeon)

        ' Place player at starting position
        playerEntity.SetPosition(dungeon.GetStartX(), dungeon.GetStartY())

        ' Clear old entities
        monsterCount = 0
        groundItemCount = 0

        ' Spawn monsters
        Me.SpawnMonsters()

        ' Spawn items
        Me.SpawnItems()

        ' Calculate initial FOV
        fov.Calculate(playerEntity.GetX(), playerEntity.GetY(), FOV_RADIUS_BASE)

        ' Welcome message
        DIM sb AS Viper.Text.StringBuilder
        sb = NEW Viper.Text.StringBuilder()
        sb.Append("Welcome to floor ")
        sb.Append(Viper.Convert.ToString(floorLevel))
        sb.Append(" of the dungeon!")
        messages.AddInfo(sb.ToString())
    END SUB

    ' Spawn monsters for current floor
    SUB SpawnMonsters()
        DIM count AS INTEGER
        DIM i AS INTEGER
        DIM attempts AS INTEGER
        DIM x AS INTEGER
        DIM y AS INTEGER
        DIM mType AS INTEGER

        count = monsterFactory.GetMonsterCountForFloor(floorLevel)

        FOR i = 1 TO count
            IF monsterCount >= 50 THEN EXIT FOR

            attempts = 0
            DO
                x = 1 + INT(RND() * (dungeon.GetWidth() - 2))
                y = 1 + INT(RND() * (dungeon.GetHeight() - 2))
                attempts = attempts + 1
                IF attempts > 100 THEN EXIT DO
            LOOP WHILE dungeon.IsWalkable(x, y) = 0

            IF dungeon.IsWalkable(x, y) = 1 THEN
                ' Don't spawn on player
                IF x <> playerEntity.GetX() THEN
                    IF y <> playerEntity.GetY() THEN
                        mType = monsterFactory.GetMonsterTypeForFloor(floorLevel)
                        monsters(monsterCount) = monsterFactory.CreateMonster(mType, x, y)
                        monsterCount = monsterCount + 1
                    END IF
                END IF
            END IF
        NEXT i
    END SUB

    ' Spawn items for current floor
    SUB SpawnItems()
        DIM i AS INTEGER
        DIM attempts AS INTEGER
        DIM x AS INTEGER
        DIM y AS INTEGER
        DIM itemCount AS INTEGER

        ' Number of items based on floor
        itemCount = 5 + floorLevel / 2 + INT(RND() * 5)

        FOR i = 1 TO itemCount
            IF groundItemCount >= 100 THEN EXIT FOR

            attempts = 0
            DO
                x = 1 + INT(RND() * (dungeon.GetWidth() - 2))
                y = 1 + INT(RND() * (dungeon.GetHeight() - 2))
                attempts = attempts + 1
                IF attempts > 100 THEN EXIT DO
            LOOP WHILE dungeon.IsWalkable(x, y) = 0

            IF dungeon.IsWalkable(x, y) = 1 THEN
                DIM itm AS Item
                DIM roll AS DOUBLE
                roll = RND()

                ' Determine item type
                IF roll < 0.25 THEN
                    ' Potion
                    DIM potionType AS INTEGER
                    potionType = 1 + INT(RND() * 5)
                    itm = itemFactory.CreatePotion(potionType, floorLevel)
                ELSEIF roll < 0.35 THEN
                    ' Food
                    itm = itemFactory.CreateFood(floorLevel)
                ELSEIF roll < 0.45 THEN
                    ' Gold
                    itm = itemFactory.CreateGold(floorLevel)
                ELSEIF roll < 0.6 THEN
                    ' Weapon
                    DIM wpnType AS INTEGER
                    DIM matType AS INTEGER
                    wpnType = 1 + INT(RND() * 7)
                    matType = 1 + INT(RND() * (1 + floorLevel / 5))
                    IF matType > 4 THEN matType = 4
                    itm = itemFactory.CreateWeapon(wpnType, matType, floorLevel, ENCH_NONE)
                ELSE
                    ' Armor
                    DIM armorSlot AS INTEGER
                    armorSlot = SLOT_CHEST
                    DIM armorMat AS INTEGER
                    armorMat = 1 + INT(RND() * (1 + floorLevel / 5))
                    IF armorMat > 4 THEN armorMat = 4
                    itm = itemFactory.CreateArmor(armorSlot, armorMat, floorLevel, ENCH_NONE)
                END IF

                itm.SetPosition(x, y)
                groundItems(groundItemCount) = itm
                groundItemCount = groundItemCount + 1
            END IF
        NEXT i
    END SUB

    ' Handle player input
    FUNCTION HandleInput() AS INTEGER
        DIM key AS STRING
        DIM moved AS INTEGER
        DIM dx AS INTEGER
        DIM dy AS INTEGER

        key = Viper.Console.ReadKey()
        moved = 0
        dx = 0
        dy = 0

        SELECT CASE key
            ' Movement
            CASE "h", "4" : dx = -1            ' West
            CASE "j", "2" : dy = 1             ' South
            CASE "k", "8" : dy = -1            ' North
            CASE "l", "6" : dx = 1             ' East
            CASE "y", "7" : dx = -1 : dy = -1  ' NW
            CASE "u", "9" : dx = 1 : dy = -1   ' NE
            CASE "b", "1" : dx = -1 : dy = 1   ' SW
            CASE "n", "3" : dx = 1 : dy = 1    ' SE
            CASE "." : moved = 1               ' Wait

            ' Actions
            CASE "g"
                Me.PickupItem()
                moved = 1

            CASE "i"
                gameState = GAME_INVENTORY

            CASE "c"
                gameState = GAME_CHARACTER

            CASE ">"
                Me.UseStairs(1)
                moved = 1

            CASE "<"
                Me.UseStairs(-1)
                moved = 1

            CASE "q", "Q"
                isRunning = 0

            CASE ELSE
                ' Unknown key
        END SELECT

        ' Handle movement
        IF dx <> 0 THEN moved = 1
        IF dy <> 0 THEN moved = 1

        IF moved = 1 THEN
            IF dx <> 0 THEN
                Me.MovePlayer(dx, dy)
            ELSEIF dy <> 0 THEN
                Me.MovePlayer(dx, dy)
            END IF
        END IF

        HandleInput = moved
    END FUNCTION

    ' Move player (or attack if monster in way)
    SUB MovePlayer(dx AS INTEGER, dy AS INTEGER)
        DIM newX AS INTEGER
        DIM newY AS INTEGER

        newX = playerEntity.GetX() + dx
        newY = playerEntity.GetY() + dy

        ' Check for monster
        DIM i AS INTEGER
        FOR i = 0 TO monsterCount - 1
            IF monsters(i).IsAlive() = 1 THEN
                IF monsters(i).GetX() = newX THEN
                    IF monsters(i).GetY() = newY THEN
                        Me.AttackMonster(i)
                        EXIT SUB
                    END IF
                END IF
            END IF
        NEXT i

        ' Check if walkable
        IF dungeon.IsWalkable(newX, newY) = 1 THEN
            playerEntity.SetPosition(newX, newY)

            ' Check for trap
            IF dungeon.GetTile(newX, newY) = TILE_TRAP_HIDDEN THEN
                Me.TriggerTrap(newX, newY)
            ELSEIF dungeon.GetTile(newX, newY) = TILE_TRAP_VISIBLE THEN
                Me.TriggerTrap(newX, newY)
            END IF
        ELSE
            messages.AddInfo("You bump into the wall.")
        END IF
    END SUB

    ' Attack a monster
    SUB AttackMonster(monIdx AS INTEGER)
        DIM result AS CombatResult
        DIM mon AS Monster
        mon = monsters(monIdx)

        result = combatSys.MeleeAttack(playerCombat, "You", _
                                        mon.GetHealth(), mon.GetCombat(), _
                                        mon.GetName())

        messages.AddCombat(result.GetMessage())

        ' Handle lifesteal
        IF result.GetLifesteal() > 0 THEN
            playerHealth.Heal(result.GetLifesteal())
        END IF

        ' Check if killed
        IF result.TargetKilled() = 1 THEN
            mon.Kill()

            ' Award XP
            DIM xp AS INTEGER
            xp = mon.GetXPValue()
            playerStats.AddXP(xp)

            DIM sb AS Viper.Text.StringBuilder
            sb = NEW Viper.Text.StringBuilder()
            sb.Append("You gain ")
            sb.Append(Viper.Convert.ToString(xp))
            sb.Append(" XP!")
            messages.AddSuccess(sb.ToString())

            ' Check level up
            IF playerStats.CheckLevelUp() = 1 THEN
                messages.AddSuccess("You leveled up!")
                ' Increase HP
                playerHealth.SetMax(playerHealth.GetMax() + 10)
                playerHealth.Heal(10)
            END IF

            ' Drop loot
            IF RND() < mon.GetDropChance() THEN
                Me.DropMonsterLoot(mon.GetX(), mon.GetY())
            END IF
        END IF
    END SUB

    ' Drop loot from killed monster
    SUB DropMonsterLoot(x AS INTEGER, y AS INTEGER)
        IF groundItemCount >= 100 THEN EXIT SUB

        DIM itm AS Item
        DIM roll AS DOUBLE
        roll = RND()

        IF roll < 0.3 THEN
            itm = itemFactory.CreateGold(floorLevel)
        ELSEIF roll < 0.5 THEN
            itm = itemFactory.CreatePotion(1, floorLevel)
        ELSEIF roll < 0.7 THEN
            itm = itemFactory.CreateFood(floorLevel)
        ELSE
            DIM wpnType AS INTEGER
            wpnType = 1 + INT(RND() * 7)
            itm = itemFactory.CreateWeapon(wpnType, MAT_IRON, floorLevel, ENCH_NONE)
        END IF

        itm.SetPosition(x, y)
        groundItems(groundItemCount) = itm
        groundItemCount = groundItemCount + 1
    END SUB

    ' Pickup item
    SUB PickupItem()
        DIM i AS INTEGER
        DIM px AS INTEGER
        DIM py AS INTEGER
        px = playerEntity.GetX()
        py = playerEntity.GetY()

        FOR i = 0 TO groundItemCount - 1
            IF groundItems(i).GetX() = px THEN
                IF groundItems(i).GetY() = py THEN
                    DIM itm AS Item
                    itm = groundItems(i)

                    ' Handle gold specially
                    IF itm.GetType() = ITEM_GOLD THEN
                        playerStats.AddGold(itm.GetValue())
                        DIM sb AS Viper.Text.StringBuilder
                        sb = NEW Viper.Text.StringBuilder()
                        sb.Append("You pick up ")
                        sb.Append(Viper.Convert.ToString(itm.GetValue()))
                        sb.Append(" gold!")
                        messages.AddSuccess(sb.ToString())
                    ELSE
                        IF playerInventory.AddItem(itm) = 1 THEN
                            DIM sb AS Viper.Text.StringBuilder
                            sb = NEW Viper.Text.StringBuilder()
                            sb.Append("You pick up ")
                            sb.Append(itm.GetName())
                            messages.AddInfo(sb.ToString())
                        ELSE
                            messages.AddDanger("Your inventory is full!")
                            EXIT SUB
                        END IF
                    END IF

                    ' Remove from ground
                    DIM j AS INTEGER
                    FOR j = i TO groundItemCount - 2
                        groundItems(j) = groundItems(j + 1)
                    NEXT j
                    groundItemCount = groundItemCount - 1
                    EXIT SUB
                END IF
            END IF
        NEXT i

        messages.AddInfo("There's nothing here to pick up.")
    END SUB

    ' Use stairs
    SUB UseStairs(direction AS INTEGER)
        DIM px AS INTEGER
        DIM py AS INTEGER
        DIM tile AS INTEGER

        px = playerEntity.GetX()
        py = playerEntity.GetY()
        tile = dungeon.GetTile(px, py)

        IF direction > 0 THEN
            ' Going down
            IF tile = TILE_STAIRS_DOWN THEN
                floorLevel = floorLevel + 1
                IF floorLevel > MAX_FLOORS THEN
                    ' Victory!
                    playerWon = 1
                    gameState = GAME_WON
                ELSE
                    Me.GenerateFloor()
                END IF
            ELSE
                messages.AddInfo("There are no stairs going down here.")
            END IF
        ELSE
            ' Going up
            IF tile = TILE_STAIRS_UP THEN
                IF floorLevel > 1 THEN
                    floorLevel = floorLevel - 1
                    Me.GenerateFloor()
                ELSE
                    messages.AddInfo("You can't leave the dungeon this way!")
                END IF
            ELSE
                messages.AddInfo("There are no stairs going up here.")
            END IF
        END IF
    END SUB

    ' Trigger trap
    SUB TriggerTrap(x AS INTEGER, y AS INTEGER)
        dungeon.RevealTrap(x, y)

        DIM damage AS INTEGER
        damage = 5 + floorLevel * 2

        ' Rogues have chance to avoid
        IF playerClass = CLASS_ROGUE THEN
            IF RND() < 0.5 THEN
                messages.AddInfo("You nimbly avoid the trap!")
                EXIT SUB
            END IF
        END IF

        playerHealth.TakeDamage(damage)

        DIM sb AS Viper.Text.StringBuilder
        sb = NEW Viper.Text.StringBuilder()
        sb.Append("You trigger a trap and take ")
        sb.Append(Viper.Convert.ToString(damage))
        sb.Append(" damage!")
        messages.AddDanger(sb.ToString())

        IF playerHealth.IsDead() = 1 THEN
            gameState = GAME_DEAD
        END IF
    END SUB

    ' Process monster turns
    SUB ProcessMonsters()
        DIM i AS INTEGER
        DIM mon AS Monster
        DIM canSee AS INTEGER
        DIM moveDir AS INTEGER
        DIM dx AS INTEGER
        DIM dy AS INTEGER
        DIM newX AS INTEGER
        DIM newY AS INTEGER

        FOR i = 0 TO monsterCount - 1
            mon = monsters(i)
            IF mon.IsAlive() = 0 THEN GOTO ContinueMonster

            ' Check if monster can see player
            canSee = dungeon.IsVisible(mon.GetX(), mon.GetY())

            ' Get AI decision
            moveDir = aiSys.GetAIMove(mon, playerEntity.GetX(), playerEntity.GetY(), _
                                       canSee, dungeon)

            IF moveDir = -1 THEN
                ' Melee attack
                Me.MonsterAttack(i)
            ELSEIF moveDir = -2 THEN
                ' Ranged attack (simplified - just melee for now)
                IF canSee = 1 THEN
                    Me.MonsterAttack(i)
                END IF
            ELSEIF moveDir > 0 THEN
                ' Movement
                aiSys.GetDirectionDeltas(moveDir, dx, dy)
                newX = mon.GetX() + dx
                newY = mon.GetY() + dy

                ' Check if can move there
                IF dungeon.IsWalkable(newX, newY) = 1 THEN
                    ' Check not occupied by another monster
                    DIM blocked AS INTEGER
                    blocked = 0
                    DIM j AS INTEGER
                    FOR j = 0 TO monsterCount - 1
                        IF j <> i THEN
                            IF monsters(j).IsAlive() = 1 THEN
                                IF monsters(j).GetX() = newX THEN
                                    IF monsters(j).GetY() = newY THEN
                                        blocked = 1
                                        EXIT FOR
                                    END IF
                                END IF
                            END IF
                        END IF
                    NEXT j

                    ' Don't walk onto player
                    IF newX = playerEntity.GetX() THEN
                        IF newY = playerEntity.GetY() THEN
                            blocked = 1
                        END IF
                    END IF

                    IF blocked = 0 THEN
                        mon.SetPosition(newX, newY)
                    END IF
                END IF
            END IF

            ' Monster regeneration
            mon.GetHealth().Regenerate()

            ContinueMonster:
        NEXT i
    END SUB

    ' Monster attacks player
    SUB MonsterAttack(monIdx AS INTEGER)
        DIM result AS CombatResult
        DIM mon AS Monster
        mon = monsters(monIdx)

        result = combatSys.MeleeAttack(mon.GetCombat(), mon.GetName(), _
                                        playerHealth, playerCombat, "you")

        IF result.DidHit() = 1 THEN
            messages.AddDanger(result.GetMessage())
        ELSE
            messages.AddInfo(result.GetMessage())
        END IF

        ' Handle lifesteal for monster
        IF result.GetLifesteal() > 0 THEN
            mon.GetHealth().Heal(result.GetLifesteal())
        END IF

        IF result.TargetKilled() = 1 THEN
            gameState = GAME_DEAD
        END IF
    END SUB

    ' Process turn effects (hunger, status, etc.)
    SUB ProcessTurnEffects()
        ' Hunger
        playerStats.DecreaseHunger(1)
        IF playerStats.GetHunger() <= 0 THEN
            playerHealth.TakeDamage(1)
            IF turnCount MOD 10 = 0 THEN
                messages.AddDanger("You are starving!")
            END IF
        ELSEIF playerStats.GetHunger() < HUNGER_STARVING THEN
            IF turnCount MOD 20 = 0 THEN
                messages.AddDanger("You are very hungry!")
            END IF
        END IF

        ' Player regeneration
        IF playerStats.GetHunger() > HUNGER_HUNGRY THEN
            playerHealth.Regenerate()
        END IF

        ' Process status effects
        playerStatus.ProcessTurn()

        ' Mana regeneration
        IF turnCount MOD 3 = 0 THEN
            playerStats.RestoreMana(1)
        END IF
    END SUB

    ' Handle inventory screen
    SUB HandleInventory()
        display.DrawInventory(playerInventory)

        DIM key AS STRING
        key = Viper.Console.ReadKey()

        IF key = CHR(27) THEN    ' ESC
            gameState = GAME_PLAYING
            EXIT SUB
        END IF

        ' Letter selection
        DIM idx AS INTEGER
        idx = ASC(key) - 97    ' 'a' = 0

        IF idx >= 0 THEN
            IF idx < playerInventory.GetItemCount() THEN
                Me.UseItem(idx)
            END IF
        END IF

        gameState = GAME_PLAYING
    END SUB

    ' Use an item from inventory
    SUB UseItem(idx AS INTEGER)
        DIM itm AS Item
        itm = playerInventory.GetItem(idx)

        SELECT CASE itm.GetType()
            CASE ITEM_POTION
                ' Use potion
                IF itm.GetHealAmount() > 0 THEN
                    playerHealth.Heal(itm.GetHealAmount())
                    DIM sb AS Viper.Text.StringBuilder
                    sb = NEW Viper.Text.StringBuilder()
                    sb.Append("You drink the ")
                    sb.Append(itm.GetName())
                    sb.Append(" and feel better!")
                    messages.AddSuccess(sb.ToString())
                ELSEIF itm.GetManaRestore() > 0 THEN
                    playerStats.RestoreMana(itm.GetManaRestore())
                    messages.AddMagic("Your mana is restored!")
                END IF
                playerInventory.RemoveItem(idx)

            CASE ITEM_FOOD
                playerStats.IncreaseHunger(itm.GetHungerRestore())
                DIM sb AS Viper.Text.StringBuilder
                sb = NEW Viper.Text.StringBuilder()
                sb.Append("You eat the ")
                sb.Append(itm.GetName())
                sb.Append(".")
                messages.AddInfo(sb.ToString())
                playerInventory.RemoveItem(idx)

            CASE ITEM_WEAPON
                playerInventory.Equip(idx, SLOT_WEAPON)
                ' Update combat stats
                playerCombat.SetMinDamage(itm.GetMinDamage())
                playerCombat.SetMaxDamage(itm.GetMaxDamage())
                messages.AddInfo("You equip the " + itm.GetName() + ".")

            CASE ITEM_ARMOR
                playerInventory.Equip(idx, itm.GetSlot())
                playerCombat.SetDefense(playerCombat.GetDefense() + itm.GetDefenseBonus())
                messages.AddInfo("You equip the " + itm.GetName() + ".")

            CASE ELSE
                messages.AddInfo("You can't use that.")
        END SELECT
    END SUB

    ' Handle character screen
    SUB HandleCharacter()
        display.DrawCharacterSheet(playerStats, playerHealth, playerCombat, playerClass)
        DIM key AS STRING
        key = Viper.Console.ReadKey()
        gameState = GAME_PLAYING
    END SUB

    ' Main game loop
    SUB Run()
        DO WHILE isRunning = 1
            SELECT CASE gameState
                CASE GAME_MENU
                    Me.RunMenu()

                CASE GAME_PLAYING
                    Me.RunGame()

                CASE GAME_INVENTORY
                    Me.HandleInventory()

                CASE GAME_CHARACTER
                    Me.HandleCharacter()

                CASE GAME_DEAD
                    Me.RunGameOver(0)

                CASE GAME_WON
                    Me.RunGameOver(1)
            END SELECT
        LOOP
    END SUB

    ' Menu loop
    SUB RunMenu()
        display.DrawTitle()

        DIM key AS STRING
        key = Viper.Console.ReadKey()

        SELECT CASE key
            CASE "1"
                Me.CreatePlayer(CLASS_WARRIOR)
                floorLevel = 1
                turnCount = 0
                Me.GenerateFloor()
                gameState = GAME_PLAYING

            CASE "2"
                Me.CreatePlayer(CLASS_MAGE)
                floorLevel = 1
                turnCount = 0
                Me.GenerateFloor()
                gameState = GAME_PLAYING

            CASE "3"
                Me.CreatePlayer(CLASS_ROGUE)
                floorLevel = 1
                turnCount = 0
                Me.GenerateFloor()
                gameState = GAME_PLAYING

            CASE "4"
                Me.CreatePlayer(CLASS_RANGER)
                floorLevel = 1
                turnCount = 0
                Me.GenerateFloor()
                gameState = GAME_PLAYING

            CASE "q", "Q"
                isRunning = 0
        END SELECT
    END SUB

    ' Main game loop
    SUB RunGame()
        ' Update FOV
        fov.Calculate(playerEntity.GetX(), playerEntity.GetY(), FOV_RADIUS_BASE)

        ' Draw everything
        display.DrawMap(dungeon, playerEntity.GetX(), playerEntity.GetY(), _
                        monsters(), monsterCount, groundItems(), groundItemCount)
        display.DrawStatus(playerStats, playerHealth, floorLevel, turnCount)
        display.DrawMessages(messages)
        display.DrawSidebar(playerStats, playerInventory)

        ' Handle input
        DIM tookTurn AS INTEGER
        tookTurn = Me.HandleInput()

        ' If player took a turn, process game
        IF tookTurn = 1 THEN
            IF gameState = GAME_PLAYING THEN
                turnCount = turnCount + 1
                Me.ProcessMonsters()
                Me.ProcessTurnEffects()
            END IF
        END IF

        ' Check for death
        IF playerHealth.IsDead() = 1 THEN
            gameState = GAME_DEAD
        END IF
    END SUB

    ' Game over screen
    SUB RunGameOver(won AS INTEGER)
        DIM score AS INTEGER
        score = highScores.CalculateScore(floorLevel, playerStats.GetLevel(), _
                                           playerStats.GetGold(), turnCount, won)

        display.DrawGameOver(won, floorLevel, playerStats.GetLevel(), _
                             playerStats.GetGold(), turnCount)

        DIM key AS STRING
        key = Viper.Console.ReadKey()

        ' Add to high scores
        i = highScores.AddScore("Player", score, floorLevel, playerClass, _
                               playerStats.GetLevel(), won)

        ' Delete save (permadeath)
        saveManager.DeleteSave()

        ' Return to menu
        gameState = GAME_MENU
    END SUB
END CLASS

' ============================================================================
' MAIN PROGRAM
' ============================================================================
DIM game AS Game
game = NEW Game()
game.Init()
game.Run()

' Clean up
Viper.Terminal.ResetFormatting()
Viper.Terminal.Clear()
PRINT "Thanks for playing Viper Roguelike Dungeon!"
