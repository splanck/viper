' =============================================================================
' API Audit: Viper.Math.Quat - Quaternion Operations (BASIC)
' =============================================================================
' Tests: New, Identity, FromAxisAngle, FromEuler, X, Y, Z, W, Mul,
'        Conjugate, Inverse, Norm, Len, LenSq, Dot, Slerp, Lerp,
'        RotateVec3, ToMat4, Axis, Angle
' =============================================================================

PRINT "=== API Audit: Viper.Math.Quat ==="

' --- New ---
PRINT "--- New ---"
DIM q1 AS Viper.Math.Quat
q1 = Viper.Math.Quat.New(0.0, 0.0, 0.0, 1.0)
PRINT "Quat.New(0,0,0,1) X: "; q1.X
PRINT "Quat.New(0,0,0,1) Y: "; q1.Y
PRINT "Quat.New(0,0,0,1) Z: "; q1.Z
PRINT "Quat.New(0,0,0,1) W: "; q1.W

' --- Identity ---
PRINT "--- Identity ---"
DIM qi AS Viper.Math.Quat
qi = Viper.Math.Quat.Identity()
PRINT "Identity X: "; qi.X
PRINT "Identity Y: "; qi.Y
PRINT "Identity Z: "; qi.Z
PRINT "Identity W: "; qi.W

' --- FromAxisAngle ---
PRINT "--- FromAxisAngle ---"
DIM axis AS Viper.Math.Vec3
axis = Viper.Math.Vec3.New(0.0, 1.0, 0.0)
DIM qa AS Viper.Math.Quat
qa = Viper.Math.Quat.FromAxisAngle(axis, 1.5707963267948966)
PRINT "FromAxisAngle(Y, Pi/2) X: "; qa.X
PRINT "FromAxisAngle(Y, Pi/2) Y: "; qa.Y
PRINT "FromAxisAngle(Y, Pi/2) Z: "; qa.Z
PRINT "FromAxisAngle(Y, Pi/2) W: "; qa.W

' --- FromEuler ---
PRINT "--- FromEuler ---"
DIM qe AS Viper.Math.Quat
qe = Viper.Math.Quat.FromEuler(0.0, 1.5707963267948966, 0.0)
PRINT "FromEuler(0, Pi/2, 0) X: "; qe.X
PRINT "FromEuler(0, Pi/2, 0) Y: "; qe.Y
PRINT "FromEuler(0, Pi/2, 0) Z: "; qe.Z
PRINT "FromEuler(0, Pi/2, 0) W: "; qe.W
DIM qe2 AS Viper.Math.Quat
qe2 = Viper.Math.Quat.FromEuler(0.0, 0.0, 0.0)
PRINT "FromEuler(0,0,0) W: "; qe2.W

' --- Mul ---
PRINT "--- Mul ---"
DIM qm AS Viper.Math.Quat
qm = qa.Mul(qi)
PRINT "Mul(rotY, identity) X: "; qm.X
PRINT "Mul(rotY, identity) Y: "; qm.Y
PRINT "Mul(rotY, identity) Z: "; qm.Z
PRINT "Mul(rotY, identity) W: "; qm.W

' --- Conjugate ---
PRINT "--- Conjugate ---"
DIM qc AS Viper.Math.Quat
qc = qa.Conjugate()
PRINT "Conjugate(rotY) X: "; qc.X
PRINT "Conjugate(rotY) Y: "; qc.Y
PRINT "Conjugate(rotY) Z: "; qc.Z
PRINT "Conjugate(rotY) W: "; qc.W

' --- Inverse ---
PRINT "--- Inverse ---"
DIM qinv AS Viper.Math.Quat
qinv = qa.Inverse()
PRINT "Inverse(rotY) X: "; qinv.X
PRINT "Inverse(rotY) Y: "; qinv.Y
PRINT "Inverse(rotY) Z: "; qinv.Z
PRINT "Inverse(rotY) W: "; qinv.W

' --- Norm ---
PRINT "--- Norm ---"
DIM q2 AS Viper.Math.Quat
q2 = Viper.Math.Quat.New(1.0, 2.0, 3.0, 4.0)
DIM qn AS Viper.Math.Quat
qn = q2.Norm()
PRINT "Norm(1,2,3,4) Len: "; qn.Len()

' --- Len ---
PRINT "--- Len ---"
PRINT "Len(identity): "; qi.Len()
PRINT "Len(rotY): "; qa.Len()
PRINT "Len(1,2,3,4): "; q2.Len()

' --- LenSq ---
PRINT "--- LenSq ---"
PRINT "LenSq(identity): "; qi.LenSq()
PRINT "LenSq(1,2,3,4): "; q2.LenSq()

' --- Dot ---
PRINT "--- Dot ---"
PRINT "Dot(identity, identity): "; qi.Dot(qi)
PRINT "Dot(rotY, identity): "; qa.Dot(qi)

' --- Slerp ---
PRINT "--- Slerp ---"
DIM qs0 AS Viper.Math.Quat
qs0 = qi.Slerp(qa, 0.0)
PRINT "Slerp(identity, rotY, 0.0) W: "; qs0.W
DIM qs5 AS Viper.Math.Quat
qs5 = qi.Slerp(qa, 0.5)
PRINT "Slerp(identity, rotY, 0.5) W: "; qs5.W
DIM qs1 AS Viper.Math.Quat
qs1 = qi.Slerp(qa, 1.0)
PRINT "Slerp(identity, rotY, 1.0) W: "; qs1.W
PRINT "Slerp(identity, rotY, 1.0) Y: "; qs1.Y

' --- Lerp ---
PRINT "--- Lerp ---"
DIM ql0 AS Viper.Math.Quat
ql0 = qi.Lerp(qa, 0.0)
PRINT "Lerp(identity, rotY, 0.0) W: "; ql0.W
DIM ql5 AS Viper.Math.Quat
ql5 = qi.Lerp(qa, 0.5)
PRINT "Lerp(identity, rotY, 0.5) W: "; ql5.W
DIM ql1 AS Viper.Math.Quat
ql1 = qi.Lerp(qa, 1.0)
PRINT "Lerp(identity, rotY, 1.0) W: "; ql1.W

' --- RotateVec3 ---
PRINT "--- RotateVec3 ---"
DIM vx AS Viper.Math.Vec3
vx = Viper.Math.Vec3.New(1.0, 0.0, 0.0)
DIM rv AS Viper.Math.Vec3
rv = qa.RotateVec3(vx)
PRINT "RotateVec3(rotY90, (1,0,0)) X: "; rv.X
PRINT "RotateVec3(rotY90, (1,0,0)) Y: "; rv.Y
PRINT "RotateVec3(rotY90, (1,0,0)) Z: "; rv.Z
DIM rv2 AS Viper.Math.Vec3
rv2 = qi.RotateVec3(vx)
PRINT "RotateVec3(identity, (1,0,0)) X: "; rv2.X

' --- ToMat4 ---
PRINT "--- ToMat4 ---"
DIM m AS OBJECT
m = qi.ToMat4()
PRINT "ToMat4(identity) [0,0]: "; Viper.Math.Mat4.Get(m, 0, 0)
PRINT "ToMat4(identity) [1,1]: "; Viper.Math.Mat4.Get(m, 1, 1)
PRINT "ToMat4(identity) [2,2]: "; Viper.Math.Mat4.Get(m, 2, 2)
PRINT "ToMat4(identity) [3,3]: "; Viper.Math.Mat4.Get(m, 3, 3)

' --- Axis ---
PRINT "--- Axis ---"
DIM ax AS Viper.Math.Vec3
ax = qa.Axis()
PRINT "Axis(rotY) X: "; ax.X
PRINT "Axis(rotY) Y: "; ax.Y
PRINT "Axis(rotY) Z: "; ax.Z

' --- Angle ---
PRINT "--- Angle ---"
PRINT "Angle(rotY): "; qa.Angle()
PRINT "Angle(identity): "; qi.Angle()

PRINT "=== Quat Audit Complete ==="
END
