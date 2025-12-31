module test_minimal;

func main() {
    var canvas = Viper.Graphics.Canvas.New("Test", 400, 300);

    var black = Viper.Graphics.Color.RGB(0, 0, 0);
    var white = Viper.Graphics.Color.RGB(255, 255, 255);

    var i = 0;
    while i < 60 {
        canvas.Poll();
        canvas.Clear(black);
        canvas.Text(10, 10, "Hello Viper!", white);
        canvas.Flip();
        Viper.Time.SleepMs(16);
        i = i + 1;
    }
}
