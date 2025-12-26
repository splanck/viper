' Particle class - represents a single particle

CLASS Particle
    PUBLIC X AS DOUBLE
    PUBLIC Y AS DOUBLE
    PUBLIC VX AS DOUBLE
    PUBLIC VY AS DOUBLE
    PUBLIC BaseColor AS INTEGER
    PUBLIC Life AS INTEGER
    PUBLIC MaxLife AS INTEGER

    SUB New(startX AS DOUBLE, startY AS DOUBLE, c AS INTEGER)
        X = startX
        Y = startY
        VX = 0.0
        VY = 0.0
        BaseColor = c
        MaxLife = 120
        Life = MaxLife
    END SUB

    SUB Update()
        X = X + VX
        Y = Y + VY
        Life = Life - 1
    END SUB

    FUNCTION IsAlive() AS INTEGER
        IsAlive = Life > 0
    END FUNCTION

    FUNCTION GetColor() AS INTEGER
        ' Fade the color based on remaining life
        DIM fade AS DOUBLE
        fade = CDBL(Life) / CDBL(MaxLife)
        IF fade < 0.0 THEN fade = 0.0
        IF fade > 1.0 THEN fade = 1.0

        ' Extract RGB components and scale
        DIM r AS INTEGER
        DIM g AS INTEGER
        DIM b AS INTEGER
        r = (BaseColor / 65536) MOD 256
        g = (BaseColor / 256) MOD 256
        b = BaseColor MOD 256

        r = INT(CDBL(r) * fade)
        g = INT(CDBL(g) * fade)
        b = INT(CDBL(b) * fade)

        GetColor = Viper.Graphics.Color.RGB(r, g, b)
    END FUNCTION

    SUB ApplyGravity(g AS DOUBLE)
        VY = VY + g
    END SUB

    SUB BounceInBounds(w AS INTEGER, h AS INTEGER)
        IF X < 0.0 THEN
            X = 0.0
            VX = -VX * 0.8
        END IF
        IF X > w THEN
            X = w
            VX = -VX * 0.8
        END IF
        IF Y < 0.0 THEN
            Y = 0.0
            VY = -VY * 0.8
        END IF
        IF Y > h THEN
            Y = h
            VY = -VY * 0.8
        END IF
    END SUB
END CLASS
