module Config;

// =============================================================================
// Frogger Configuration - All game constants in one place
// =============================================================================

// Display dimensions
final SCREEN_WIDTH = 80;
final SCREEN_HEIGHT = 24;
final GAME_WIDTH = 78;

// Row layout (top to bottom)
final TITLE_ROW = 1;
final HOME_ROW = 3;
final RIVER_START = 5;
final RIVER_END = 9;
final SAFE_ZONE_ROW = 11;
final ROAD_START = 13;
final ROAD_END = 17;
final START_ROW = 19;
final INSTRUCTIONS_ROW = 21;

// Game balance
final MAX_LIVES = 3;
final MAX_HOMES = 5;
final TIME_LIMIT = 600;

// Scoring
final SCORE_HOME = 200;
final SCORE_FORWARD = 10;
final SCORE_FLY_BONUS = 200;
final SCORE_TIME_BONUS = 10;

// Timing (frames)
final FRAME_DELAY = 80;
final FLY_DURATION = 150;
final INVINCIBLE_DURATION = 50;

// Entity counts
final NUM_CARS = 4;
final NUM_TRUCKS = 3;
final NUM_LOGS = 4;
final NUM_TURTLES = 3;

// Physics
final FROG_SPEED = 1;
final SLOW_SPEED = 1;
final MEDIUM_SPEED = 2;
final FAST_SPEED = 3;

// Key bindings
final KEY_UP = "w";
final KEY_UP_ALT = "W";
final KEY_DOWN = "s";
final KEY_DOWN_ALT = "S";
final KEY_LEFT = "a";
final KEY_LEFT_ALT = "A";
final KEY_RIGHT = "d";
final KEY_RIGHT_ALT = "D";
final KEY_PAUSE = "p";
final KEY_PAUSE_ALT = "P";
final KEY_QUIT = "q";
final KEY_QUIT_ALT = "Q";
