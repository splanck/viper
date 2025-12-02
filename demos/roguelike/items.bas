' items.bas - Item system for Roguelike RPG
' All item types, equipment, and inventory management

' ============================================================================
' ITEM CLASS
' ============================================================================
CLASS Item
    DIM itemId AS INTEGER
    DIM itemName AS STRING
    DIM itemType AS INTEGER
    DIM itemSymbol AS STRING
    DIM itemColor AS INTEGER

    ' Item properties
    DIM isStackable AS INTEGER
    DIM stackCount AS INTEGER
    DIM isIdentified AS INTEGER
    DIM isCursed AS INTEGER
    DIM isEquipped AS INTEGER

    ' Equipment stats
    DIM equipSlot AS INTEGER
    DIM damageMin AS INTEGER
    DIM damageMax AS INTEGER
    DIM armorValue AS INTEGER
    DIM bonusStr AS INTEGER
    DIM bonusDex AS INTEGER
    DIM bonusCon AS INTEGER
    DIM bonusInt AS INTEGER
    DIM bonusWis AS INTEGER
    DIM bonusCha AS INTEGER

    ' Weapon/material properties
    DIM weaponType AS INTEGER
    DIM materialTier AS INTEGER
    DIM enchantment AS INTEGER

    ' Consumable properties
    DIM useEffect AS INTEGER
    DIM effectPower AS INTEGER

    ' Value
    DIM goldValue AS INTEGER

    SUB Init(newId AS INTEGER, nm AS STRING, typ AS INTEGER)
        itemId = newId
        itemName = nm
        itemType = typ
        itemSymbol = "?"
        itemColor = CLR_WHITE
        isStackable = 0
        stackCount = 1
        isIdentified = 0
        isCursed = 0
        isEquipped = 0
        equipSlot = -1
        damageMin = 0
        damageMax = 0
        armorValue = 0
        bonusStr = 0
        bonusDex = 0
        bonusCon = 0
        bonusInt = 0
        bonusWis = 0
        bonusCha = 0
        weaponType = 0
        materialTier = MAT_IRON
        enchantment = ENCH_NONE
        useEffect = 0
        effectPower = 0
        goldValue = 1

        ' Set symbol/color by type
        IF typ = ITEM_WEAPON THEN
            itemSymbol = "/"
            itemColor = CLR_CYAN
        END IF
        IF typ = ITEM_ARMOR THEN
            itemSymbol = "["
            itemColor = CLR_BLUE
        END IF
        IF typ = ITEM_POTION THEN
            itemSymbol = "!"
            itemColor = CLR_MAGENTA
            isStackable = 1
        END IF
        IF typ = ITEM_SCROLL THEN
            itemSymbol = "?"
            itemColor = CLR_WHITE
            isStackable = 1
        END IF
        IF typ = ITEM_FOOD THEN
            itemSymbol = "%"
            itemColor = CLR_YELLOW
            isStackable = 1
        END IF
        IF typ = ITEM_KEY THEN
            itemSymbol = "-"
            itemColor = CLR_BRIGHT_YELLOW
        END IF
        IF typ = ITEM_GOLD THEN
            itemSymbol = "$"
            itemColor = CLR_BRIGHT_YELLOW
            isStackable = 1
            isIdentified = 1
        END IF
        IF typ = ITEM_RING THEN
            itemSymbol = "="
            itemColor = CLR_BRIGHT_CYAN
        END IF
        IF typ = ITEM_AMULET THEN
            itemSymbol = "\""
            itemColor = CLR_BRIGHT_MAGENTA
        END IF
        IF typ = ITEM_ARTIFACT THEN
            itemSymbol = "*"
            itemColor = CLR_BRIGHT_YELLOW
        END IF
    END SUB

    FUNCTION GetId() AS INTEGER
        GetId = itemId
    END FUNCTION

    FUNCTION GetName() AS STRING
        IF isIdentified = 1 THEN
            GetName = itemName
        ELSE
            IF itemType = ITEM_POTION THEN GetName = "Unknown Potion"
            IF itemType = ITEM_SCROLL THEN GetName = "Unknown Scroll"
            IF itemType = ITEM_RING THEN GetName = "Unknown Ring"
            IF itemType = ITEM_AMULET THEN GetName = "Unknown Amulet"
            IF itemType = ITEM_WEAPON THEN GetName = itemName
            IF itemType = ITEM_ARMOR THEN GetName = itemName
            IF itemType = ITEM_FOOD THEN GetName = itemName
            IF itemType = ITEM_GOLD THEN GetName = itemName
            IF itemType = ITEM_KEY THEN GetName = itemName
            IF itemType = ITEM_ARTIFACT THEN GetName = "Strange Artifact"
        END IF
    END FUNCTION

    FUNCTION GetFullName() AS STRING
        GetFullName = itemName
    END FUNCTION

    FUNCTION GetType() AS INTEGER
        GetType = itemType
    END FUNCTION

    FUNCTION GetSymbol() AS STRING
        GetSymbol = itemSymbol
    END FUNCTION

    FUNCTION GetColor() AS INTEGER
        GetColor = itemColor
    END FUNCTION

    SUB SetEquipStats(slot AS INTEGER, dmin AS INTEGER, dmax AS INTEGER, armor AS INTEGER)
        equipSlot = slot
        damageMin = dmin
        damageMax = dmax
        armorValue = armor
    END SUB

    FUNCTION GetSlot() AS INTEGER
        GetSlot = equipSlot
    END FUNCTION

    FUNCTION GetDamageMin() AS INTEGER
        GetDamageMin = damageMin
    END FUNCTION

    FUNCTION GetDamageMax() AS INTEGER
        GetDamageMax = damageMax
    END FUNCTION

    FUNCTION GetArmor() AS INTEGER
        GetArmor = armorValue
    END FUNCTION

    SUB SetBonus(stat AS INTEGER, amount AS INTEGER)
        IF stat = STAT_STR THEN bonusStr = amount
        IF stat = STAT_DEX THEN bonusDex = amount
        IF stat = STAT_CON THEN bonusCon = amount
        IF stat = STAT_INT THEN bonusInt = amount
        IF stat = STAT_WIS THEN bonusWis = amount
        IF stat = STAT_CHA THEN bonusCha = amount
    END SUB

    FUNCTION GetBonus(stat AS INTEGER) AS INTEGER
        GetBonus = 0
        IF stat = STAT_STR THEN GetBonus = bonusStr
        IF stat = STAT_DEX THEN GetBonus = bonusDex
        IF stat = STAT_CON THEN GetBonus = bonusCon
        IF stat = STAT_INT THEN GetBonus = bonusInt
        IF stat = STAT_WIS THEN GetBonus = bonusWis
        IF stat = STAT_CHA THEN GetBonus = bonusCha
    END FUNCTION

    SUB SetMaterial(mat AS INTEGER)
        materialTier = mat
        ' Adjust stats based on material
        DIM mult AS INTEGER
        mult = mat  ' 1-4
        damageMin = damageMin + mult
        damageMax = damageMax + mult
        armorValue = armorValue + mult
        goldValue = goldValue * mult
    END SUB

    SUB SetEnchantment(ench AS INTEGER)
        enchantment = ench
        goldValue = goldValue * 2
    END SUB

    FUNCTION GetEnchantment() AS INTEGER
        GetEnchantment = enchantment
    END FUNCTION

    SUB SetConsumable(effect AS INTEGER, power AS INTEGER)
        useEffect = effect
        effectPower = power
    END SUB

    FUNCTION GetUseEffect() AS INTEGER
        GetUseEffect = useEffect
    END FUNCTION

    FUNCTION GetEffectPower() AS INTEGER
        GetEffectPower = effectPower
    END FUNCTION

    SUB Identify()
        isIdentified = 1
    END SUB

    FUNCTION IsIdentified() AS INTEGER
        IsIdentified = isIdentified
    END FUNCTION

    SUB SetCursed(val AS INTEGER)
        isCursed = val
    END SUB

    FUNCTION IsCursed() AS INTEGER
        IsCursed = isCursed
    END FUNCTION

    SUB Equip()
        isEquipped = 1
    END SUB

    SUB Unequip()
        isEquipped = 0
    END SUB

    FUNCTION IsEquipped() AS INTEGER
        IsEquipped = isEquipped
    END FUNCTION

    FUNCTION IsStackable() AS INTEGER
        IsStackable = isStackable
    END FUNCTION

    FUNCTION GetStack() AS INTEGER
        GetStack = stackCount
    END FUNCTION

    SUB AddStack(amount AS INTEGER)
        stackCount = stackCount + amount
    END SUB

    FUNCTION RemoveStack(amount AS INTEGER) AS INTEGER
        RemoveStack = 0
        IF stackCount >= amount THEN
            stackCount = stackCount - amount
            RemoveStack = 1
        END IF
    END FUNCTION

    FUNCTION GetValue() AS INTEGER
        GetValue = goldValue * stackCount
    END FUNCTION

    SUB SetValue(val AS INTEGER)
        goldValue = val
    END SUB
END CLASS

' ============================================================================
' INVENTORY COMPONENT
' ============================================================================
CLASS InventoryComponent
    DIM itemCount AS INTEGER
    DIM capacity AS INTEGER
    DIM items(49) AS Item       ' Max 50 items

    ' Equipped items by slot
    DIM equippedWeapon AS Item
    DIM equippedOffhand AS Item
    DIM equippedHead AS Item
    DIM equippedChest AS Item
    DIM equippedHands AS Item
    DIM equippedFeet AS Item
    DIM equippedRing1 AS Item
    DIM equippedRing2 AS Item
    DIM equippedAmulet AS Item

    DIM hasWeapon AS INTEGER
    DIM hasOffhand AS INTEGER
    DIM hasHead AS INTEGER
    DIM hasChest AS INTEGER
    DIM hasHands AS INTEGER
    DIM hasFeet AS INTEGER
    DIM hasRing1 AS INTEGER
    DIM hasRing2 AS INTEGER
    DIM hasAmulet AS INTEGER

    SUB Init(cap AS INTEGER)
        capacity = cap
        IF capacity > 50 THEN capacity = 50
        itemCount = 0
        hasWeapon = 0
        hasOffhand = 0
        hasHead = 0
        hasChest = 0
        hasHands = 0
        hasFeet = 0
        hasRing1 = 0
        hasRing2 = 0
        hasAmulet = 0
    END SUB

    FUNCTION GetCount() AS INTEGER
        GetCount = itemCount
    END FUNCTION

    FUNCTION GetCapacity() AS INTEGER
        GetCapacity = capacity
    END FUNCTION

    FUNCTION IsFull() AS INTEGER
        IsFull = 0
        IF itemCount >= capacity THEN IsFull = 1
    END FUNCTION

    FUNCTION AddItem(itm AS Item) AS INTEGER
        AddItem = 0
        IF itemCount >= capacity THEN EXIT FUNCTION

        ' Check for stacking
        IF itm.IsStackable() = 1 THEN
            DIM i AS INTEGER
            FOR i = 0 TO itemCount - 1
                IF items(i).GetId() = itm.GetId() THEN
                    items(i).AddStack(itm.GetStack())
                    AddItem = 1
                    EXIT FUNCTION
                END IF
            NEXT i
        END IF

        ' Add as new item
        items(itemCount) = itm
        itemCount = itemCount + 1
        AddItem = 1
    END FUNCTION

    FUNCTION GetItem(idx AS INTEGER) AS Item
        GetItem = items(idx)
    END FUNCTION

    SUB RemoveItem(idx AS INTEGER)
        IF idx < 0 THEN EXIT SUB
        IF idx >= itemCount THEN EXIT SUB

        DIM i AS INTEGER
        FOR i = idx TO itemCount - 2
            items(i) = items(i + 1)
        NEXT i
        itemCount = itemCount - 1
    END SUB

    FUNCTION Equip(idx AS INTEGER) AS INTEGER
        Equip = 0
        IF idx < 0 THEN EXIT FUNCTION
        IF idx >= itemCount THEN EXIT FUNCTION

        DIM itm AS Item
        itm = items(idx)
        DIM slot AS INTEGER
        slot = itm.GetSlot()

        IF slot < 0 THEN EXIT FUNCTION

        ' Unequip current item in slot first
        Me.UnequipSlot(slot)

        ' Equip new item
        itm.Equip()
        items(idx) = itm

        IF slot = SLOT_WEAPON THEN
            equippedWeapon = itm
            hasWeapon = 1
        END IF
        IF slot = SLOT_OFFHAND THEN
            equippedOffhand = itm
            hasOffhand = 1
        END IF
        IF slot = SLOT_HEAD THEN
            equippedHead = itm
            hasHead = 1
        END IF
        IF slot = SLOT_CHEST THEN
            equippedChest = itm
            hasChest = 1
        END IF
        IF slot = SLOT_HANDS THEN
            equippedHands = itm
            hasHands = 1
        END IF
        IF slot = SLOT_FEET THEN
            equippedFeet = itm
            hasFeet = 1
        END IF
        IF slot = SLOT_RING1 THEN
            equippedRing1 = itm
            hasRing1 = 1
        END IF
        IF slot = SLOT_RING2 THEN
            equippedRing2 = itm
            hasRing2 = 1
        END IF
        IF slot = SLOT_AMULET THEN
            equippedAmulet = itm
            hasAmulet = 1
        END IF

        Equip = 1
    END FUNCTION

    SUB UnequipSlot(slot AS INTEGER)
        IF slot = SLOT_WEAPON THEN
            IF hasWeapon = 1 THEN
                equippedWeapon.Unequip()
                hasWeapon = 0
            END IF
        END IF
        IF slot = SLOT_OFFHAND THEN
            IF hasOffhand = 1 THEN
                equippedOffhand.Unequip()
                hasOffhand = 0
            END IF
        END IF
        IF slot = SLOT_HEAD THEN
            IF hasHead = 1 THEN
                equippedHead.Unequip()
                hasHead = 0
            END IF
        END IF
        IF slot = SLOT_CHEST THEN
            IF hasChest = 1 THEN
                equippedChest.Unequip()
                hasChest = 0
            END IF
        END IF
        IF slot = SLOT_HANDS THEN
            IF hasHands = 1 THEN
                equippedHands.Unequip()
                hasHands = 0
            END IF
        END IF
        IF slot = SLOT_FEET THEN
            IF hasFeet = 1 THEN
                equippedFeet.Unequip()
                hasFeet = 0
            END IF
        END IF
        IF slot = SLOT_RING1 THEN
            IF hasRing1 = 1 THEN
                equippedRing1.Unequip()
                hasRing1 = 0
            END IF
        END IF
        IF slot = SLOT_RING2 THEN
            IF hasRing2 = 1 THEN
                equippedRing2.Unequip()
                hasRing2 = 0
            END IF
        END IF
        IF slot = SLOT_AMULET THEN
            IF hasAmulet = 1 THEN
                equippedAmulet.Unequip()
                hasAmulet = 0
            END IF
        END IF
    END SUB

    FUNCTION GetTotalArmor() AS INTEGER
        DIM total AS INTEGER
        total = 0
        IF hasHead = 1 THEN total = total + equippedHead.GetArmor()
        IF hasChest = 1 THEN total = total + equippedChest.GetArmor()
        IF hasHands = 1 THEN total = total + equippedHands.GetArmor()
        IF hasFeet = 1 THEN total = total + equippedFeet.GetArmor()
        IF hasOffhand = 1 THEN total = total + equippedOffhand.GetArmor()
        GetTotalArmor = total
    END FUNCTION

    FUNCTION GetWeaponDamageMin() AS INTEGER
        GetWeaponDamageMin = 1
        IF hasWeapon = 1 THEN GetWeaponDamageMin = equippedWeapon.GetDamageMin()
    END FUNCTION

    FUNCTION GetWeaponDamageMax() AS INTEGER
        GetWeaponDamageMax = 2
        IF hasWeapon = 1 THEN GetWeaponDamageMax = equippedWeapon.GetDamageMax()
    END FUNCTION

    FUNCTION GetStatBonus(stat AS INTEGER) AS INTEGER
        DIM total AS INTEGER
        total = 0
        IF hasWeapon = 1 THEN total = total + equippedWeapon.GetBonus(stat)
        IF hasOffhand = 1 THEN total = total + equippedOffhand.GetBonus(stat)
        IF hasHead = 1 THEN total = total + equippedHead.GetBonus(stat)
        IF hasChest = 1 THEN total = total + equippedChest.GetBonus(stat)
        IF hasHands = 1 THEN total = total + equippedHands.GetBonus(stat)
        IF hasFeet = 1 THEN total = total + equippedFeet.GetBonus(stat)
        IF hasRing1 = 1 THEN total = total + equippedRing1.GetBonus(stat)
        IF hasRing2 = 1 THEN total = total + equippedRing2.GetBonus(stat)
        IF hasAmulet = 1 THEN total = total + equippedAmulet.GetBonus(stat)
        GetStatBonus = total
    END FUNCTION

    FUNCTION HasSlotEquipped(slot AS INTEGER) AS INTEGER
        HasSlotEquipped = 0
        IF slot = SLOT_WEAPON THEN HasSlotEquipped = hasWeapon
        IF slot = SLOT_OFFHAND THEN HasSlotEquipped = hasOffhand
        IF slot = SLOT_HEAD THEN HasSlotEquipped = hasHead
        IF slot = SLOT_CHEST THEN HasSlotEquipped = hasChest
        IF slot = SLOT_HANDS THEN HasSlotEquipped = hasHands
        IF slot = SLOT_FEET THEN HasSlotEquipped = hasFeet
        IF slot = SLOT_RING1 THEN HasSlotEquipped = hasRing1
        IF slot = SLOT_RING2 THEN HasSlotEquipped = hasRing2
        IF slot = SLOT_AMULET THEN HasSlotEquipped = hasAmulet
    END FUNCTION
END CLASS

' ============================================================================
' ITEM FACTORY - Creates items with proper stats
' ============================================================================
CLASS ItemFactory
    DIM nextItemId AS INTEGER

    SUB Init()
        nextItemId = 1
    END SUB

    FUNCTION GetNextId() AS INTEGER
        DIM id AS INTEGER
        id = nextItemId
        nextItemId = nextItemId + 1
        GetNextId = id
    END FUNCTION

    FUNCTION CreateHealthPotion(power AS INTEGER) AS Item
        DIM itm AS Item
        itm = NEW Item()
        itm.Init(Me.GetNextId(), "Health Potion", ITEM_POTION)
        itm.SetConsumable(1, power)
        itm.SetValue(20 + power)
        CreateHealthPotion = itm
    END FUNCTION

    FUNCTION CreateManaPotion(power AS INTEGER) AS Item
        DIM itm AS Item
        itm = NEW Item()
        itm.Init(Me.GetNextId(), "Mana Potion", ITEM_POTION)
        itm.SetConsumable(2, power)
        itm.SetValue(25 + power)
        CreateManaPotion = itm
    END FUNCTION

    FUNCTION CreateFood() AS Item
        DIM itm AS Item
        itm = NEW Item()
        itm.Init(Me.GetNextId(), "Ration", ITEM_FOOD)
        itm.SetConsumable(3, 200)
        itm.SetValue(5)
        itm.Identify()
        CreateFood = itm
    END FUNCTION

    FUNCTION CreateGold(amount AS INTEGER) AS Item
        DIM itm AS Item
        itm = NEW Item()
        itm.Init(Me.GetNextId(), "Gold", ITEM_GOLD)
        itm.AddStack(amount - 1)
        itm.SetValue(1)
        CreateGold = itm
    END FUNCTION

    FUNCTION CreateWeapon(wpnType AS INTEGER, mat AS INTEGER, ench AS INTEGER) AS Item
        DIM itm AS Item
        itm = NEW Item()

        DIM nm AS STRING
        DIM dmin AS INTEGER
        DIM dmax AS INTEGER

        ' Base stats by weapon type
        IF wpnType = WPN_DAGGER THEN
            nm = "Dagger"
            dmin = 1
            dmax = 4
        END IF
        IF wpnType = WPN_SWORD THEN
            nm = "Sword"
            dmin = 2
            dmax = 8
        END IF
        IF wpnType = WPN_AXE THEN
            nm = "Axe"
            dmin = 3
            dmax = 10
        END IF
        IF wpnType = WPN_MACE THEN
            nm = "Mace"
            dmin = 2
            dmax = 7
        END IF
        IF wpnType = WPN_STAFF THEN
            nm = "Staff"
            dmin = 1
            dmax = 6
        END IF
        IF wpnType = WPN_BOW THEN
            nm = "Bow"
            dmin = 2
            dmax = 6
        END IF
        IF wpnType = WPN_SPEAR THEN
            nm = "Spear"
            dmin = 2
            dmax = 9
        END IF

        ' Add material prefix
        DIM matName AS STRING
        IF mat = MAT_IRON THEN matName = "Iron"
        IF mat = MAT_STEEL THEN matName = "Steel"
        IF mat = MAT_MITHRIL THEN matName = "Mithril"
        IF mat = MAT_ADAMANTINE THEN matName = "Adamantine"

        ' Add enchantment suffix
        DIM enchName AS STRING
        enchName = ""
        IF ench = ENCH_FLAMING THEN enchName = " of Flame"
        IF ench = ENCH_FREEZING THEN enchName = " of Frost"
        IF ench = ENCH_SHOCKING THEN enchName = " of Lightning"
        IF ench = ENCH_VAMPIRIC THEN enchName = " of Vampirism"
        IF ench = ENCH_VORPAL THEN enchName = " of Vorpal"
        IF ench = ENCH_SPEED THEN enchName = " of Speed"

        itm.Init(Me.GetNextId(), matName + " " + nm + enchName, ITEM_WEAPON)
        itm.SetEquipStats(SLOT_WEAPON, dmin, dmax, 0)
        itm.SetMaterial(mat)
        IF ench <> ENCH_NONE THEN itm.SetEnchantment(ench)
        itm.Identify()

        CreateWeapon = itm
    END FUNCTION

    FUNCTION CreateArmor(slot AS INTEGER, mat AS INTEGER) AS Item
        DIM itm AS Item
        itm = NEW Item()

        DIM nm AS STRING
        DIM armor AS INTEGER

        IF slot = SLOT_HEAD THEN
            nm = "Helm"
            armor = 2
        END IF
        IF slot = SLOT_CHEST THEN
            nm = "Chestplate"
            armor = 5
        END IF
        IF slot = SLOT_HANDS THEN
            nm = "Gauntlets"
            armor = 1
        END IF
        IF slot = SLOT_FEET THEN
            nm = "Boots"
            armor = 1
        END IF
        IF slot = SLOT_OFFHAND THEN
            nm = "Shield"
            armor = 3
        END IF

        DIM matName AS STRING
        IF mat = MAT_IRON THEN matName = "Iron"
        IF mat = MAT_STEEL THEN matName = "Steel"
        IF mat = MAT_MITHRIL THEN matName = "Mithril"
        IF mat = MAT_ADAMANTINE THEN matName = "Adamantine"

        itm.Init(Me.GetNextId(), matName + " " + nm, ITEM_ARMOR)
        itm.SetEquipStats(slot, 0, 0, armor)
        itm.SetMaterial(mat)
        itm.Identify()

        CreateArmor = itm
    END FUNCTION

    FUNCTION CreateKey() AS Item
        DIM itm AS Item
        itm = NEW Item()
        itm.Init(Me.GetNextId(), "Iron Key", ITEM_KEY)
        itm.SetValue(10)
        itm.Identify()
        CreateKey = itm
    END FUNCTION

    FUNCTION CreateRandomItem(floorLevel AS INTEGER) AS Item
        DIM roll AS INTEGER
        DIM itm AS Item

        roll = INT(RND() * 100)

        IF roll < 30 THEN
            ' Health potion
            DIM power AS INTEGER
            power = 20 + floorLevel * 5
            itm = Me.CreateHealthPotion(power)
        ELSEIF roll < 45 THEN
            ' Mana potion
            itm = Me.CreateManaPotion(15 + floorLevel * 3)
        ELSEIF roll < 55 THEN
            ' Food
            itm = Me.CreateFood()
        ELSEIF roll < 70 THEN
            ' Gold
            DIM amount AS INTEGER
            amount = 10 + INT(RND() * (floorLevel * 20))
            itm = Me.CreateGold(amount)
        ELSEIF roll < 85 THEN
            ' Weapon
            DIM wpnType AS INTEGER
            DIM mat AS INTEGER
            DIM ench AS INTEGER
            wpnType = 1 + INT(RND() * 7)
            mat = 1 + INT(floorLevel / 5)
            IF mat > 4 THEN mat = 4
            ench = ENCH_NONE
            IF RND() < 0.1 + (floorLevel * 0.02) THEN
                ench = 1 + INT(RND() * 6)
            END IF
            itm = Me.CreateWeapon(wpnType, mat, ench)
        ELSE
            ' Armor
            DIM slot AS INTEGER
            DIM armorMat AS INTEGER
            slot = SLOT_HEAD + INT(RND() * 5)
            IF slot > SLOT_OFFHAND THEN slot = SLOT_OFFHAND
            armorMat = 1 + INT(floorLevel / 5)
            IF armorMat > 4 THEN armorMat = 4
            itm = Me.CreateArmor(slot, armorMat)
        END IF

        CreateRandomItem = itm
    END FUNCTION
END CLASS
