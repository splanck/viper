' API Audit: Viper.Game.Physics2D (BASIC)
PRINT "=== API Audit: Viper.Game.Physics2D ==="
DIM world AS OBJECT = Viper.Game.Physics2D.World.New(0, 980)
PRINT world.BodyCount
DIM body AS OBJECT = Viper.Game.Physics2D.Body.New(100, 0, 32, 32, 0)
world.Add(body)
PRINT world.BodyCount
PRINT body.X
PRINT body.Y
PRINT body.Width
PRINT body.Height
body.SetVel(10, 0)
PRINT body.VX
PRINT body.VY
body.ApplyForce(0, 100)
world.Step(16)
PRINT body.IsStatic
PRINT body.Mass
PRINT body.Restitution
PRINT body.Friction
world.SetGravity(0, 0)
world.Remove(body)
PRINT world.BodyCount
PRINT "=== Physics2D Audit Complete ==="
END
