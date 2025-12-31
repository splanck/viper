// config.viper - Game configuration constants for Centipede
module config;

// Window dimensions
final SCREEN_WIDTH = 480;
final SCREEN_HEIGHT = 640;
final TITLE = "Viper Centipede";

// Grid settings (classic arcade had 30 columns x 31 rows visible)
final CELL_SIZE = 16;
final GRID_COLS = 30;
final GRID_ROWS = 40;

// Player area (bottom 6 rows)
final PLAYER_ZONE_TOP = 544;  // GRID_ROWS - 6 = 34, 34 * 16 = 544

// Player settings
final PLAYER_WIDTH = 12;
final PLAYER_HEIGHT = 14;
final PLAYER_SPEED = 8;

// Bullet settings
final BULLET_WIDTH = 2;
final BULLET_HEIGHT = 8;
final BULLET_SPEED = 80;
final MAX_BULLETS = 1;  // Classic centipede only allows one bullet at a time

// Centipede settings
final CENTIPEDE_INITIAL_LENGTH = 12;
final CENTIPEDE_SPEED = 2;
final SEGMENT_SIZE = 14;

// Mushroom settings
final MUSHROOM_SIZE = 14;
final MUSHROOM_HITS = 4;  // Hits to destroy
final MUSHROOM_DENSITY = 8;  // % chance to spawn in each cell (reduced from 15)

// Spider settings
final SPIDER_SIZE = 14;
final SPIDER_SPEED = 3;
final SPIDER_SPAWN_INTERVAL = 500;  // Frames between spawns

// Flea settings
final FLEA_SIZE = 12;
final FLEA_SPEED = 4;
final FLEA_SPAWN_THRESHOLD = 5;  // Spawns when mushrooms in player zone < this

// Scorpion settings
final SCORPION_SIZE = 14;
final SCORPION_SPEED = 2;

// Scoring
final SCORE_CENTIPEDE_HEAD = 100;
final SCORE_CENTIPEDE_BODY = 10;
final SCORE_SPIDER_CLOSE = 900;
final SCORE_SPIDER_MED = 600;
final SCORE_SPIDER_FAR = 300;
final SCORE_FLEA = 200;
final SCORE_SCORPION = 1000;
final SCORE_MUSHROOM = 1;
final SCORE_BONUS_LIFE = 12000;

// Lives
final INITIAL_LIVES = 3;

// Frame timing
final TARGET_FPS = 60;
final FRAME_TIME_MS = 16;  // ~60fps
