DIM base AS OBJECT
DIM inst AS OBJECT
DIM clone AS OBJECT

PRINT "=== Material3D PBR Demo ==="

base = Zanna.Graphics3D.Material3D.PBR(0.7, 0.55, 0.35)
Zanna.Graphics3D.Material3D.set_Metallic(base, 0.8)
Zanna.Graphics3D.Material3D.set_Roughness(base, 0.25)
Zanna.Graphics3D.Material3D.set_AmbientOcclusion(base, 0.9)
Zanna.Graphics3D.Material3D.set_EmissiveIntensity(base, 1.4)
Zanna.Graphics3D.Material3D.set_NormalScale(base, 0.65)
Zanna.Graphics3D.Material3D.set_Alpha(base, 0.7)
Zanna.Graphics3D.Material3D.set_AlphaMode(base, 2)
Zanna.Graphics3D.Material3D.set_DoubleSided(base, 1)

inst = Zanna.Graphics3D.Material3D.MakeInstance(base)
Zanna.Graphics3D.Material3D.set_Roughness(inst, 0.75)
Zanna.Graphics3D.Material3D.set_Metallic(inst, 0.15)
Zanna.Graphics3D.Material3D.set_DoubleSided(inst, 0)

clone = Zanna.Graphics3D.Material3D.Clone(base)

PRINT "Base Metallic = "; Zanna.Graphics3D.Material3D.get_Metallic(base)
PRINT "Base Roughness = "; Zanna.Graphics3D.Material3D.get_Roughness(base)
PRINT "Instance Roughness = "; Zanna.Graphics3D.Material3D.get_Roughness(inst)
PRINT "Instance DoubleSided = "; Zanna.Graphics3D.Material3D.get_DoubleSided(inst)
PRINT "Clone AlphaMode = "; Zanna.Graphics3D.Material3D.get_AlphaMode(clone)
