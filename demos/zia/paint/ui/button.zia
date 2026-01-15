// button.viper - Button UI control for Viper Paint
module button;

import "../config";

// Button entity - clickable button with text
entity Button {
    expose Integer x;           // X position
    expose Integer y;           // Y position
    expose Integer width;       // Button width
    expose Integer height;      // Button height
    expose String text;         // Button text
    expose Integer enabled;     // 1 = enabled, 0 = disabled
    expose Integer visible;     // 1 = visible, 0 = hidden
    expose Integer selected;    // 1 = selected/active (for tool buttons)

    hide Integer hovered;       // 1 if mouse is over button
    hide Integer pressed;       // 1 if mouse is pressed on button
    hide Integer wasClicked;    // 1 if button was clicked this frame

    // Default constructor
    expose func init() {
        x = 0;
        y = 0;
        width = 0;
        height = 0;
        text = "";
        enabled = 1;
        visible = 1;
        selected = 0;
        hovered = 0;
        pressed = 0;
        wasClicked = 0;
    }

    expose func setup(bx: Integer, by: Integer, bw: Integer, bh: Integer, txt: String) {
        x = bx;
        y = by;
        width = bw;
        height = bh;
        text = txt;
        enabled = 1;
        visible = 1;
        selected = 0;
        hovered = 0;
        pressed = 0;
        wasClicked = 0;
    }

    // Reset click state (call at start of frame)
    expose func resetClick() {
        wasClicked = 0;
    }

    // Check if point is inside button
    expose func containsPoint(px: Integer, py: Integer) -> Integer {
        if visible == 0 {
            return 0;
        }
        if px >= x and px < x + width and py >= y and py < y + height {
            return 1;
        }
        return 0;
    }

    // Handle mouse input, returns 1 if button was clicked
    expose func handleMouse(mx: Integer, my: Integer, mouseDown: Integer) -> Integer {
        if visible == 0 or enabled == 0 {
            hovered = 0;
            pressed = 0;
            return 0;
        }

        var inside = containsPoint(mx, my);
        hovered = inside;

        if inside == 1 and mouseDown == 1 {
            pressed = 1;
        } else if pressed == 1 and mouseDown == 0 {
            // Mouse released
            if inside == 1 {
                wasClicked = 1;
                pressed = 0;
                return 1;  // Clicked!
            }
            pressed = 0;
        } else if mouseDown == 0 {
            pressed = 0;
        }

        return 0;
    }

    // Check if button was clicked this frame
    expose func clicked() -> Integer {
        return wasClicked;
    }

    // Draw the button
    expose func draw(gfx: Viper.Graphics.Canvas) {
        if visible == 0 {
            return;
        }

        // Determine background color
        var bgColor = config.UI_BG_BUTTON;
        if enabled == 0 {
            bgColor = config.UI_BG_DARK;
        } else if selected == 1 {
            bgColor = config.UI_BG_BUTTON_ACTIVE;
        } else if pressed == 1 {
            bgColor = config.UI_BG_BUTTON_ACTIVE;
        } else if hovered == 1 {
            bgColor = config.UI_BG_BUTTON_HOVER;
        }

        // Draw button background
        gfx.Box( x, y, width, height, bgColor);

        // Draw border
        gfx.Frame( x, y, width, height, config.UI_BORDER);

        // Draw text centered
        var textColor = config.UI_TEXT;
        if enabled == 0 {
            textColor = config.UI_TEXT_DIM;
        }

        // Simple centering (approximate - 8px per char)
        var textLen = 1;  // Assume single character for tool buttons
        var textX = x + (width - textLen * 8) / 2;
        var textY = y + (height - 8) / 2;
        gfx.Text( textX, textY, text, textColor);
    }
}

// ToolButton - specialized button for tool selection
entity ToolButton {
    expose Integer x;
    expose Integer y;
    expose Integer size;        // Button is square
    expose String icon;         // Single character icon
    expose Integer toolId;      // Tool identifier
    expose Integer selected;    // 1 if this tool is active
    expose Integer enabled;

    hide Integer hovered;
    hide Integer pressed;
    hide Integer wasClicked;

    // Default constructor
    expose func init() {
        x = 0;
        y = 0;
        size = 0;
        icon = "";
        toolId = 0;
        selected = 0;
        enabled = 1;
        hovered = 0;
        pressed = 0;
        wasClicked = 0;
    }

    expose func setup(bx: Integer, by: Integer, sz: Integer, ic: String, tid: Integer) {
        x = bx;
        y = by;
        size = sz;
        icon = ic;
        toolId = tid;
        selected = 0;
        enabled = 1;
        hovered = 0;
        pressed = 0;
        wasClicked = 0;
    }

    expose func resetClick() {
        wasClicked = 0;
    }

    expose func containsPoint(px: Integer, py: Integer) -> Integer {
        if px >= x and px < x + size and py >= y and py < y + size {
            return 1;
        }
        return 0;
    }

    expose func handleMouse(mx: Integer, my: Integer, mouseDown: Integer) -> Integer {
        if enabled == 0 {
            hovered = 0;
            pressed = 0;
            return 0;
        }

        var inside = containsPoint(mx, my);
        hovered = inside;

        if inside == 1 and mouseDown == 1 {
            pressed = 1;
        } else if pressed == 1 and mouseDown == 0 {
            if inside == 1 {
                wasClicked = 1;
                pressed = 0;
                return 1;
            }
            pressed = 0;
        } else if mouseDown == 0 {
            pressed = 0;
        }

        return 0;
    }

    expose func clicked() -> Integer {
        return wasClicked;
    }

    expose func draw(gfx: Viper.Graphics.Canvas) {
        var bgColor = config.UI_BG_BUTTON;
        if selected == 1 {
            bgColor = config.UI_BG_BUTTON_ACTIVE;
        } else if pressed == 1 {
            bgColor = config.UI_BG_BUTTON_ACTIVE;
        } else if hovered == 1 {
            bgColor = config.UI_BG_BUTTON_HOVER;
        }

        gfx.Box( x, y, size, size, bgColor);
        gfx.Frame( x, y, size, size, config.UI_BORDER);

        // Draw icon centered
        var textX = x + (size - 8) / 2;
        var textY = y + (size - 8) / 2;
        gfx.Text( textX, textY, icon, config.UI_TEXT);
    }
}
