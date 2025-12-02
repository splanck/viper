' constants.bas - Game constants for Roguelike RPG
' All game-wide constants defined here

' ============================================================================
' DISPLAY CONSTANTS
' ============================================================================
CONST SCREEN_WIDTH AS INTEGER = 80
CONST SCREEN_HEIGHT AS INTEGER = 25
CONST MAP_VIEW_WIDTH AS INTEGER = 60
CONST MAP_VIEW_HEIGHT AS INTEGER = 18
CONST MAP_VIEW_X AS INTEGER = 1
CONST MAP_VIEW_Y AS INTEGER = 1
CONST STATUS_Y AS INTEGER = 20
CONST MSG_Y AS INTEGER = 22
CONST MSG_COUNT AS INTEGER = 3

' ============================================================================
' MAP CONSTANTS
' ============================================================================
CONST MAP_WIDTH AS INTEGER = 60
CONST MAP_HEIGHT AS INTEGER = 40
CONST MAX_ROOMS AS INTEGER = 15
CONST MIN_ROOM_SIZE AS INTEGER = 5
CONST MAX_ROOM_SIZE AS INTEGER = 12
CONST MAX_FLOORS AS INTEGER = 20

' ============================================================================
' TILE TYPES
' ============================================================================
CONST TILE_VOID AS INTEGER = 0
CONST TILE_FLOOR AS INTEGER = 1
CONST TILE_WALL AS INTEGER = 2
CONST TILE_DOOR_CLOSED AS INTEGER = 3
CONST TILE_DOOR_OPEN AS INTEGER = 4
CONST TILE_DOOR_LOCKED AS INTEGER = 5
CONST TILE_STAIRS_DOWN AS INTEGER = 6
CONST TILE_STAIRS_UP AS INTEGER = 7
CONST TILE_WATER AS INTEGER = 8
CONST TILE_LAVA AS INTEGER = 9
CONST TILE_TRAP_HIDDEN AS INTEGER = 10
CONST TILE_TRAP_VISIBLE AS INTEGER = 11
CONST TILE_SECRET_WALL AS INTEGER = 12
CONST TILE_TORCH AS INTEGER = 13
CONST TILE_SHRINE AS INTEGER = 14
CONST TILE_CHEST AS INTEGER = 15

' ============================================================================
' ENTITY TYPES
' ============================================================================
CONST ENT_NONE AS INTEGER = 0
CONST ENT_PLAYER AS INTEGER = 1
CONST ENT_MONSTER AS INTEGER = 2
CONST ENT_ITEM AS INTEGER = 3
CONST ENT_PROJECTILE AS INTEGER = 4

' ============================================================================
' MONSTER IDS (30+ types)
' ============================================================================
' Basic (Floors 1-5)
CONST MON_RAT AS INTEGER = 1
CONST MON_BAT AS INTEGER = 2
CONST MON_GOBLIN AS INTEGER = 3
CONST MON_SKELETON AS INTEGER = 4
CONST MON_ZOMBIE AS INTEGER = 5
CONST MON_SNAKE AS INTEGER = 6
CONST MON_KOBOLD AS INTEGER = 7

' Intermediate (Floors 6-10)
CONST MON_ORC AS INTEGER = 10
CONST MON_TROLL AS INTEGER = 11
CONST MON_GHOST AS INTEGER = 12
CONST MON_SPIDER AS INTEGER = 13
CONST MON_MUMMY AS INTEGER = 14
CONST MON_OGRE AS INTEGER = 15
CONST MON_HARPY AS INTEGER = 16

' Advanced (Floors 11-15)
CONST MON_VAMPIRE AS INTEGER = 20
CONST MON_WEREWOLF AS INTEGER = 21
CONST MON_DEMON AS INTEGER = 22
CONST MON_GOLEM AS INTEGER = 23
CONST MON_LICH AS INTEGER = 24
CONST MON_WRAITH AS INTEGER = 25
CONST MON_MINOTAUR AS INTEGER = 26

' Elite (Floors 16-19)
CONST MON_DRAGON AS INTEGER = 30
CONST MON_ARCHDEMON AS INTEGER = 31
CONST MON_DEATH_KNIGHT AS INTEGER = 32
CONST MON_BEHOLDER AS INTEGER = 33
CONST MON_HYDRA AS INTEGER = 34

' Bosses
CONST MON_GOBLIN_KING AS INTEGER = 50
CONST MON_VAMPIRE_LORD AS INTEGER = 51
CONST MON_ANCIENT_LICH AS INTEGER = 52
CONST MON_DEMON_PRINCE AS INTEGER = 53

' ============================================================================
' AI BEHAVIORS
' ============================================================================
CONST AI_IDLE AS INTEGER = 0
CONST AI_PATROL AS INTEGER = 1
CONST AI_AGGRESSIVE AS INTEGER = 2
CONST AI_COWARD AS INTEGER = 3
CONST AI_RANGED AS INTEGER = 4
CONST AI_PACK AS INTEGER = 5

' AI States
CONST STATE_IDLE AS INTEGER = 0
CONST STATE_HUNTING AS INTEGER = 1
CONST STATE_FLEEING AS INTEGER = 2
CONST STATE_ATTACKING AS INTEGER = 3

' ============================================================================
' CHARACTER CLASSES
' ============================================================================
CONST CLASS_WARRIOR AS INTEGER = 1
CONST CLASS_MAGE AS INTEGER = 2
CONST CLASS_ROGUE AS INTEGER = 3
CONST CLASS_RANGER AS INTEGER = 4

' ============================================================================
' STATS
' ============================================================================
CONST STAT_STR AS INTEGER = 0
CONST STAT_DEX AS INTEGER = 1
CONST STAT_CON AS INTEGER = 2
CONST STAT_INT AS INTEGER = 3
CONST STAT_WIS AS INTEGER = 4
CONST STAT_CHA AS INTEGER = 5
CONST NUM_STATS AS INTEGER = 6

' ============================================================================
' EQUIPMENT SLOTS
' ============================================================================
CONST SLOT_WEAPON AS INTEGER = 0
CONST SLOT_OFFHAND AS INTEGER = 1
CONST SLOT_HEAD AS INTEGER = 2
CONST SLOT_CHEST AS INTEGER = 3
CONST SLOT_HANDS AS INTEGER = 4
CONST SLOT_FEET AS INTEGER = 5
CONST SLOT_RING1 AS INTEGER = 6
CONST SLOT_RING2 AS INTEGER = 7
CONST SLOT_AMULET AS INTEGER = 8
CONST NUM_SLOTS AS INTEGER = 9

' ============================================================================
' ITEM TYPES
' ============================================================================
CONST ITEM_WEAPON AS INTEGER = 1
CONST ITEM_ARMOR AS INTEGER = 2
CONST ITEM_POTION AS INTEGER = 3
CONST ITEM_SCROLL AS INTEGER = 4
CONST ITEM_FOOD AS INTEGER = 5
CONST ITEM_KEY AS INTEGER = 6
CONST ITEM_GOLD AS INTEGER = 7
CONST ITEM_RING AS INTEGER = 8
CONST ITEM_AMULET AS INTEGER = 9
CONST ITEM_ARTIFACT AS INTEGER = 10

' ============================================================================
' WEAPON TYPES
' ============================================================================
CONST WPN_DAGGER AS INTEGER = 1
CONST WPN_SWORD AS INTEGER = 2
CONST WPN_AXE AS INTEGER = 3
CONST WPN_MACE AS INTEGER = 4
CONST WPN_STAFF AS INTEGER = 5
CONST WPN_BOW AS INTEGER = 6
CONST WPN_SPEAR AS INTEGER = 7

' ============================================================================
' MATERIAL TIERS
' ============================================================================
CONST MAT_IRON AS INTEGER = 1
CONST MAT_STEEL AS INTEGER = 2
CONST MAT_MITHRIL AS INTEGER = 3
CONST MAT_ADAMANTINE AS INTEGER = 4

' ============================================================================
' ENCHANTMENTS
' ============================================================================
CONST ENCH_NONE AS INTEGER = 0
CONST ENCH_FLAMING AS INTEGER = 1
CONST ENCH_FREEZING AS INTEGER = 2
CONST ENCH_SHOCKING AS INTEGER = 3
CONST ENCH_VAMPIRIC AS INTEGER = 4
CONST ENCH_VORPAL AS INTEGER = 5
CONST ENCH_SPEED AS INTEGER = 6
CONST ENCH_PROTECTION AS INTEGER = 7

' ============================================================================
' DAMAGE TYPES
' ============================================================================
CONST DMG_PHYSICAL AS INTEGER = 0
CONST DMG_FIRE AS INTEGER = 1
CONST DMG_ICE AS INTEGER = 2
CONST DMG_LIGHTNING AS INTEGER = 3
CONST DMG_POISON AS INTEGER = 4
CONST DMG_HOLY AS INTEGER = 5
CONST DMG_DARK AS INTEGER = 6

' ============================================================================
' STATUS EFFECTS
' ============================================================================
CONST STATUS_NONE AS INTEGER = 0
CONST STATUS_POISONED AS INTEGER = 1
CONST STATUS_BURNING AS INTEGER = 2
CONST STATUS_FROZEN AS INTEGER = 3
CONST STATUS_STUNNED AS INTEGER = 4
CONST STATUS_BLIND AS INTEGER = 5
CONST STATUS_INVISIBLE AS INTEGER = 6
CONST STATUS_HASTE AS INTEGER = 7
CONST STATUS_SLOW AS INTEGER = 8
CONST STATUS_REGEN AS INTEGER = 9
CONST STATUS_BERSERK AS INTEGER = 10

' ============================================================================
' SPELL SCHOOLS
' ============================================================================
CONST SCHOOL_FIRE AS INTEGER = 1
CONST SCHOOL_ICE AS INTEGER = 2
CONST SCHOOL_LIGHTNING AS INTEGER = 3
CONST SCHOOL_DEATH AS INTEGER = 4
CONST SCHOOL_LIFE AS INTEGER = 5
CONST SCHOOL_FORCE AS INTEGER = 6
CONST SCHOOL_ILLUSION AS INTEGER = 7
CONST SCHOOL_SUMMONING AS INTEGER = 8

' ============================================================================
' COLORS (Terminal colors)
' ============================================================================
CONST CLR_BLACK AS INTEGER = 0
CONST CLR_RED AS INTEGER = 1
CONST CLR_GREEN AS INTEGER = 2
CONST CLR_YELLOW AS INTEGER = 3
CONST CLR_BLUE AS INTEGER = 4
CONST CLR_MAGENTA AS INTEGER = 5
CONST CLR_CYAN AS INTEGER = 6
CONST CLR_WHITE AS INTEGER = 7
CONST CLR_BRIGHT_BLACK AS INTEGER = 8
CONST CLR_BRIGHT_RED AS INTEGER = 9
CONST CLR_BRIGHT_GREEN AS INTEGER = 10
CONST CLR_BRIGHT_YELLOW AS INTEGER = 11
CONST CLR_BRIGHT_BLUE AS INTEGER = 12
CONST CLR_BRIGHT_MAGENTA AS INTEGER = 13
CONST CLR_BRIGHT_CYAN AS INTEGER = 14
CONST CLR_BRIGHT_WHITE AS INTEGER = 15

' ============================================================================
' GAME STATES
' ============================================================================
CONST GAME_MENU AS INTEGER = 0
CONST GAME_PLAYING AS INTEGER = 1
CONST GAME_INVENTORY AS INTEGER = 2
CONST GAME_CHARACTER AS INTEGER = 3
CONST GAME_DEAD AS INTEGER = 4
CONST GAME_WON AS INTEGER = 5
CONST GAME_TARGETING AS INTEGER = 6

' ============================================================================
' HUNGER LEVELS
' ============================================================================
CONST HUNGER_MAX AS INTEGER = 1000
CONST HUNGER_FULL AS INTEGER = 800
CONST HUNGER_NORMAL AS INTEGER = 400
CONST HUNGER_HUNGRY AS INTEGER = 200
CONST HUNGER_STARVING AS INTEGER = 50

' ============================================================================
' FOV CONSTANTS
' ============================================================================
CONST FOV_RADIUS_BASE AS INTEGER = 8
CONST FOV_RADIUS_TORCH AS INTEGER = 5

' ============================================================================
' EXPERIENCE TABLE
' ============================================================================
CONST XP_BASE AS INTEGER = 100
CONST XP_FACTOR AS INTEGER = 150
CONST MAX_LEVEL AS INTEGER = 30

' ============================================================================
' DIRECTIONS
' ============================================================================
CONST DIR_NONE AS INTEGER = 0
CONST DIR_N AS INTEGER = 1
CONST DIR_NE AS INTEGER = 2
CONST DIR_E AS INTEGER = 3
CONST DIR_SE AS INTEGER = 4
CONST DIR_S AS INTEGER = 5
CONST DIR_SW AS INTEGER = 6
CONST DIR_W AS INTEGER = 7
CONST DIR_NW AS INTEGER = 8

' Direction deltas (arrays not supported as CONST, so use helper)
