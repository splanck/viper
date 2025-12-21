module Config;

// Display dimensions
Integer SCREEN_WIDTH = 80;
Integer SCREEN_HEIGHT = 24;
Integer GAME_WIDTH = 70;

// Row positions (from top)
Integer TITLE_ROW = 1;
Integer HOME_ROW = 2;
Integer RIVER_START = 4;
Integer RIVER_END = 8;
Integer SAFE_ZONE_ROW = 10;
Integer ROAD_START = 12;
Integer ROAD_END = 16;
Integer START_ROW = 18;
Integer INSTRUCTIONS_ROW = 20;

// Game settings
Integer MAX_LIVES = 3;
Integer MAX_HOMES = 5;
Integer MAX_VEHICLES = 10;
Integer MAX_PLATFORMS = 10;
Integer MAX_POWERUPS = 3;

// Scoring
Integer SCORE_HOME = 200;
Integer SCORE_FORWARD = 10;
Integer SCORE_POWERUP = 50;
Integer SCORE_TIME_BONUS = 100;

// Timing (milliseconds)
Integer FRAME_DELAY = 100;
Integer POWERUP_DURATION = 5000;
Integer INVINCIBLE_FRAMES = 30;

// Power-up types
Integer POWERUP_NONE = 0;
Integer POWERUP_SPEED = 1;
Integer POWERUP_INVINCIBLE = 2;
Integer POWERUP_EXTRA_LIFE = 3;
