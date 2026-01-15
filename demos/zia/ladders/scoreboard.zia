// scoreboard.viper - Score display and high score tracking for Ladders game
module scoreboard;

import "./config";
import "./colors";

// High score entry
entity HighScoreEntry {
    expose String name;
    expose Integer score;
    expose Integer level;

    expose func init(n: String, s: Integer, l: Integer) {
        name = n;
        score = s;
        level = l;
    }
}

// Scoreboard state - track current game and high scores
var currentScore: Integer;
var currentLevel: Integer;
var currentLives: Integer;
var collectiblesRemaining: Integer;
var highScores: List[HighScoreEntry];
var highScore: Integer;

// Initialize scoreboard
func initScoreboard() {
    currentScore = 0;
    currentLevel = 1;
    currentLives = config.INITIAL_LIVES;
    collectiblesRemaining = 0;
    highScore = 0;

    // Initialize high scores list with default entries
    highScores = [];

    var entry1 = new HighScoreEntry();
    entry1.init("AAA", 5000, 3);
    highScores.add(entry1);

    var entry2 = new HighScoreEntry();
    entry2.init("BBB", 3000, 2);
    highScores.add(entry2);

    var entry3 = new HighScoreEntry();
    entry3.init("CCC", 1000, 1);
    highScores.add(entry3);

    highScore = 5000;
}

// Reset score for new game
func resetScore() {
    currentScore = 0;
    currentLevel = 1;
    currentLives = config.INITIAL_LIVES;
}

// Add points to score
func addScore(points: Integer) {
    currentScore = currentScore + points;
    if currentScore > highScore {
        highScore = currentScore;
    }
}

// Set current level
func setLevel(level: Integer) {
    currentLevel = level;
}

// Set remaining lives
func setLives(lives: Integer) {
    currentLives = lives;
}

// Set collectibles remaining
func setCollectiblesRemaining(count: Integer) {
    collectiblesRemaining = count;
}

// Get current score
func getScore() -> Integer {
    return currentScore;
}

// Get high score
func getHighScore() -> Integer {
    return highScore;
}

// Get current level
func getLevel() -> Integer {
    return currentLevel;
}

// Get current lives
func getLives() -> Integer {
    return currentLives;
}

// Add level completion bonus
func addLevelBonus() {
    addScore(config.SCORE_PER_LEVEL);
}

// Check if score qualifies for high score list
func isHighScore(score: Integer) -> Integer {
    if highScores.count() < 5 {
        return 1;
    }
    var lowest = highScores.get(highScores.count() - 1);
    if score > lowest.score {
        return 1;
    }
    return 0;
}

// Add new high score entry
func addHighScore(name: String, score: Integer, level: Integer) {
    var entry = new HighScoreEntry();
    entry.init(name, score, level);

    // Find position to insert
    var pos = 0;
    while pos < highScores.count() {
        var existing = highScores.get(pos);
        if score > existing.score {
            break;
        }
        pos = pos + 1;
    }

    // Insert at position
    highScores.insert(pos, entry);

    // Keep only top 5
    while highScores.count() > 5 {
        highScores.removeAt(highScores.count() - 1);
    }

    // Update high score
    if highScores.count() > 0 {
        var top = highScores.get(0);
        highScore = top.score;
    }
}

// Draw the in-game HUD
func drawHUD(canvas: Viper.Graphics.Canvas) {
    // Score
    canvas.Text(10, 10, "SCORE: " + Viper.Fmt.Int(currentScore), colors.UI_SCORE_COLOR);

    // High score
    canvas.Text(config.SCREEN_WIDTH / 2 - 50, 10, "HIGH: " + Viper.Fmt.Int(highScore), colors.GOLD);

    // Level
    canvas.Text(config.SCREEN_WIDTH - 100, 10, "LVL: " + Viper.Fmt.Int(currentLevel), colors.UI_TEXT_COLOR);

    // Lives (draw hearts or icons)
    var lifeX = 10;
    var lifeY = 30;
    canvas.Text(lifeX, lifeY, "LIVES:", colors.UI_LIVES_COLOR);
    lifeX = lifeX + 60;

    var i = 0;
    while i < currentLives {
        // Draw a small heart/life icon
        canvas.Disc(lifeX + i * 18, lifeY + 4, 5, colors.RED);
        i = i + 1;
    }

    // Collectibles remaining
    if collectiblesRemaining > 0 {
        canvas.Text(config.SCREEN_WIDTH - 150, 30, "ITEMS: " + Viper.Fmt.Int(collectiblesRemaining), colors.COLLECTIBLE_COLOR);
    } else {
        canvas.Text(config.SCREEN_WIDTH - 150, 30, "EXIT OPEN!", colors.GREEN);
    }
}

// Draw the high scores screen
func drawHighScores(canvas: Viper.Graphics.Canvas) {
    // Background
    canvas.Box(0, 0, config.SCREEN_WIDTH, config.SCREEN_HEIGHT, colors.MENU_BG_COLOR);

    // Title
    canvas.Text(config.SCREEN_WIDTH / 2 - 70, 50, "HIGH SCORES", colors.GOLD);

    // Headers
    var headerY = 100;
    canvas.Text(100, headerY, "RANK", colors.LIGHT_GRAY);
    canvas.Text(180, headerY, "NAME", colors.LIGHT_GRAY);
    canvas.Text(280, headerY, "SCORE", colors.LIGHT_GRAY);
    canvas.Text(400, headerY, "LEVEL", colors.LIGHT_GRAY);

    // Line under headers
    canvas.Line(80, headerY + 20, config.SCREEN_WIDTH - 80, headerY + 20, colors.DARK_GRAY);

    // Score entries
    var entryY = 140;
    var rank = 1;

    var i = 0;
    while i < highScores.count() {
        var entry = highScores.get(i);

        var rankColor = colors.UI_TEXT_COLOR;
        if rank == 1 {
            rankColor = colors.GOLD;
        } else if rank == 2 {
            rankColor = colors.LIGHT_GRAY;
        } else if rank == 3 {
            rankColor = colors.BROWN;
        }

        canvas.Text(110, entryY, Viper.Fmt.Int(rank), rankColor);
        canvas.Text(180, entryY, entry.name, colors.WHITE);
        canvas.Text(280, entryY, Viper.Fmt.Int(entry.score), colors.UI_SCORE_COLOR);
        canvas.Text(410, entryY, Viper.Fmt.Int(entry.level), colors.UI_TEXT_COLOR);

        entryY = entryY + 40;
        rank = rank + 1;
        i = i + 1;
    }

    // Return prompt
    canvas.Text(config.SCREEN_WIDTH / 2 - 100, config.SCREEN_HEIGHT - 60, "Press ENTER to return", colors.YELLOW);
}
