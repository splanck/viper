module test_canvas;

// Test if we can create a canvas using the .New constructor function directly

func main() {
    // Try calling the constructor function directly instead of using 'new' keyword
    var canvas = Viper.Graphics.Canvas.New("Test Window", 800, 600);

    var white = Viper.Graphics.Color.RGB(255, 255, 255);
    canvas.Clear(white);
    canvas.Text(10, 10, "Hello from Viperlang!", white);
    canvas.Flip();

    Viper.Time.SleepMs(2000);
}
