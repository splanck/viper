# ViperLang Demos

This directory contains demo applications written in ViperLang.

## Running Demos

```bash
# From the viper root directory
./build/src/tools/viperlang/viperlang demos/viperlang/<demo>.viper
```

## Available Demos

### hello.viper
Simple "Hello World" demo. Tests basic output.

### bounce.viper
Bouncing ball animation. Demonstrates:
- Terminal graphics (colors, positioning)
- Animation loop with timing
- Keyboard input with timeout

### countdown.viper
Countdown timer with "BLAST OFF!" finale. Demonstrates:
- Terminal clearing and positioning
- Color changes
- Sleep/timing functions

### colortest.viper
Display all terminal colors (0-15). Demonstrates:
- Full color palette
- Text formatting
- Grid layout

### reaction.viper
Reaction time game. Press a key when the screen turns green. Demonstrates:
- User input handling
- Timing with Clock.Millis
- Conditional logic and game states

### snake.viper
Simple snake game with WASD controls. Demonstrates:
- Game loop pattern
- Multiple moving objects
- Collision detection
- Score tracking

### pong.viper
One-player pong game. Demonstrates:
- Ball physics (bouncing)
- Paddle control
- Lives system
- Game over state

## Controls

Most demos use:
- **Q** - Quit
- **W/A/S/D** - Movement (where applicable)
- **SPACE** or any key - Continue/Select

## Features Used

These demos showcase:
- Terminal I/O (Say, Print, GetKey, InKey)
- Screen control (Clear, SetPosition, SetColor)
- Timing (SleepMs, Clock.Millis)
- Input handling (GetKeyTimeout for non-blocking input)
- Control flow (if, while, for)
- Variables and arithmetic
- String comparison
