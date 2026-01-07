' Emitter class - spawns particles at a location

CLASS Emitter
    PUBLIC X AS DOUBLE
    PUBLIC Y AS DOUBLE
    PUBLIC SpawnRate AS INTEGER  ' particles per second
    PUBLIC Color AS INTEGER
    PRIVATE FrameCounter AS INTEGER

    SUB New(posX AS DOUBLE, posY AS DOUBLE, rate AS INTEGER, c AS INTEGER)
        X = posX
        Y = posY
        SpawnRate = rate
        Color = c
        FrameCounter = 0
    END SUB

    SUB Emit(ps AS ParticleSystem)
        FrameCounter = FrameCounter + 1
        ' At 60fps, spawn SpawnRate particles per second
        IF FrameCounter >= (60 / SpawnRate) THEN
            FrameCounter = 0
            DIM p AS Particle
            p = NEW Particle(X, Y, Color)
            ' Random velocity spread using NextInt
            p.VX = (Viper.Random.NextInt(100) - 50) / 10.0
            p.VY = -(Viper.Random.NextInt(50) + 20) / 10.0
            ps.AddParticle(p)
        END IF
    END SUB
END CLASS
