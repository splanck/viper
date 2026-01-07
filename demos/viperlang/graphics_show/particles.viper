module particles;

// ============================================================================
// PARTICLES.VIPER - Advanced Particle System Demo
// ============================================================================
// Demonstrates a physics-based particle system with gravity, wind,
// multiple emitters, and various particle behaviors.
// ============================================================================

import "./colors";

// Single particle with physics properties
entity Particle {
    expose Number x;
    expose Number y;
    expose Number vx;
    expose Number vy;
    expose Integer life;
    expose Integer maxLife;
    expose Integer baseColor;
    expose Integer size;
    expose Number gravity;
    expose Number drag;

    expose func init(px: Number, py: Number, color: Integer) {
        x = px;
        y = py;
        vx = 0.0;
        vy = 0.0;
        baseColor = color;
        maxLife = 60 + Viper.Random.NextInt(60);
        life = maxLife;
        size = 2 + Viper.Random.NextInt(3);
        gravity = 0.15;
        drag = 0.99;
    }

    expose func setVelocity(velX: Number, velY: Number) {
        vx = velX;
        vy = velY;
    }

    expose func update() {
        vy = vy + gravity;
        vx = vx * drag;
        vy = vy * drag;

        x = x + vx;
        y = y + vy;

        life = life - 1;
    }

    expose func isAlive() -> Boolean {
        return life > 0;
    }

    expose func getColor() -> Integer {
        var fade = life / maxLife;
        if fade < 0.0 {
            fade = 0.0;
        }
        return colors.fadeColor(baseColor, fade);
    }

    expose func draw(canvas: Viper.Graphics.Canvas) {
        if life <= 0 {
            return;
        }

        var drawColor = getColor();
        var px = Viper.Math.Floor(x);
        var py = Viper.Math.Floor(y);

        var currentSize = (size * life) / maxLife;
        if currentSize < 1 {
            currentSize = 1;
        }

        if currentSize <= 1 {
            canvas.Plot(px, py, drawColor);
        } else {
            canvas.Disc(px, py, currentSize, drawColor);
        }
    }
}

// Particle emitter
entity Emitter {
    expose Number x;
    expose Number y;
    expose Integer color;
    expose Integer rate;
    expose Number spread;
    expose Number speed;
    expose Number angle;
    expose Boolean active;

    expose func init(px: Number, py: Number, c: Integer) {
        x = px;
        y = py;
        color = c;
        rate = 5;
        spread = 0.5;
        speed = 5.0;
        angle = -1.5708;
        active = true;
    }

    expose func emit(system: ParticleSystem) {
        if not active {
            return;
        }

        var i = 0;
        while i < rate {
            if system.particles.count() < system.maxParticles {
                var p = new Particle();
                p.init(x, y, color);

                var emitAngle = angle + (Viper.Random.Next() - 0.5) * spread;
                var emitSpeed = speed * (0.8 + Viper.Random.Next() * 0.4);

                var velX = Viper.Math.Cos(emitAngle) * emitSpeed;
                var velY = Viper.Math.Sin(emitAngle) * emitSpeed;
                p.setVelocity(velX, velY);

                system.particles.add(p);
            }
            i = i + 1;
        }
    }
}

// Particle system manager
entity ParticleSystem {
    expose List[Particle] particles;
    expose Integer maxParticles;
    expose Number wind;

    expose func init(maxP: Integer) {
        maxParticles = maxP;
        particles = [];
        wind = 0.0;
    }

    expose func update(screenWidth: Integer, screenHeight: Integer) {
        var i = 0;
        while i < particles.count() {
            var p = particles.get(i);
            p.vx = p.vx + wind;
            p.update();

            if p.y > screenHeight {
                p.y = screenHeight;
                p.vy = p.vy * -0.6;
            }

            i = i + 1;
        }

        removeDead();
    }

    expose func removeDead() {
        var i = particles.count() - 1;
        while i >= 0 {
            var p = particles.get(i);
            if not p.isAlive() {
                particles.removeAt(i);
            }
            i = i - 1;
        }
    }

    expose func draw(canvas: Viper.Graphics.Canvas) {
        var i = 0;
        while i < particles.count() {
            var p = particles.get(i);
            p.draw(canvas);
            i = i + 1;
        }
    }

    expose func clear() {
        particles.clear();
    }

    expose func getCount() -> Integer {
        return particles.count();
    }
}

// Run the particle demo
func run(canvas: Viper.Graphics.Canvas) {
    var width = canvas.Width;
    var height = canvas.Height;

    colors.initColors();

    var system = new ParticleSystem();
    system.init(2000);

    var emitter1 = new Emitter();
    emitter1.init(width / 4, height - 50, colors.ORANGE);
    emitter1.rate = 8;
    emitter1.speed = 8.0;

    var emitter2 = new Emitter();
    emitter2.init(width / 2, height - 50, colors.CYAN);
    emitter2.rate = 6;
    emitter2.speed = 10.0;
    emitter2.spread = 0.3;

    var emitter3 = new Emitter();
    emitter3.init((width * 3) / 4, height - 50, colors.MAGENTA);
    emitter3.rate = 8;
    emitter3.speed = 8.0;

    var running = true;
    var showEmitters = true;
    var frameCount = 0;

    while running {
        canvas.Poll();

        if canvas.ShouldClose != 0 {
            running = false;
        }

        if canvas.KeyHeld(27) != 0 {
            running = false;
        }
        if canvas.KeyHeld(81) != 0 {
            running = false;
        }

        if canvas.KeyHeld(262) != 0 {
            system.wind = system.wind + 0.02;
            if system.wind > 0.5 {
                system.wind = 0.5;
            }
        }
        if canvas.KeyHeld(263) != 0 {
            system.wind = system.wind - 0.02;
            if system.wind < -0.5 {
                system.wind = -0.5;
            }
        }

        if canvas.KeyHeld(82) != 0 {
            system.wind = 0.0;
        }

        if canvas.KeyHeld(67) != 0 {
            system.clear();
        }

        emitter1.emit(system);
        emitter2.emit(system);
        emitter3.emit(system);

        system.update(width, height);

        canvas.Clear(Viper.Graphics.Color.RGB(10, 10, 20));

        system.draw(canvas);

        if showEmitters {
            canvas.Ring(Viper.Math.Floor(emitter1.x), Viper.Math.Floor(emitter1.y), 10, colors.ORANGE);
            canvas.Ring(Viper.Math.Floor(emitter2.x), Viper.Math.Floor(emitter2.y), 10, colors.CYAN);
            canvas.Ring(Viper.Math.Floor(emitter3.x), Viper.Math.Floor(emitter3.y), 10, colors.MAGENTA);
        }

        canvas.TextBg(10, 10, "PARTICLE SYSTEM", colors.WHITE, colors.BLACK);

        var countStr = "Particles: " + Viper.Fmt.Int(system.getCount());
        canvas.TextBg(10, 28, countStr, colors.GRAY, colors.BLACK);

        var windStr = "Wind: " + Viper.Fmt.Num(system.wind);
        canvas.TextBg(10, 46, windStr, colors.GRAY, colors.BLACK);

        canvas.TextBg(10, height - 30, "LEFT/RIGHT: Wind | C: Clear | R: Reset Wind | ESC: Exit", colors.DARK_GRAY, colors.BLACK);

        canvas.Flip();
        frameCount = frameCount + 1;
        Viper.Time.SleepMs(16);
    }
}
