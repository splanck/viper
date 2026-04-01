# Viper.Game.LevelData

JSON-based level loader that parses tilemap data and entity spawn objects from a single file.

## Level JSON Format

```json
{
    "width": 50, "height": 18,
    "tileWidth": 32, "tileHeight": 32,
    "properties": {
        "theme": "grasslands",
        "playerStartX": 96,
        "playerStartY": 480
    },
    "layers": [{
        "name": "terrain",
        "type": "tiles",
        "data": [0, 0, 1, 1, 1, 0, ...]
    }],
    "objects": [
        {"type": "enemy", "id": "slime", "x": 640, "y": 480},
        {"type": "pickup", "id": "coin", "x": 320, "y": 384}
    ]
}
```

## API

### LevelData.Load(path) -> LevelData?
### Properties
- `Tilemap` — The parsed Tilemap object
- `ObjectCount` — Number of objects in the level
- `PlayerStartX`, `PlayerStartY` — Player spawn position
- `Theme` — Theme string from properties
### Methods
- `ObjectType(index)` — Get object type string at index
- `ObjectId(index)` — Get object id string at index
- `ObjectX(index)`, `ObjectY(index)` — Get object position

## Example
```zia
var level = LevelData.Load("levels/level1.json")
var tilemap = level.get_Tilemap()
player.set_X(level.get_PlayerStartX() * 100)

var i = 0
while i < level.get_ObjectCount() {
    if level.ObjectType(i) == "enemy" {
        enemies.spawn(level.ObjectId(i), level.ObjectX(i), level.ObjectY(i))
    }
    i = i + 1
}
```
