DIM base AS OBJECT
DIM inst AS OBJECT
DIM clone AS OBJECT

PRINT "=== Material3D PBR Demo ==="

base = Viper.Graphics3D.Material3D.NewPBR(0.7, 0.55, 0.35)
Viper.Graphics3D.Material3D.set_Metallic(base, 0.8)
Viper.Graphics3D.Material3D.set_Roughness(base, 0.25)
Viper.Graphics3D.Material3D.set_AO(base, 0.9)
Viper.Graphics3D.Material3D.set_EmissiveIntensity(base, 1.4)
Viper.Graphics3D.Material3D.set_NormalScale(base, 0.65)
Viper.Graphics3D.Material3D.set_Alpha(base, 0.7)
Viper.Graphics3D.Material3D.set_AlphaMode(base, 2)
Viper.Graphics3D.Material3D.set_DoubleSided(base, 1)

inst = Viper.Graphics3D.Material3D.MakeInstance(base)
Viper.Graphics3D.Material3D.set_Roughness(inst, 0.75)
Viper.Graphics3D.Material3D.set_Metallic(inst, 0.15)
Viper.Graphics3D.Material3D.set_DoubleSided(inst, 0)

clone = Viper.Graphics3D.Material3D.Clone(base)

PRINT "Base Metallic = "; Viper.Graphics3D.Material3D.get_Metallic(base)
PRINT "Base Roughness = "; Viper.Graphics3D.Material3D.get_Roughness(base)
PRINT "Instance Roughness = "; Viper.Graphics3D.Material3D.get_Roughness(inst)
PRINT "Instance DoubleSided = "; Viper.Graphics3D.Material3D.get_DoubleSided(inst)
PRINT "Clone AlphaMode = "; Viper.Graphics3D.Material3D.get_AlphaMode(clone)
