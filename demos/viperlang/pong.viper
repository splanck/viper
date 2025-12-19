// Pong - ViperLang Demo
// A simple pong game for one player
module Pong;

func start() {
    Viper.Terminal.SetAltScreen(1);
    Viper.Terminal.SetCursorVisible(0);

    var playing = 1;
    var score = 0;
    var lives = 3;

    // Ball position and velocity
    var ballX = 40;
    var ballY = 12;
    var ballDX = 1;
    var ballDY = 1;

    // Paddle position (right side)
    var paddleY = 10;
    var paddleSize = 5;

    while (playing == 1) {
        // Draw
        Viper.Terminal.BeginBatch();
        Viper.Terminal.Clear();

        // Draw border
        Viper.Terminal.SetColor(7, 0);
        var i = 1;
        while (i <= 78) {
            Viper.Terminal.SetPosition(1, i);
            Viper.Terminal.Print("=");
            Viper.Terminal.SetPosition(23, i);
            Viper.Terminal.Print("=");
            i = i + 1;
        }

        // Draw paddle
        Viper.Terminal.SetColor(3, 0);  // Yellow
        i = 0;
        while (i < paddleSize) {
            Viper.Terminal.SetPosition(paddleY + i, 75);
            Viper.Terminal.Print("|");
            Viper.Terminal.SetPosition(paddleY + i, 76);
            Viper.Terminal.Print("|");
            i = i + 1;
        }

        // Draw ball
        Viper.Terminal.SetColor(2, 0);  // Green
        Viper.Terminal.SetPosition(ballY, ballX);
        Viper.Terminal.Print("O");

        // Draw score and lives
        Viper.Terminal.SetColor(7, 0);
        Viper.Terminal.SetPosition(24, 2);
        Viper.Terminal.Print("Score: ");
        Viper.Terminal.PrintInt(score);
        Viper.Terminal.Print("  Lives: ");
        Viper.Terminal.PrintInt(lives);
        Viper.Terminal.Print("  |  W/S to move, Q to quit");

        Viper.Terminal.EndBatch();

        // Get input
        var key = Viper.Terminal.GetKeyTimeout(50);
        if (key == "q") {
            playing = 0;
        }
        if (key == "w") {
            if (paddleY > 2) {
                paddleY = paddleY - 1;
            }
        }
        if (key == "s") {
            if (paddleY + paddleSize < 22) {
                paddleY = paddleY + 1;
            }
        }

        // Move ball
        ballX = ballX + ballDX;
        ballY = ballY + ballDY;

        // Ball collision with top/bottom
        if (ballY <= 2) {
            ballDY = 1;
            ballY = 3;
        }
        if (ballY >= 22) {
            ballDY = 0 - 1;
            ballY = 21;
        }

        // Ball collision with left wall (bounce back)
        if (ballX <= 2) {
            ballDX = 1;
            ballX = 3;
        }

        // Ball collision with paddle
        if (ballX >= 74) {
            if (ballY >= paddleY) {
                if (ballY < paddleY + paddleSize) {
                    // Hit!
                    ballDX = 0 - 1;
                    ballX = 73;
                    score = score + 10;
                    Viper.Terminal.Bell();
                }
            }
        }

        // Ball missed paddle
        if (ballX >= 77) {
            lives = lives - 1;
            if (lives <= 0) {
                playing = 0;
            } else {
                // Reset ball
                ballX = 40;
                ballY = 12;
                ballDX = 1;
                // Vary direction based on score
                if (score > 50) {
                    ballDY = 0 - 1;
                } else {
                    ballDY = 1;
                }
                Viper.Time.SleepMs(500);
            }
        }
    }

    // Game over screen
    Viper.Terminal.BeginBatch();
    Viper.Terminal.Clear();
    Viper.Terminal.SetColor(1, 0);  // Red
    Viper.Terminal.SetPosition(10, 30);
    Viper.Terminal.Say("GAME OVER");
    Viper.Terminal.SetColor(7, 0);
    Viper.Terminal.SetPosition(12, 25);
    Viper.Terminal.Print("Final Score: ");
    Viper.Terminal.SayInt(score);
    Viper.Terminal.SetPosition(15, 20);
    Viper.Terminal.Say("Press any key to exit...");
    Viper.Terminal.EndBatch();

    Viper.Terminal.GetKey();

    Viper.Terminal.SetCursorVisible(1);
    Viper.Terminal.SetAltScreen(0);
    Viper.Terminal.Say("Thanks for playing Pong!");
}
