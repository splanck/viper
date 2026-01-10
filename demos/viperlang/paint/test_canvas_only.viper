module test_canvas_only;

import "./config";
import "./canvas";

func main() {
    var c = new canvas.DrawingCanvas();
    c.initWithSize(100, 100);
}
