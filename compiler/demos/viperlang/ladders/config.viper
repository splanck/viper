// config.viper - Game configuration constants for Ladders
// A Mr. Robot style platform/ladder climbing game
module config;

// Window dimensions
final SCREEN_WIDTH = 640;
final SCREEN_HEIGHT = 480;
final TITLE = "Viper Ladders";

// Tile/grid settings
final TILE_SIZE = 16;
final GRID_COLS = 40;   // 640 / 16
final GRID_ROWS = 30;   // 480 / 16

// Player settings
final PLAYER_WIDTH = 14;
final PLAYER_HEIGHT = 16;
final PLAYER_SPEED = 2;
final PLAYER_CLIMB_SPEED = 2;

// Enemy settings
final ENEMY_WIDTH = 14;
final ENEMY_HEIGHT = 16;
final ENEMY_SPEED = 1;
final ENEMY_CLIMB_SPEED = 1;
final MAX_ENEMIES = 5;

// Platform settings
final PLATFORM_HEIGHT = 4;

// Ladder settings
final LADDER_WIDTH = 12;

// Collectible settings
final COLLECTIBLE_SIZE = 10;
final SCORE_PER_COLLECTIBLE = 100;
final SCORE_PER_LEVEL = 500;

// Lives
final INITIAL_LIVES = 3;

// Frame timing
final TARGET_FPS = 60;
final FRAME_TIME_MS = 16;

// Key codes (from vgfx.h)
final KEY_ESCAPE = 256;
final KEY_ENTER = 257;
final KEY_LEFT = 258;
final KEY_RIGHT = 259;
final KEY_UP = 260;
final KEY_DOWN = 261;
final KEY_SPACE = 32;
final KEY_P = 80;    // 'P' for pause
final KEY_R = 82;    // 'R' for restart

// Game states
final STATE_MENU = 0;
final STATE_PLAYING = 1;
final STATE_PAUSED = 2;
final STATE_GAMEOVER = 3;
final STATE_LEVELCOMPLETE = 4;

// Direction constants
final DIR_NONE = 0;
final DIR_LEFT = 1;
final DIR_RIGHT = 2;
final DIR_UP = 3;
final DIR_DOWN = 4;

// Animation timing
final PLAYER_ANIM_DELAY = 100;
final ENEMY_ANIM_DELAY = 150;
