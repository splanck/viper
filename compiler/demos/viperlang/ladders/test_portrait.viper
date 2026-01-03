// Minimal test with portrait dimensions (same as centipede: 480x640)
module main;

func main() {
    var canvas = new Viper.Graphics.Canvas("Portrait Test", 480, 640);

    var white = Viper.Graphics.Color.RGB(255, 255, 255);
    var running = 1;

    while running == 1 {
        canvas.Poll();

        if canvas.ShouldClose != 0 {
            running = 0;
        }

        canvas.Clear(0);
        canvas.Text(100, 100, "HELLO WORLD", white);
        canvas.Text(100, 120, "Test 123", white);
        canvas.Flip();

        Viper.Time.SleepMs(16);
    }
}
