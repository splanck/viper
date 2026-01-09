# Chapter 21: Building a Game

This is where everything comes together. We'll build a complete game from scratch — Frogger, the classic arcade game where you guide a frog across a busy road and a dangerous river.

This isn't a toy example. By the end, you'll have a playable game with multiple lives, scoring, progressive difficulty, and polish. More importantly, you'll understand how games are structured.

---

## Game Overview

In Frogger:
- A frog starts at the bottom of the screen
- The player must guide it to safety zones at the top
- The lower half is a road with cars and trucks
- The upper half is a river with logs and turtles
- Getting hit by a car = death
- Falling in the river = death
- Landing on a log = ride across the river
- Reaching a home slot = score points
- Fill all five home slots = next level

---

## Project Structure

We'll organize the code into modules:

```
frogger/
├── main.viper        # Entry point and game loop
├── config.viper      # Constants and settings
├── game.viper        # Game state and logic
├── frog.viper        # Player character
├── lane.viper        # Roads and rivers
├── vehicle.viper     # Cars and trucks
├── platform.viper    # Logs and turtles
├── renderer.viper    # Drawing code
└── input.viper       # Input handling
```

Let's build it step by step.

---

## Step 1: Configuration

First, define constants:

```viper
// config.viper
module Config;

// Screen dimensions
pub final SCREEN_WIDTH = 800;
pub final SCREEN_HEIGHT = 600;

// Grid layout
pub final TILE_SIZE = 40;
pub final GRID_WIDTH = 20;   // 800 / 40
pub final GRID_HEIGHT = 15;  // 600 / 40

// Game zones
pub final HOME_ROW = 0;
pub final RIVER_START = 1;
pub final RIVER_END = 6;
pub final SAFE_ZONE = 7;
pub final ROAD_START = 8;
pub final ROAD_END = 13;
pub final START_ROW = 14;

// Timing
pub final FROG_MOVE_COOLDOWN = 0.15;

// Scoring
pub final SCORE_PER_STEP = 10;
pub final SCORE_PER_HOME = 200;
pub final SCORE_PER_LEVEL = 1000;

// Lives
pub final STARTING_LIVES = 3;
```

---

## Step 2: The Frog (Player)

```viper
// frog.viper
module Frog;

import Config;

expose value Frog {
    x: f64;
    y: f64;
    gridX: i64;
    gridY: i64;
    ridingPlatform: ?Platform;
    alive: bool;
    moveCooldown: f64;
}

pub func create() -> Frog {
    return spawn();
}

pub func spawn() -> Frog {
    return Frog {
        x: Config.SCREEN_WIDTH / 2.0,
        y: Config.START_ROW * Config.TILE_SIZE,
        gridX: Config.GRID_WIDTH / 2,
        gridY: Config.START_ROW,
        ridingPlatform: null,
        alive: true,
        moveCooldown: 0.0
    };
}

pub func update(frog: Frog, dt: f64) -> Frog {
    var f = frog;

    // Reduce cooldown
    if f.moveCooldown > 0 {
        f.moveCooldown -= dt;
    }

    // If riding a platform, move with it
    if f.ridingPlatform != null {
        f.x += f.ridingPlatform.speed * dt;

        // Fell off the screen?
        if f.x < 0 || f.x > Config.SCREEN_WIDTH {
            f.alive = false;
        }
    }

    return f;
}

pub func canMove(frog: Frog) -> bool {
    return frog.moveCooldown <= 0;
}

pub func move(frog: Frog, dx: i64, dy: i64) -> Frog {
    var f = frog;

    var newX = f.gridX + dx;
    var newY = f.gridY + dy;

    // Bounds checking
    if newX < 0 || newX >= Config.GRID_WIDTH {
        return f;
    }
    if newY < 0 || newY >= Config.GRID_HEIGHT {
        return f;
    }

    f.gridX = newX;
    f.gridY = newY;
    f.x = newX * Config.TILE_SIZE + Config.TILE_SIZE / 2.0;
    f.y = newY * Config.TILE_SIZE;
    f.moveCooldown = Config.FROG_MOVE_COOLDOWN;
    f.ridingPlatform = null;

    return f;
}

pub func die(frog: Frog) -> Frog {
    var f = frog;
    f.alive = false;
    return f;
}
```

---

## Step 3: Vehicles (Road Hazards)

```viper
// vehicle.viper
module Vehicle;

import Config;

expose value Vehicle {
    x: f64;
    y: f64;
    width: f64;
    speed: f64;
    color: Color;
}

pub func create(y: i64, speed: f64, width: f64, color: Color) -> Vehicle {
    var startX = 0.0;
    if speed < 0 {
        startX = Config.SCREEN_WIDTH;
    }

    return Vehicle {
        x: startX,
        y: y * Config.TILE_SIZE,
        width: width,
        speed: speed,
        color: color
    };
}

pub func update(vehicle: Vehicle, dt: f64) -> Vehicle {
    var v = vehicle;
    v.x += v.speed * dt;

    // Wrap around
    if v.speed > 0 && v.x > Config.SCREEN_WIDTH + v.width {
        v.x = -v.width;
    }
    if v.speed < 0 && v.x < -v.width {
        v.x = Config.SCREEN_WIDTH;
    }

    return v;
}

pub func getBounds(vehicle: Vehicle) -> Rect {
    return Rect {
        x: v.x,
        y: v.y,
        width: v.width,
        height: Config.TILE_SIZE - 4
    };
}

pub func hitsPoint(v: Vehicle, px: f64, py: f64) -> bool {
    var bounds = getBounds(v);
    return px >= bounds.x && px < bounds.x + bounds.width &&
           py >= bounds.y && py < bounds.y + bounds.height;
}
```

---

## Step 4: Platforms (River Objects)

```viper
// platform.viper
module Platform;

import Config;

expose value Platform {
    x: f64;
    y: f64;
    width: f64;
    speed: f64;
    color: Color;
}

pub func create(y: i64, speed: f64, width: f64, color: Color) -> Platform {
    var startX = 0.0;
    if speed < 0 {
        startX = Config.SCREEN_WIDTH;
    }

    return Platform {
        x: startX,
        y: y * Config.TILE_SIZE,
        width: width,
        speed: speed,
        color: color
    };
}

pub func update(platform: Platform, dt: f64) -> Platform {
    var p = platform;
    p.x += p.speed * dt;

    // Wrap around
    if p.speed > 0 && p.x > Config.SCREEN_WIDTH + p.width {
        p.x = -p.width;
    }
    if p.speed < 0 && p.x < -p.width {
        p.x = Config.SCREEN_WIDTH;
    }

    return p;
}

pub func containsPoint(p: Platform, px: f64, py: f64) -> bool {
    return px >= p.x && px < p.x + p.width &&
           py >= p.y && py < p.y + Config.TILE_SIZE;
}
```

---

## Step 5: Game State

```viper
// game.viper
module Game;

import Config;
import Frog;
import Vehicle;
import Platform;

expose value GameState {
    frog: Frog.Frog;
    vehicles: [Vehicle.Vehicle];
    platforms: [Platform.Platform];
    homesOccupied: [bool];
    score: i64;
    lives: i64;
    level: i64;
    gameOver: bool;
    won: bool;
}

pub func create() -> GameState {
    var state = GameState {
        frog: Frog.create(),
        vehicles: [],
        platforms: [],
        homesOccupied: [false, false, false, false, false],
        score: 0,
        lives: Config.STARTING_LIVES,
        level: 1,
        gameOver: false,
        won: false
    };

    return setupLevel(state);
}

func setupLevel(state: GameState) -> GameState {
    var s = state;
    var speedMod = 1.0 + (s.level - 1) * 0.2;

    // Clear and create vehicles
    s.vehicles = [];

    // Row 8: slow cars going right
    s.vehicles.push(Vehicle.create(8, 60 * speedMod, 60, Color.RED));
    s.vehicles.push(Vehicle.create(8, 60 * speedMod, 60, Color.RED));

    // Row 9: faster cars going left
    s.vehicles.push(Vehicle.create(9, -80 * speedMod, 50, Color.BLUE));
    s.vehicles.push(Vehicle.create(9, -80 * speedMod, 50, Color.BLUE));

    // Row 10: trucks going right
    s.vehicles.push(Vehicle.create(10, 50 * speedMod, 120, Color.YELLOW));

    // Row 11: fast cars going left
    s.vehicles.push(Vehicle.create(11, -120 * speedMod, 40, Color.GREEN));
    s.vehicles.push(Vehicle.create(11, -120 * speedMod, 40, Color.GREEN));
    s.vehicles.push(Vehicle.create(11, -120 * speedMod, 40, Color.GREEN));

    // Row 12: medium cars going right
    s.vehicles.push(Vehicle.create(12, 70 * speedMod, 55, Color.MAGENTA));
    s.vehicles.push(Vehicle.create(12, 70 * speedMod, 55, Color.MAGENTA));

    // Row 13: trucks going left
    s.vehicles.push(Vehicle.create(13, -40 * speedMod, 100, Color.CYAN));
    s.vehicles.push(Vehicle.create(13, -40 * speedMod, 100, Color.CYAN));

    // Spread vehicles out
    for i in 0..s.vehicles.length {
        s.vehicles[i].x = (i * 170) % Config.SCREEN_WIDTH;
    }

    // Clear and create platforms
    s.platforms = [];

    // Row 1-6: logs and turtles
    s.platforms.push(Platform.create(1, 50 * speedMod, 120, Color(139, 69, 19)));
    s.platforms.push(Platform.create(1, 50 * speedMod, 120, Color(139, 69, 19)));
    s.platforms.push(Platform.create(2, -40 * speedMod, 80, Color(0, 100, 0)));
    s.platforms.push(Platform.create(2, -40 * speedMod, 80, Color(0, 100, 0)));
    s.platforms.push(Platform.create(2, -40 * speedMod, 80, Color(0, 100, 0)));
    s.platforms.push(Platform.create(3, 60 * speedMod, 160, Color(139, 69, 19)));
    s.platforms.push(Platform.create(4, -70 * speedMod, 100, Color(0, 100, 0)));
    s.platforms.push(Platform.create(4, -70 * speedMod, 100, Color(0, 100, 0)));
    s.platforms.push(Platform.create(5, 45 * speedMod, 140, Color(139, 69, 19)));
    s.platforms.push(Platform.create(5, 45 * speedMod, 140, Color(139, 69, 19)));
    s.platforms.push(Platform.create(6, -55 * speedMod, 90, Color(0, 100, 0)));
    s.platforms.push(Platform.create(6, -55 * speedMod, 90, Color(0, 100, 0)));

    // Spread platforms out
    for i in 0..s.platforms.length {
        s.platforms[i].x = (i * 130) % Config.SCREEN_WIDTH;
    }

    return s;
}

pub func update(state: GameState, dt: f64) -> GameState {
    var s = state;

    if s.gameOver {
        return s;
    }

    // Update frog
    s.frog = Frog.update(s.frog, dt);

    // Update vehicles
    for i in 0..s.vehicles.length {
        s.vehicles[i] = Vehicle.update(s.vehicles[i], dt);
    }

    // Update platforms
    for i in 0..s.platforms.length {
        s.platforms[i] = Platform.update(s.platforms[i], dt);
    }

    // Check collisions
    s = checkCollisions(s);

    // Check if frog reached home
    s = checkHome(s);

    // Check death
    if !s.frog.alive {
        s.lives -= 1;
        if s.lives <= 0 {
            s.gameOver = true;
        } else {
            s.frog = Frog.spawn();
        }
    }

    // Check level complete
    var allHome = true;
    for occupied in s.homesOccupied {
        if !occupied {
            allHome = false;
            break;
        }
    }

    if allHome {
        s.score += Config.SCORE_PER_LEVEL;
        s.level += 1;
        s.homesOccupied = [false, false, false, false, false];
        s.frog = Frog.spawn();
        s = setupLevel(s);
    }

    return s;
}

func checkCollisions(state: GameState) -> GameState {
    var s = state;
    var frogX = s.frog.x;
    var frogY = s.frog.y;
    var gridY = s.frog.gridY;

    // On the road?
    if gridY >= Config.ROAD_START && gridY <= Config.ROAD_END {
        for vehicle in s.vehicles {
            if Vehicle.hitsPoint(vehicle, frogX, frogY) {
                s.frog = Frog.die(s.frog);
                return s;
            }
        }
    }

    // On the river?
    if gridY >= Config.RIVER_START && gridY <= Config.RIVER_END {
        var onPlatform = false;
        for platform in s.platforms {
            if Platform.containsPoint(platform, frogX, frogY) {
                s.frog.ridingPlatform = platform;
                onPlatform = true;
                break;
            }
        }

        if !onPlatform {
            s.frog = Frog.die(s.frog);
        }
    }

    return s;
}

func checkHome(state: GameState) -> GameState {
    var s = state;

    if s.frog.gridY == Config.HOME_ROW {
        // Which home slot?
        var homeIndex = getHomeIndex(s.frog.gridX);

        if homeIndex >= 0 && homeIndex < 5 {
            if !s.homesOccupied[homeIndex] {
                s.homesOccupied[homeIndex] = true;
                s.score += Config.SCORE_PER_HOME;
                s.frog = Frog.spawn();
            } else {
                // Home already occupied
                s.frog = Frog.die(s.frog);
            }
        } else {
            // Hit the edge, not a home slot
            s.frog = Frog.die(s.frog);
        }
    }

    return s;
}

func getHomeIndex(gridX: i64) -> i64 {
    // 5 home slots spread across the top
    var positions = [2, 6, 10, 14, 18];
    for i in 0..5 {
        if gridX == positions[i] || gridX == positions[i] + 1 {
            return i;
        }
    }
    return -1;
}

pub func moveFrog(state: GameState, dx: i64, dy: i64) -> GameState {
    var s = state;

    if Frog.canMove(s.frog) {
        var oldY = s.frog.gridY;
        s.frog = Frog.move(s.frog, dx, dy);

        // Score for moving forward
        if s.frog.gridY < oldY {
            s.score += Config.SCORE_PER_STEP;
        }
    }

    return s;
}
```

---

## Step 6: Rendering

```viper
// renderer.viper
module Renderer;

import Config;
import Game;
import Viper.Graphics;

pub func render(canvas: Canvas, state: Game.GameState) {
    // Clear
    canvas.setColor(Color.BLACK);
    canvas.fillRect(0, 0, Config.SCREEN_WIDTH, Config.SCREEN_HEIGHT);

    // Draw zones
    drawBackground(canvas);

    // Draw home slots
    drawHomes(canvas, state.homesOccupied);

    // Draw platforms (logs/turtles)
    for platform in state.platforms {
        canvas.setColor(platform.color);
        canvas.fillRect(platform.x, platform.y, platform.width, Config.TILE_SIZE - 4);
    }

    // Draw vehicles
    for vehicle in state.vehicles {
        canvas.setColor(vehicle.color);
        canvas.fillRect(vehicle.x, vehicle.y, vehicle.width, Config.TILE_SIZE - 4);
    }

    // Draw frog
    if state.frog.alive {
        canvas.setColor(Color(0, 255, 0));  // Bright green
        canvas.fillRect(state.frog.x - 15, state.frog.y, 30, 35);
    }

    // Draw UI
    drawUI(canvas, state);
}

func drawBackground(canvas: Canvas) {
    // Sky/homes area
    canvas.setColor(Color(50, 50, 100));
    canvas.fillRect(0, 0, Config.SCREEN_WIDTH, Config.TILE_SIZE);

    // River
    canvas.setColor(Color(0, 0, 150));
    canvas.fillRect(0, Config.RIVER_START * Config.TILE_SIZE,
                   Config.SCREEN_WIDTH, (Config.RIVER_END - Config.RIVER_START + 1) * Config.TILE_SIZE);

    // Safe zone (middle)
    canvas.setColor(Color(100, 50, 150));
    canvas.fillRect(0, Config.SAFE_ZONE * Config.TILE_SIZE,
                   Config.SCREEN_WIDTH, Config.TILE_SIZE);

    // Road
    canvas.setColor(Color(50, 50, 50));
    canvas.fillRect(0, Config.ROAD_START * Config.TILE_SIZE,
                   Config.SCREEN_WIDTH, (Config.ROAD_END - Config.ROAD_START + 1) * Config.TILE_SIZE);

    // Draw lane lines
    canvas.setColor(Color.YELLOW);
    for row in Config.ROAD_START..Config.ROAD_END {
        var y = row * Config.TILE_SIZE + Config.TILE_SIZE / 2;
        for x in 0..(Config.SCREEN_WIDTH / 50) {
            canvas.fillRect(x * 50, y - 2, 30, 4);
        }
    }

    // Start area
    canvas.setColor(Color(100, 50, 150));
    canvas.fillRect(0, Config.START_ROW * Config.TILE_SIZE,
                   Config.SCREEN_WIDTH, Config.TILE_SIZE);
}

func drawHomes(canvas: Canvas, occupied: [bool]) {
    var positions = [2, 6, 10, 14, 18];

    for i in 0..5 {
        var x = positions[i] * Config.TILE_SIZE;

        if occupied[i] {
            canvas.setColor(Color(0, 255, 0));
        } else {
            canvas.setColor(Color(0, 100, 0));
        }

        canvas.fillRect(x, 2, Config.TILE_SIZE * 2, Config.TILE_SIZE - 4);
    }
}

func drawUI(canvas: Canvas, state: Game.GameState) {
    canvas.setColor(Color.WHITE);
    canvas.setFont("Arial", 20);

    canvas.drawText(10, Config.SCREEN_HEIGHT - 10, "Score: " + state.score);
    canvas.drawText(200, Config.SCREEN_HEIGHT - 10, "Lives: " + state.lives);
    canvas.drawText(350, Config.SCREEN_HEIGHT - 10, "Level: " + state.level);

    if state.gameOver {
        canvas.setFont("Arial", 48);
        canvas.setColor(Color.RED);
        canvas.drawText(300, 300, "GAME OVER");

        canvas.setFont("Arial", 24);
        canvas.setColor(Color.WHITE);
        canvas.drawText(280, 350, "Press ENTER to restart");
    }
}
```

---

## Step 7: Main Game Loop

```viper
// main.viper
module Main;

import Config;
import Game;
import Renderer;
import Viper.Graphics;
import Viper.Input;

func start() {
    var canvas = Canvas(Config.SCREEN_WIDTH, Config.SCREEN_HEIGHT);
    canvas.setTitle("Frogger");

    var state = Game.create();
    var lastTime = Viper.Time.millis();

    while canvas.isOpen() {
        var now = Viper.Time.millis();
        var dt = (now - lastTime) / 1000.0;
        lastTime = now;

        // Input
        if state.gameOver {
            if Input.wasKeyPressed(Key.ENTER) {
                state = Game.create();
            }
        } else {
            if Input.wasKeyPressed(Key.UP) || Input.wasKeyPressed(Key.W) {
                state = Game.moveFrog(state, 0, -1);
            }
            if Input.wasKeyPressed(Key.DOWN) || Input.wasKeyPressed(Key.S) {
                state = Game.moveFrog(state, 0, 1);
            }
            if Input.wasKeyPressed(Key.LEFT) || Input.wasKeyPressed(Key.A) {
                state = Game.moveFrog(state, -1, 0);
            }
            if Input.wasKeyPressed(Key.RIGHT) || Input.wasKeyPressed(Key.D) {
                state = Game.moveFrog(state, 1, 0);
            }
        }

        if Input.wasKeyPressed(Key.ESCAPE) {
            break;
        }

        // Update
        state = Game.update(state, dt);

        // Render
        Renderer.render(canvas, state);
        canvas.show();

        Viper.Time.sleep(16);
    }
}
```

---

## What We Built

This complete game demonstrates:

- **Modular architecture**: Separate files for different concerns
- **Data structures**: Structs for frog, vehicles, platforms, game state
- **Game loop**: Input → Update → Render
- **Collision detection**: Point-in-rectangle checks
- **State management**: Tracking score, lives, level, game over
- **Progressive difficulty**: Speed increases with level
- **Multiple entity types**: Vehicles and platforms behave differently
- **User feedback**: Score display, game over screen

---

## Exercises

**Exercise 21.1**: Add sound effects — a hop sound when the frog moves, a splash when it drowns.

**Exercise 21.2**: Add a high score system that saves to a file.

**Exercise 21.3**: Add animated sprites instead of colored rectangles.

**Exercise 21.4**: Add a time limit for each life — score bonus for quick completion.

**Exercise 21.5**: Add a second game mode with different obstacles.

**Exercise 21.6** (Challenge): Add two-player mode — one frog per player, competing for score.

---

*We built a complete game! The skills transfer to any project: modular code, state management, game loops, collision detection. Next, we explore networking — connecting programs across the internet.*

*[Continue to Chapter 22: Networking →](22-networking.md)*
