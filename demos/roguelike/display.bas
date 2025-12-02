' display.bas - Terminal rendering and UI system
' Uses Viper.Terminal for full-featured terminal output

' ============================================================================
' MESSAGE LOG CLASS
' ============================================================================
CLASS MessageLog
    DIM messages(49) AS STRING
    DIM msgColors(49) AS INTEGER
    DIM msgCount AS INTEGER
    DIM maxMessages AS INTEGER

    SUB Init()
        msgCount = 0
        maxMessages = 50
    END SUB

    SUB AddMessage(msg AS STRING, clr AS INTEGER)
        ' Shift messages if full
        IF msgCount >= maxMessages THEN
            DIM i AS INTEGER
            FOR i = 0 TO maxMessages - 2
                messages(i) = messages(i + 1)
                msgColors(i) = msgColors(i + 1)
            NEXT i
            msgCount = maxMessages - 1
        END IF

        messages(msgCount) = msg
        msgColors(msgCount) = clr
        msgCount = msgCount + 1
    END SUB

    SUB AddInfo(msg AS STRING)
        Me.AddMessage(msg, CLR_WHITE)
    END SUB

    SUB AddCombat(msg AS STRING)
        Me.AddMessage(msg, CLR_YELLOW)
    END SUB

    SUB AddDanger(msg AS STRING)
        Me.AddMessage(msg, CLR_RED)
    END SUB

    SUB AddSuccess(msg AS STRING)
        Me.AddMessage(msg, CLR_GREEN)
    END SUB

    SUB AddMagic(msg AS STRING)
        Me.AddMessage(msg, CLR_CYAN)
    END SUB

    FUNCTION GetMessage(idx AS INTEGER) AS STRING
        IF idx >= 0 THEN
            IF idx < msgCount THEN
                GetMessage = messages(idx)
                EXIT FUNCTION
            END IF
        END IF
        GetMessage = ""
    END FUNCTION

    FUNCTION GetColor(idx AS INTEGER) AS INTEGER
        IF idx >= 0 THEN
            IF idx < msgCount THEN
                GetColor = msgColors(idx)
                EXIT FUNCTION
            END IF
        END IF
        GetColor = CLR_WHITE
    END FUNCTION

    FUNCTION GetCount() AS INTEGER
        GetCount = msgCount
    END FUNCTION

    ' Get recent messages (last N)
    SUB GetRecent(count AS INTEGER, BYREF outMsgs() AS STRING, BYREF outColors() AS INTEGER, BYREF outCount AS INTEGER)
        DIM start AS INTEGER
        DIM i AS INTEGER
        DIM j AS INTEGER

        IF count > msgCount THEN count = msgCount
        start = msgCount - count

        j = 0
        FOR i = start TO msgCount - 1
            outMsgs(j) = messages(i)
            outColors(j) = msgColors(i)
            j = j + 1
        NEXT i
        outCount = j
    END SUB
END CLASS

' ============================================================================
' DISPLAY SYSTEM CLASS
' ============================================================================
CLASS DisplaySystem
    DIM screenWidth AS INTEGER
    DIM screenHeight AS INTEGER
    DIM viewWidth AS INTEGER
    DIM viewHeight AS INTEGER
    DIM viewX AS INTEGER
    DIM viewY AS INTEGER
    DIM statusY AS INTEGER
    DIM msgY AS INTEGER

    SUB Init()
        screenWidth = SCREEN_WIDTH
        screenHeight = SCREEN_HEIGHT
        viewWidth = MAP_VIEW_WIDTH
        viewHeight = MAP_VIEW_HEIGHT
        viewX = MAP_VIEW_X
        viewY = MAP_VIEW_Y
        statusY = STATUS_Y
        msgY = MSG_Y
    END SUB

    ' Clear the entire screen
    SUB ClearScreen()
        Viper.Terminal.Clear()
    END SUB

    ' Set cursor position
    SUB SetCursor(x AS INTEGER, y AS INTEGER)
        Viper.Terminal.SetCursorPosition(x, y)
    END SUB

    ' Set foreground color
    SUB SetFg(clr AS INTEGER)
        SELECT CASE clr
            CASE CLR_BLACK : Viper.Terminal.SetForegroundColor(30)
            CASE CLR_RED : Viper.Terminal.SetForegroundColor(31)
            CASE CLR_GREEN : Viper.Terminal.SetForegroundColor(32)
            CASE CLR_YELLOW : Viper.Terminal.SetForegroundColor(33)
            CASE CLR_BLUE : Viper.Terminal.SetForegroundColor(34)
            CASE CLR_MAGENTA : Viper.Terminal.SetForegroundColor(35)
            CASE CLR_CYAN : Viper.Terminal.SetForegroundColor(36)
            CASE CLR_WHITE : Viper.Terminal.SetForegroundColor(37)
            CASE CLR_BRIGHT_BLACK : Viper.Terminal.SetForegroundColor(90)
            CASE CLR_BRIGHT_RED : Viper.Terminal.SetForegroundColor(91)
            CASE CLR_BRIGHT_GREEN : Viper.Terminal.SetForegroundColor(92)
            CASE CLR_BRIGHT_YELLOW : Viper.Terminal.SetForegroundColor(93)
            CASE CLR_BRIGHT_BLUE : Viper.Terminal.SetForegroundColor(94)
            CASE CLR_BRIGHT_MAGENTA : Viper.Terminal.SetForegroundColor(95)
            CASE CLR_BRIGHT_CYAN : Viper.Terminal.SetForegroundColor(96)
            CASE CLR_BRIGHT_WHITE : Viper.Terminal.SetForegroundColor(97)
        END SELECT
    END SUB

    ' Set background color
    SUB SetBg(clr AS INTEGER)
        SELECT CASE clr
            CASE CLR_BLACK : Viper.Terminal.SetBackgroundColor(40)
            CASE CLR_RED : Viper.Terminal.SetBackgroundColor(41)
            CASE CLR_GREEN : Viper.Terminal.SetBackgroundColor(42)
            CASE CLR_YELLOW : Viper.Terminal.SetBackgroundColor(43)
            CASE CLR_BLUE : Viper.Terminal.SetBackgroundColor(44)
            CASE CLR_MAGENTA : Viper.Terminal.SetBackgroundColor(45)
            CASE CLR_CYAN : Viper.Terminal.SetBackgroundColor(46)
            CASE CLR_WHITE : Viper.Terminal.SetBackgroundColor(47)
            CASE ELSE : Viper.Terminal.SetBackgroundColor(40)
        END SELECT
    END SUB

    ' Reset colors
    SUB ResetColors()
        Viper.Terminal.ResetFormatting()
    END SUB

    ' Draw a character at position
    SUB DrawChar(x AS INTEGER, y AS INTEGER, ch AS STRING, fg AS INTEGER, bg AS INTEGER)
        Me.SetCursor(x, y)
        Me.SetFg(fg)
        Me.SetBg(bg)
        PRINT ch;
    END SUB

    ' Draw text at position
    SUB DrawText(x AS INTEGER, y AS INTEGER, txt AS STRING, fg AS INTEGER)
        Me.SetCursor(x, y)
        Me.SetFg(fg)
        PRINT txt;
    END SUB

    ' Draw the map viewport
    SUB DrawMap(dm AS DungeonMap, playerX AS INTEGER, playerY AS INTEGER, _
                monsters() AS Monster, monsterCount AS INTEGER, _
                items() AS Item, itemCount AS INTEGER)
        DIM x AS INTEGER
        DIM y AS INTEGER
        DIM mapX AS INTEGER
        DIM mapY AS INTEGER
        DIM offsetX AS INTEGER
        DIM offsetY AS INTEGER
        DIM sym AS STRING
        DIM fg AS INTEGER
        DIM bg AS INTEGER

        ' Calculate viewport offset to center on player
        offsetX = playerX - viewWidth / 2
        offsetY = playerY - viewHeight / 2

        ' Clamp to map bounds
        IF offsetX < 0 THEN offsetX = 0
        IF offsetY < 0 THEN offsetY = 0
        IF offsetX > dm.GetWidth() - viewWidth THEN offsetX = dm.GetWidth() - viewWidth
        IF offsetY > dm.GetHeight() - viewHeight THEN offsetY = dm.GetHeight() - viewHeight

        ' Draw map tiles
        FOR y = 0 TO viewHeight - 1
            Me.SetCursor(viewX, viewY + y)

            FOR x = 0 TO viewWidth - 1
                mapX = offsetX + x
                mapY = offsetY + y

                IF dm.IsVisible(mapX, mapY) = 1 THEN
                    ' Visible tile
                    sym = dm.GetTileSymbol(mapX, mapY)
                    fg = dm.GetTileColor(mapX, mapY)
                    bg = CLR_BLACK

                    ' Check for light
                    IF dm.GetLight(mapX, mapY) > 0 THEN
                        ' Lit area - brighten
                        IF fg < 8 THEN fg = fg + 8
                    END IF

                    Me.SetFg(fg)
                    Me.SetBg(bg)
                    PRINT sym;

                ELSEIF dm.IsExplored(mapX, mapY) = 1 THEN
                    ' Explored but not visible - dim
                    sym = dm.GetTileSymbol(mapX, mapY)
                    Me.SetFg(CLR_BRIGHT_BLACK)
                    Me.SetBg(CLR_BLACK)
                    PRINT sym;
                ELSE
                    ' Unexplored
                    Me.SetFg(CLR_BLACK)
                    Me.SetBg(CLR_BLACK)
                    PRINT " ";
                END IF
            NEXT x
        NEXT y

        ' Draw items (only if visible)
        DIM i AS INTEGER
        FOR i = 0 TO itemCount - 1
            DIM itm AS Item
            itm = items(i)
            IF itm.GetX() >= offsetX THEN
                IF itm.GetX() < offsetX + viewWidth THEN
                    IF itm.GetY() >= offsetY THEN
                        IF itm.GetY() < offsetY + viewHeight THEN
                            IF dm.IsVisible(itm.GetX(), itm.GetY()) = 1 THEN
                                Me.DrawChar(viewX + itm.GetX() - offsetX, _
                                           viewY + itm.GetY() - offsetY, _
                                           itm.GetSymbol(), itm.GetColor(), CLR_BLACK)
                            END IF
                        END IF
                    END IF
                END IF
            END IF
        NEXT i

        ' Draw monsters (only if visible)
        FOR i = 0 TO monsterCount - 1
            DIM mon AS Monster
            mon = monsters(i)
            IF mon.IsAlive() = 1 THEN
                IF mon.GetX() >= offsetX THEN
                    IF mon.GetX() < offsetX + viewWidth THEN
                        IF mon.GetY() >= offsetY THEN
                            IF mon.GetY() < offsetY + viewHeight THEN
                                IF dm.IsVisible(mon.GetX(), mon.GetY()) = 1 THEN
                                    Me.DrawChar(viewX + mon.GetX() - offsetX, _
                                               viewY + mon.GetY() - offsetY, _
                                               mon.GetSymbol(), mon.GetFgColor(), CLR_BLACK)
                                END IF
                            END IF
                        END IF
                    END IF
                END IF
            END IF
        NEXT i

        ' Draw player
        Me.DrawChar(viewX + playerX - offsetX, viewY + playerY - offsetY, "@", CLR_BRIGHT_WHITE, CLR_BLACK)

        Me.ResetColors()
    END SUB

    ' Draw status bar
    SUB DrawStatus(playerStats AS StatsComponent, playerHealth AS HealthComponent, _
                    floorLevel AS INTEGER, turnCount AS INTEGER)
        DIM sb AS Viper.Text.StringBuilder
        sb = NEW Viper.Text.StringBuilder()

        ' Clear status lines
        Me.SetCursor(1, statusY)
        Me.SetFg(CLR_WHITE)
        DIM blank AS STRING
        blank = Viper.String.Space(78)
        PRINT blank;
        Me.SetCursor(1, statusY + 1)
        PRINT blank;

        ' Line 1: Health, Mana, Level
        Me.SetCursor(1, statusY)

        ' HP bar
        Me.SetFg(CLR_WHITE)
        PRINT "HP: ";
        IF playerHealth.GetCurrent() < playerHealth.GetMax() / 4 THEN
            Me.SetFg(CLR_RED)
        ELSEIF playerHealth.GetCurrent() < playerHealth.GetMax() / 2 THEN
            Me.SetFg(CLR_YELLOW)
        ELSE
            Me.SetFg(CLR_GREEN)
        END IF
        sb.Clear()
        sb.Append(Viper.Convert.ToString(playerHealth.GetCurrent()))
        sb.Append("/")
        sb.Append(Viper.Convert.ToString(playerHealth.GetMax()))
        PRINT sb.ToString();

        ' Mana
        Me.SetFg(CLR_WHITE)
        PRINT "  MP: ";
        Me.SetFg(CLR_CYAN)
        sb.Clear()
        sb.Append(Viper.Convert.ToString(playerStats.GetMana()))
        sb.Append("/")
        sb.Append(Viper.Convert.ToString(playerStats.GetMaxMana()))
        PRINT sb.ToString();

        ' Level
        Me.SetFg(CLR_WHITE)
        PRINT "  Lv: ";
        Me.SetFg(CLR_YELLOW)
        PRINT Viper.Convert.ToString(playerStats.GetLevel());

        ' XP
        Me.SetFg(CLR_WHITE)
        PRINT "  XP: ";
        Me.SetFg(CLR_BRIGHT_YELLOW)
        sb.Clear()
        sb.Append(Viper.Convert.ToString(playerStats.GetXP()))
        sb.Append("/")
        sb.Append(Viper.Convert.ToString(playerStats.GetXPForNextLevel()))
        PRINT sb.ToString();

        ' Floor
        Me.SetFg(CLR_WHITE)
        PRINT "  Floor: ";
        Me.SetFg(CLR_BRIGHT_WHITE)
        sb.Clear()
        sb.Append(Viper.Convert.ToString(floorLevel))
        sb.Append("/")
        sb.Append(Viper.Convert.ToString(MAX_FLOORS))
        PRINT sb.ToString();

        ' Line 2: Gold, Hunger, Turn
        Me.SetCursor(1, statusY + 1)

        ' Gold
        Me.SetFg(CLR_WHITE)
        PRINT "Gold: ";
        Me.SetFg(CLR_BRIGHT_YELLOW)
        PRINT Viper.Convert.ToString(playerStats.GetGold());

        ' Hunger
        Me.SetFg(CLR_WHITE)
        PRINT "  Hunger: ";
        DIM hunger AS INTEGER
        hunger = playerStats.GetHunger()
        IF hunger > HUNGER_FULL THEN
            Me.SetFg(CLR_GREEN)
            PRINT "Full";
        ELSEIF hunger > HUNGER_NORMAL THEN
            Me.SetFg(CLR_WHITE)
            PRINT "Normal";
        ELSEIF hunger > HUNGER_HUNGRY THEN
            Me.SetFg(CLR_YELLOW)
            PRINT "Hungry";
        ELSEIF hunger > HUNGER_STARVING THEN
            Me.SetFg(CLR_RED)
            PRINT "Weak";
        ELSE
            Me.SetFg(CLR_BRIGHT_RED)
            PRINT "Starving!";
        END IF

        ' Turn
        Me.SetFg(CLR_WHITE)
        PRINT "  Turn: ";
        Me.SetFg(CLR_WHITE)
        PRINT Viper.Convert.ToString(turnCount);

        Me.ResetColors()
    END SUB

    ' Draw message log
    SUB DrawMessages(log AS MessageLog)
        DIM i AS INTEGER
        DIM startIdx AS INTEGER
        DIM y AS INTEGER

        ' Clear message area
        DIM blank AS STRING
        blank = Viper.String.Space(78)
        FOR i = 0 TO MSG_COUNT - 1
            Me.SetCursor(1, msgY + i)
            Me.SetFg(CLR_BLACK)
            PRINT blank;
        NEXT i

        ' Get last N messages
        startIdx = log.GetCount() - MSG_COUNT
        IF startIdx < 0 THEN startIdx = 0

        y = 0
        FOR i = startIdx TO log.GetCount() - 1
            Me.SetCursor(1, msgY + y)
            Me.SetFg(log.GetColor(i))
            PRINT log.GetMessage(i);
            y = y + 1
            IF y >= MSG_COUNT THEN EXIT FOR
        NEXT i

        Me.ResetColors()
    END SUB

    ' Draw sidebar with info
    SUB DrawSidebar(playerStats AS StatsComponent, playerInventory AS InventoryComponent)
        DIM x AS INTEGER
        DIM y AS INTEGER
        DIM sb AS Viper.Text.StringBuilder
        sb = NEW Viper.Text.StringBuilder()

        x = viewX + viewWidth + 2
        y = viewY

        ' Stats header
        Me.DrawText(x, y, "=== STATS ===", CLR_BRIGHT_WHITE)
        y = y + 1

        ' Stats
        Me.SetFg(CLR_WHITE)

        Me.SetCursor(x, y)
        PRINT "STR: ";
        Me.SetFg(CLR_BRIGHT_YELLOW)
        PRINT Viper.Convert.ToString(playerStats.GetStat(STAT_STR));
        y = y + 1

        Me.SetFg(CLR_WHITE)
        Me.SetCursor(x, y)
        PRINT "DEX: ";
        Me.SetFg(CLR_BRIGHT_YELLOW)
        PRINT Viper.Convert.ToString(playerStats.GetStat(STAT_DEX));
        y = y + 1

        Me.SetFg(CLR_WHITE)
        Me.SetCursor(x, y)
        PRINT "CON: ";
        Me.SetFg(CLR_BRIGHT_YELLOW)
        PRINT Viper.Convert.ToString(playerStats.GetStat(STAT_CON));
        y = y + 1

        Me.SetFg(CLR_WHITE)
        Me.SetCursor(x, y)
        PRINT "INT: ";
        Me.SetFg(CLR_BRIGHT_YELLOW)
        PRINT Viper.Convert.ToString(playerStats.GetStat(STAT_INT));
        y = y + 1

        Me.SetFg(CLR_WHITE)
        Me.SetCursor(x, y)
        PRINT "WIS: ";
        Me.SetFg(CLR_BRIGHT_YELLOW)
        PRINT Viper.Convert.ToString(playerStats.GetStat(STAT_WIS));
        y = y + 1

        Me.SetFg(CLR_WHITE)
        Me.SetCursor(x, y)
        PRINT "CHA: ";
        Me.SetFg(CLR_BRIGHT_YELLOW)
        PRINT Viper.Convert.ToString(playerStats.GetStat(STAT_CHA));
        y = y + 2

        ' Equipment header
        Me.DrawText(x, y, "=== EQUIP ===", CLR_BRIGHT_WHITE)
        y = y + 1

        ' Show weapon
        Me.SetFg(CLR_WHITE)
        Me.SetCursor(x, y)
        PRINT "Wpn: ";
        DIM wpn AS Item
        wpn = playerInventory.GetEquipped(SLOT_WEAPON)
        IF wpn.GetType() > 0 THEN
            Me.SetFg(CLR_BRIGHT_CYAN)
            PRINT Viper.String.Left(wpn.GetName(), 10);
        ELSE
            Me.SetFg(CLR_BRIGHT_BLACK)
            PRINT "None";
        END IF
        y = y + 1

        ' Show armor
        Me.SetFg(CLR_WHITE)
        Me.SetCursor(x, y)
        PRINT "Arm: ";
        DIM arm AS Item
        arm = playerInventory.GetEquipped(SLOT_CHEST)
        IF arm.GetType() > 0 THEN
            Me.SetFg(CLR_BRIGHT_CYAN)
            PRINT Viper.String.Left(arm.GetName(), 10);
        ELSE
            Me.SetFg(CLR_BRIGHT_BLACK)
            PRINT "None";
        END IF
        y = y + 2

        ' Keys
        Me.DrawText(x, y, "=== KEYS ===", CLR_BRIGHT_WHITE)
        y = y + 1
        Me.DrawText(x, y, "hjkl/arrows", CLR_WHITE)
        y = y + 1
        Me.DrawText(x, y, "i=inventory", CLR_WHITE)
        y = y + 1
        Me.DrawText(x, y, "g=get item", CLR_WHITE)
        y = y + 1
        Me.DrawText(x, y, ">/< stairs", CLR_WHITE)
        y = y + 1
        Me.DrawText(x, y, "q=quit", CLR_WHITE)

        Me.ResetColors()
    END SUB

    ' Draw inventory screen
    SUB DrawInventory(playerInventory AS InventoryComponent)
        Me.ClearScreen()

        DIM y AS INTEGER
        DIM i AS INTEGER
        DIM letter AS STRING

        Me.DrawText(10, 1, "=== INVENTORY ===", CLR_BRIGHT_WHITE)
        Me.DrawText(10, 2, "Press letter to use, ESC to close", CLR_WHITE)
        y = 4

        DIM count AS INTEGER
        count = playerInventory.GetItemCount()

        IF count = 0 THEN
            Me.DrawText(10, y, "Your inventory is empty.", CLR_BRIGHT_BLACK)
        ELSE
            FOR i = 0 TO count - 1
                DIM itm AS Item
                itm = playerInventory.GetItem(i)

                letter = CHR(97 + i)    ' a, b, c, ...

                Me.SetCursor(10, y)
                Me.SetFg(CLR_BRIGHT_YELLOW)
                PRINT letter;
                PRINT ") ";
                Me.SetFg(itm.GetColor())
                PRINT itm.GetSymbol();
                PRINT " ";
                Me.SetFg(CLR_WHITE)
                PRINT itm.GetName();

                ' Show if equipped
                IF playerInventory.IsEquipped(i) = 1 THEN
                    Me.SetFg(CLR_BRIGHT_GREEN)
                    PRINT " [equipped]";
                END IF

                y = y + 1
                IF y > 22 THEN EXIT FOR
            NEXT i
        END IF

        Me.ResetColors()
    END SUB

    ' Draw character screen
    SUB DrawCharacterSheet(playerStats AS StatsComponent, playerHealth AS HealthComponent, _
                            playerCombat AS CombatComponent, playerClass AS INTEGER)
        Me.ClearScreen()

        DIM y AS INTEGER
        y = 1

        Me.DrawText(10, y, "=== CHARACTER SHEET ===", CLR_BRIGHT_WHITE)
        y = y + 2

        ' Class
        Me.SetCursor(10, y)
        Me.SetFg(CLR_WHITE)
        PRINT "Class: ";
        Me.SetFg(CLR_BRIGHT_YELLOW)
        SELECT CASE playerClass
            CASE CLASS_WARRIOR : PRINT "Warrior"
            CASE CLASS_MAGE : PRINT "Mage"
            CASE CLASS_ROGUE : PRINT "Rogue"
            CASE CLASS_RANGER : PRINT "Ranger"
        END SELECT
        y = y + 1

        ' Level
        Me.SetCursor(10, y)
        Me.SetFg(CLR_WHITE)
        PRINT "Level: ";
        Me.SetFg(CLR_BRIGHT_WHITE)
        PRINT Viper.Convert.ToString(playerStats.GetLevel())
        y = y + 2

        ' Stats
        Me.DrawText(10, y, "--- ATTRIBUTES ---", CLR_BRIGHT_CYAN)
        y = y + 1

        Me.SetCursor(10, y)
        Me.SetFg(CLR_WHITE)
        PRINT "Strength:     ";
        Me.SetFg(CLR_BRIGHT_YELLOW)
        PRINT Viper.Convert.ToString(playerStats.GetStat(STAT_STR))
        y = y + 1

        Me.SetCursor(10, y)
        Me.SetFg(CLR_WHITE)
        PRINT "Dexterity:    ";
        Me.SetFg(CLR_BRIGHT_YELLOW)
        PRINT Viper.Convert.ToString(playerStats.GetStat(STAT_DEX))
        y = y + 1

        Me.SetCursor(10, y)
        Me.SetFg(CLR_WHITE)
        PRINT "Constitution: ";
        Me.SetFg(CLR_BRIGHT_YELLOW)
        PRINT Viper.Convert.ToString(playerStats.GetStat(STAT_CON))
        y = y + 1

        Me.SetCursor(10, y)
        Me.SetFg(CLR_WHITE)
        PRINT "Intelligence: ";
        Me.SetFg(CLR_BRIGHT_YELLOW)
        PRINT Viper.Convert.ToString(playerStats.GetStat(STAT_INT))
        y = y + 1

        Me.SetCursor(10, y)
        Me.SetFg(CLR_WHITE)
        PRINT "Wisdom:       ";
        Me.SetFg(CLR_BRIGHT_YELLOW)
        PRINT Viper.Convert.ToString(playerStats.GetStat(STAT_WIS))
        y = y + 1

        Me.SetCursor(10, y)
        Me.SetFg(CLR_WHITE)
        PRINT "Charisma:     ";
        Me.SetFg(CLR_BRIGHT_YELLOW)
        PRINT Viper.Convert.ToString(playerStats.GetStat(STAT_CHA))
        y = y + 2

        ' Combat stats
        Me.DrawText(10, y, "--- COMBAT ---", CLR_BRIGHT_RED)
        y = y + 1

        Me.SetCursor(10, y)
        Me.SetFg(CLR_WHITE)
        PRINT "Attack:   ";
        Me.SetFg(CLR_BRIGHT_WHITE)
        PRINT Viper.Convert.ToString(playerCombat.GetAttack())
        y = y + 1

        Me.SetCursor(10, y)
        Me.SetFg(CLR_WHITE)
        PRINT "Defense:  ";
        Me.SetFg(CLR_BRIGHT_WHITE)
        PRINT Viper.Convert.ToString(playerCombat.GetDefense())
        y = y + 1

        Me.SetCursor(10, y)
        Me.SetFg(CLR_WHITE)
        PRINT "Damage:   ";
        Me.SetFg(CLR_BRIGHT_WHITE)
        DIM sb AS Viper.Text.StringBuilder
        sb = NEW Viper.Text.StringBuilder()
        sb.Append(Viper.Convert.ToString(playerCombat.GetMinDamage()))
        sb.Append("-")
        sb.Append(Viper.Convert.ToString(playerCombat.GetMaxDamage()))
        PRINT sb.ToString()
        y = y + 1

        Me.SetCursor(10, y)
        Me.SetFg(CLR_WHITE)
        PRINT "Crit %:   ";
        Me.SetFg(CLR_BRIGHT_WHITE)
        PRINT Viper.Convert.ToString(playerCombat.GetCritChance())
        y = y + 2

        Me.DrawText(10, y, "Press any key to continue...", CLR_BRIGHT_BLACK)

        Me.ResetColors()
    END SUB

    ' Draw title screen
    SUB DrawTitle()
        Me.ClearScreen()

        Me.DrawText(25, 5, "=================================", CLR_BRIGHT_RED)
        Me.DrawText(25, 6, "     VIPER ROGUELIKE DUNGEON     ", CLR_BRIGHT_WHITE)
        Me.DrawText(25, 7, "=================================", CLR_BRIGHT_RED)
        Me.DrawText(25, 9, "  A classic roguelike adventure", CLR_WHITE)
        Me.DrawText(25, 10, "  20 floors of deadly dungeons", CLR_WHITE)
        Me.DrawText(25, 12, "     SELECT YOUR CLASS:", CLR_BRIGHT_YELLOW)
        Me.DrawText(25, 14, "  1. Warrior - Strong & Tough", CLR_WHITE)
        Me.DrawText(25, 15, "  2. Mage    - Arcane Power", CLR_WHITE)
        Me.DrawText(25, 16, "  3. Rogue   - Quick & Deadly", CLR_WHITE)
        Me.DrawText(25, 17, "  4. Ranger  - Skilled Hunter", CLR_WHITE)
        Me.DrawText(25, 19, "  Press 1-4 to select class", CLR_BRIGHT_BLACK)
        Me.DrawText(25, 20, "  Press Q to quit", CLR_BRIGHT_BLACK)

        Me.ResetColors()
    END SUB

    ' Draw game over screen
    SUB DrawGameOver(won AS INTEGER, floor AS INTEGER, level AS INTEGER, gold AS INTEGER, turns AS INTEGER)
        Me.ClearScreen()

        IF won = 1 THEN
            Me.DrawText(25, 8, "================================", CLR_BRIGHT_YELLOW)
            Me.DrawText(25, 9, "         CONGRATULATIONS!        ", CLR_BRIGHT_GREEN)
            Me.DrawText(25, 10, "================================", CLR_BRIGHT_YELLOW)
            Me.DrawText(25, 12, "   You conquered the dungeon!", CLR_WHITE)
        ELSE
            Me.DrawText(25, 8, "================================", CLR_RED)
            Me.DrawText(25, 9, "           GAME OVER             ", CLR_BRIGHT_RED)
            Me.DrawText(25, 10, "================================", CLR_RED)
            Me.DrawText(25, 12, "    You have been slain...", CLR_WHITE)
        END IF

        DIM sb AS Viper.Text.StringBuilder
        sb = NEW Viper.Text.StringBuilder()

        Me.SetCursor(25, 14)
        Me.SetFg(CLR_WHITE)
        PRINT "    Floor reached: ";
        Me.SetFg(CLR_BRIGHT_YELLOW)
        PRINT Viper.Convert.ToString(floor)

        Me.SetCursor(25, 15)
        Me.SetFg(CLR_WHITE)
        PRINT "    Character level: ";
        Me.SetFg(CLR_BRIGHT_YELLOW)
        PRINT Viper.Convert.ToString(level)

        Me.SetCursor(25, 16)
        Me.SetFg(CLR_WHITE)
        PRINT "    Gold collected: ";
        Me.SetFg(CLR_BRIGHT_YELLOW)
        PRINT Viper.Convert.ToString(gold)

        Me.SetCursor(25, 17)
        Me.SetFg(CLR_WHITE)
        PRINT "    Turns survived: ";
        Me.SetFg(CLR_BRIGHT_YELLOW)
        PRINT Viper.Convert.ToString(turns)

        Me.DrawText(25, 20, "  Press any key to continue...", CLR_BRIGHT_BLACK)

        Me.ResetColors()
    END SUB
END CLASS
