' test_math_extra.bas — Quat, Scanner, CompiledPattern
DIM q AS Viper.Math.Quat
q = Viper.Math.Quat.New(0.0, 0.0, 0.0, 1.0)
PRINT "quat x: "; q.X
PRINT "quat y: "; q.Y
PRINT "quat z: "; q.Z
PRINT "quat w: "; q.W

DIM qi AS Viper.Math.Quat
qi = Viper.Math.Quat.Identity()
PRINT "identity w: "; qi.W
PRINT "identity x: "; qi.X

DIM qe AS Viper.Math.Quat
qe = Viper.Math.Quat.FromEuler(0.0, 0.0, 0.0)
PRINT "euler w: "; qe.W

PRINT "quat len: "; qi.Len()
PRINT "quat lensq: "; qi.LenSq()

DIM qc AS Viper.Math.Quat
qc = qi.Conjugate()
PRINT "conjugate w: "; qc.W

DIM qinv AS Viper.Math.Quat
qinv = qi.Inverse()
PRINT "inverse w: "; qinv.W

PRINT "quat dot: "; qi.Dot(q)

DIM sc AS OBJECT
sc = Viper.Text.Scanner.New("hello world")
PRINT "scanner ident: "; sc.ReadIdent()

DIM pat AS OBJECT
DIM matches AS OBJECT
DIM count AS INTEGER
pat = Viper.Text.CompiledPattern.New("[0-9]+")
matches = pat.FindAll("a1b22c333")
count = matches.Length
PRINT "match count: "; count

PRINT "done"
END
