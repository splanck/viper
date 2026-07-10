# Profiles, saves, and recovery

XENOSCAPE has three player-selected expedition slots and a separate global
settings store. The release campaign schema is version 2.

Each profile summary reports area, checkpoint, playtime, completion percentage,
difficulty, maximum HP, ability count, upgrade tiers, banked salvage, and a
compact local last-write stamp. Continue selects the most recently written valid
profile. Migrated profiles without a timestamp fall back to farthest area,
playtime, and slot number. Empty Continue and Load actions show explicit
nonblocking messages. Deletion and overwrite require confirmation.

## Autosave points

Progress is saved at save stations, permanent ability acquisition, boss defeat,
permanent Workbench purchase, regional entry, postgame-result commit, and a
clean return to title or application exit. Simulation modes first save a
campaign snapshot; their world, pickups, deaths, and interactions are discarded
on return. A Time Trial commits only an improved regional time/rank.

## Persistent campaign data

The snapshot includes abilities, max HP, banked salvage, keys, lore, bestiary,
upgrades, difficulty, map rooms, switches, doors, shrines, teleporters,
checkpoint coordinates, tutorials, regional best times/ranks, and New Game+
state. Temporary enemies, projectiles, power-ups, and room combat state are
rebuilt after death or reload.

## Corrupt or failed saves

Malformed data is copied to a `.corrupt.bak` file when the platform permits,
then safe defaults are restored. The game reports:

> Save data was unreadable. A backup was preserved and defaults were restored.

A failed write does not bank unbanked salvage and reports the storage-permission
message without blocking play. Save probes use isolated SaveData namespaces and
never alter production profiles.

`SaveData.Save()` writes the complete JSON payload through a same-directory
temporary file, flushes it, and atomically replaces the live save. A failed
write therefore leaves the prior valid payload in place.
