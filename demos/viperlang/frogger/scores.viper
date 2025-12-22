// ============================================================================
// Frogger - High Score System
// ============================================================================
// Manages high score tracking (in-memory only due to VL-036 file I/O bug).
// ============================================================================

module Scores;

import "config";
import "terminal";

// High score entry
entity HighScore {
    expose String name;
    expose Integer score;

    expose func init(n: String, s: Integer) {
        name = n;
        score = s;
    }

    expose func getName() -> String {
        return name;
    }

    expose func getScore() -> Integer {
        return score;
    }
}

// High score manager (in-memory only)
entity HighScoreManager {
    expose List[HighScore] scores;

    expose func init() {
        scores = [];
    }

    expose func getCount() -> Integer {
        return scores.size();
    }

    expose func getScore(index: Integer) -> HighScore {
        return scores.get(index);
    }

    expose func isHighScore(testScore: Integer) -> Boolean {
        if scores.size() < MAX_HIGH_SCORES {
            return testScore > 0;
        }
        var lastScore = scores.get(scores.size() - 1);
        return testScore > lastScore.getScore();
    }

    expose func addScore(playerName: String, scoreValue: Integer) {
        var entry = new HighScore(playerName, scoreValue);

        // Find insertion position (sorted descending)
        var insertPos = scores.size();
        for i in 0..scores.size() {
            var existing = scores.get(i);
            if scoreValue > existing.getScore() {
                insertPos = i;
                break;
            }
        }

        // Insert at position by adding and then shifting
        scores.add(entry);
        var j = scores.size() - 1;
        while j > insertPos {
            var temp = scores.get(j - 1);
            scores.set(j, temp);
            j = j - 1;
        }
        scores.set(insertPos, entry);

        // Trim to max size
        while scores.size() > MAX_HIGH_SCORES {
            scores.remove(scores.size() - 1);
        }
    }

    expose func load() {
        // NOTE: File I/O not available in ViperLang (VL-036)
        // Scores are in-memory only for this session
    }

    expose func save() {
        // NOTE: File I/O not available in ViperLang (VL-036)
        // Scores are in-memory only for this session
    }

    expose func display() {
        clearScreen();
        Viper.Terminal.Say("");
        Viper.Terminal.Say("============================================");
        Viper.Terminal.Say("        CLASSIC FROGGER - HIGH SCORES       ");
        Viper.Terminal.Say("============================================");
        Viper.Terminal.Say("");

        if scores.size() == 0 {
            Viper.Terminal.Say("           No high scores yet!");
            Viper.Terminal.Say("           Be the first to set a record!");
        } else {
            Viper.Terminal.Say("  RANK    NAME                    SCORE");
            Viper.Terminal.Say("  ----------------------------------------");

            for i in 0..scores.size() {
                var entry = scores.get(i);
                var rank = i + 1;
                Viper.Terminal.Print("   ");
                Viper.Terminal.PrintInt(rank);
                Viper.Terminal.Print("      ");
                Viper.Terminal.Print(entry.getName());

                // Pad name to align scores
                var nameLen = Viper.String.Length(entry.getName());
                var padding = 24 - nameLen;
                for j in 0..padding {
                    Viper.Terminal.Print(" ");
                }

                Viper.Terminal.SayInt(entry.getScore());
            }
        }

        Viper.Terminal.Say("");
        Viper.Terminal.Say("  Press any key to continue...");
    }
}

// Get player name for high score entry
func getPlayerName() -> String {
    Viper.Terminal.Say("");
    Viper.Terminal.Say("NEW HIGH SCORE!");
    Viper.Terminal.Say("");
    Viper.Terminal.Say("Enter your name (max 15 chars, press ENTER when done):");
    Viper.Terminal.Print("> ");

    var playerName = "";
    var BS = Viper.String.Chr(8);  // Backspace character

    while Viper.String.Length(playerName) < 15 {
        var ch = Viper.Terminal.GetKeyTimeout(50);

        if Viper.String.Length(ch) > 0 {
            var code = ch.CharAt(0);

            if code == 13 || code == 10 {
                break;
            } else if code == 8 || code == 127 {
                if Viper.String.Length(playerName) > 0 {
                    var nameLen = Viper.String.Length(playerName);
                    playerName = playerName.Substring(0, nameLen - 1);
                    Viper.Terminal.Print(BS + " " + BS);
                }
            } else if code >= 32 && code <= 126 {
                playerName = playerName + ch;
                Viper.Terminal.Print(ch);
            }
        }
    }

    Viper.Terminal.Say("");

    if Viper.String.Length(playerName) == 0 {
        playerName = "Anonymous";
    }

    return playerName;
}
