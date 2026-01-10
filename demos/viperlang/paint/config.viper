module config;

// ============================================================================
// VIPER PAINT - Configuration and Constants
// ============================================================================

// Window dimensions
final WINDOW_WIDTH = 1200;
final WINDOW_HEIGHT = 800;
final WINDOW_TITLE = "Viper Paint";

// Default canvas dimensions
final DEFAULT_CANVAS_WIDTH = 1024;
final DEFAULT_CANVAS_HEIGHT = 768;

// UI Panel dimensions
final TOOLBAR_HEIGHT = 40;
final TOOL_PANEL_WIDTH = 52;
final COLOR_PANEL_WIDTH = 140;
final STATUS_BAR_HEIGHT = 24;
final BRUSH_PANEL_HEIGHT = 80;

// Tool button size
final TOOL_BUTTON_SIZE = 40;
final TOOL_BUTTON_MARGIN = 6;

// Color palette
final PALETTE_CELL_SIZE = 24;
final PALETTE_MARGIN = 4;

// Brush limits
final MIN_BRUSH_SIZE = 1;
final MAX_BRUSH_SIZE = 64;
final DEFAULT_BRUSH_SIZE = 8;
final MIN_OPACITY = 0;
final MAX_OPACITY = 100;
final DEFAULT_OPACITY = 100;

// History
final MAX_UNDO_LEVELS = 20;

// Colors (ARGB format)
final COLOR_BLACK = 0xFF000000;
final COLOR_WHITE = 0xFFFFFFFF;
final COLOR_RED = 0xFFFF0000;
final COLOR_GREEN = 0xFF00FF00;
final COLOR_BLUE = 0xFF0000FF;
final COLOR_YELLOW = 0xFFFFFF00;
final COLOR_CYAN = 0xFF00FFFF;
final COLOR_MAGENTA = 0xFFFF00FF;
final COLOR_GRAY = 0xFF808080;
final COLOR_LIGHT_GRAY = 0xFFC0C0C0;
final COLOR_DARK_GRAY = 0xFF404040;

// UI Colors
final UI_BG_DARK = 0xFF2D2D2D;
final UI_BG_PANEL = 0xFF383838;
final UI_BG_BUTTON = 0xFF4A4A4A;
final UI_BG_BUTTON_HOVER = 0xFF5A5A5A;
final UI_BG_BUTTON_ACTIVE = 0xFF3080D0;
final UI_BORDER = 0xFF505050;
final UI_TEXT = 0xFFE0E0E0;
final UI_TEXT_DIM = 0xFF909090;

// Tool IDs
final TOOL_PENCIL = 0;
final TOOL_BRUSH = 1;
final TOOL_ERASER = 2;
final TOOL_LINE = 3;
final TOOL_RECTANGLE = 4;
final TOOL_ELLIPSE = 5;
final TOOL_FILL = 6;
final TOOL_EYEDROPPER = 7;
final TOOL_SELECT = 8;

// Keyboard shortcuts (ASCII codes)
final KEY_P = 80;  // Pencil
final KEY_B = 66;  // Brush
final KEY_E = 69;  // Eraser
final KEY_L = 76;  // Line
final KEY_R = 82;  // Rectangle
final KEY_O = 79;  // Ellipse (O for oval)
final KEY_F = 70;  // Fill
final KEY_I = 73;  // eyedropper (I for pick)
final KEY_S = 83;  // Select
final KEY_X = 88;  // Swap colors
final KEY_Z = 90;  // Undo (with Ctrl)
final KEY_Y = 89;  // Redo (with Ctrl)
final KEY_ESCAPE = 256;
final KEY_DELETE = 261;
final KEY_LEFT_BRACKET = 91;   // Decrease brush size
final KEY_RIGHT_BRACKET = 93;  // Increase brush size
