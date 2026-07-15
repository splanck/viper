' test_scene.bas - Viper.Game2D.SceneDocument smoke
DIM scene AS OBJECT
scene = Viper.Game2D.SceneDocument.New(3, 2, 16, 16)
scene.SetInt("playerStartX", 32)
scene.SetStr("theme", "cavern")
scene.SetBool("dark", TRUE)
PRINT scene.GetInt("playerStartX", -1)
PRINT scene.GetStr("theme", "")
PRINT scene.GetBool("dark", FALSE)

DIM layer AS INTEGER
layer = scene.AddLayer("fg")
scene.SetTile(layer, 1, 1, 7)
scene.SetLayerAsset(layer, "tiles/fg.png")

DIM obj AS INTEGER
obj = scene.AddObject("enemy", "slime-1", 64, 48)
scene.ObjectSetInt(obj, "hp", 3)
scene.ObjectSetStr(obj, "sprite", "sprites/slime.png")
PRINT scene.ObjectGetInt(obj, "hp", -1)
PRINT Viper.Option.UnwrapOrI64(scene.FindObjectOption("slime-1"), -1)
PRINT scene.CountOfType("enemy")

DIM json AS STRING
json = scene.ToJson()
PRINT Viper.String.Contains(json, """version""")

DIM loaded AS OBJECT
loaded = Viper.Game2D.SceneDocument.LoadJson(json)
PRINT loaded.HasErrors()
PRINT loaded.GetInt("playerStartX", -1)
PRINT loaded.ObjectGetInt(0, "hp", -1)

DIM bad AS OBJECT
bad = Viper.Game2D.SceneDocument.LoadJson("{""version"":1,")
PRINT bad.HasErrors()

DIM tm AS OBJECT
tm = scene.BuildTilemap()
PRINT tm.GetTileLayer(layer, 1, 1)
PRINT scene.AssetPaths().Count
PRINT scene.AssetDescriptors().Count

PRINT "done"
END
