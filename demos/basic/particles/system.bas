' ParticleSystem class - manages a collection of particles

CLASS ParticleSystem
    PUBLIC Particles AS Viper.Collections.Seq
    PUBLIC MaxParticles AS INTEGER

    SUB New(maxP AS INTEGER)
        MaxParticles = maxP
        Particles = NEW Viper.Collections.Seq()
    END SUB

    SUB AddParticle(p AS Particle)
        IF Particles.Len < MaxParticles THEN
            Particles.Push(p)
        END IF
    END SUB

    SUB UpdateAll(gravity AS DOUBLE, w AS INTEGER, h AS INTEGER)
        DIM i AS INTEGER
        DIM p AS Particle
        FOR i = 0 TO Particles.Len - 1
            p = Particles.Get(i)
            p.ApplyGravity(gravity)
            p.Update()
            p.BounceInBounds(w, h)
        NEXT i
    END SUB

    SUB RemoveDead()
        ' Remove dead particles by rebuilding the list
        DIM newList AS Viper.Collections.Seq
        newList = NEW Viper.Collections.Seq()
        DIM i AS INTEGER
        DIM p AS Particle
        FOR i = 0 TO Particles.Len - 1
            p = Particles.Get(i)
            IF p.IsAlive() THEN
                newList.Push(p)
            END IF
        NEXT i
        Particles = newList
    END SUB

    SUB DrawAll(canvas AS Viper.Graphics.Canvas)
        DIM i AS INTEGER
        DIM p AS Particle
        FOR i = 0 TO Particles.Len - 1
            p = Particles.Get(i)
            canvas.Disc(INT(p.X), INT(p.Y), 4, p.GetColor())
        NEXT i
    END SUB
END CLASS
