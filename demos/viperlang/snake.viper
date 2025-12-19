// Snake Game - ViperLang Demo
// A simple snake game with growing snake
module Snake;

func start() {
    Viper.Terminal.SetAltScreen(1);
    Viper.Terminal.SetCursorVisible(0);

    var playing = 1;
    var score = 0;
    var length = 3;

    // Snake position (head)
    var headX = 40;
    var headY = 12;

    // Direction: 0=up, 1=right, 2=down, 3=left
    var dir = 1;

    // Simple tail tracking (just 3 positions for now)
    var t1x = 38;
    var t1y = 12;
    var t2x = 37;
    var t2y = 12;
    var t3x = 36;
    var t3y = 12;

    // Food position
    var foodX = 60;
    var foodY = 12;

    while (playing == 1) {
        // Draw
        Viper.Terminal.BeginBatch();
        Viper.Terminal.Clear();

        // Draw border
        Viper.Terminal.SetColor(7, 0);
        var i = 1;
        while (i <= 78) {
            Viper.Terminal.SetPosition(1, i);
            Viper.Terminal.Print("-");
            Viper.Terminal.SetPosition(23, i);
            Viper.Terminal.Print("-");
            i = i + 1;
        }
        i = 2;
        while (i <= 22) {
            Viper.Terminal.SetPosition(i, 1);
            Viper.Terminal.Print("|");
            Viper.Terminal.SetPosition(i, 78);
            Viper.Terminal.Print("|");
            i = i + 1;
        }

        // Draw food
        Viper.Terminal.SetColor(1, 0);  // Red
        Viper.Terminal.SetPosition(foodY, foodX);
        Viper.Terminal.Print("*");

        // Draw snake tail
        Viper.Terminal.SetColor(2, 0);  // Green
        Viper.Terminal.SetPosition(t3y, t3x);
        Viper.Terminal.Print("o");
        Viper.Terminal.SetPosition(t2y, t2x);
        Viper.Terminal.Print("o");
        Viper.Terminal.SetPosition(t1y, t1x);
        Viper.Terminal.Print("o");

        // Draw snake head
        Viper.Terminal.SetColor(3, 0);  // Cyan
        Viper.Terminal.SetPosition(headY, headX);
        Viper.Terminal.Print("@");

        // Draw score
        Viper.Terminal.SetColor(7, 0);
        Viper.Terminal.SetPosition(24, 2);
        Viper.Terminal.Print("Score: ");
        Viper.Terminal.PrintInt(score);
        Viper.Terminal.Print("  |  WASD to move, Q to quit");

        Viper.Terminal.EndBatch();

        // Get input with timeout
        var key = Viper.Terminal.GetKeyTimeout(100);
        if (key == "q") {
            playing = 0;
        }
        if (key == "w") {
            if (dir != 2) {
                dir = 0;
            }
        }
        if (key == "s") {
            if (dir != 0) {
                dir = 2;
            }
        }
        if (key == "a") {
            if (dir != 1) {
                dir = 3;
            }
        }
        if (key == "d") {
            if (dir != 3) {
                dir = 1;
            }
        }

        // Move tail
        t3x = t2x;
        t3y = t2y;
        t2x = t1x;
        t2y = t1y;
        t1x = headX;
        t1y = headY;

        // Move head
        if (dir == 0) {
            headY = headY - 1;
        }
        if (dir == 1) {
            headX = headX + 1;
        }
        if (dir == 2) {
            headY = headY + 1;
        }
        if (dir == 3) {
            headX = headX - 1;
        }

        // Wrap around
        if (headX < 2) {
            headX = 77;
        }
        if (headX > 77) {
            headX = 2;
        }
        if (headY < 2) {
            headY = 22;
        }
        if (headY > 22) {
            headY = 2;
        }

        // Check food collision
        if (headX == foodX) {
            if (headY == foodY) {
                score = score + 10;
                // Move food to new position (simple pattern)
                foodX = foodX + 17;
                if (foodX > 70) {
                    foodX = foodX - 60;
                }
                foodY = foodY + 7;
                if (foodY > 20) {
                    foodY = foodY - 18;
                }
            }
        }
    }

    Viper.Terminal.SetCursorVisible(1);
    Viper.Terminal.SetAltScreen(0);
    Viper.Terminal.Print("Final Score: ");
    Viper.Terminal.SayInt(score);
    Viper.Terminal.Say("Thanks for playing!");
}
