# 25 — Game: Meta Systems — Economy, Scoring, Difficulty, NG+, Stats

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track G · 1 session.
> Prereqs: 11 (save schema), 20 §1 (hub consumes the workbench UI). The layer that makes
> nine levels a *campaign*: persistent progression, replay hooks, and honest numbers.

## 1. Salvage economy (`meta/economy.zia`)

- Income: kill drops (per-archetype table; environment-kills ×1.25 — L7 press bonus rule),
  secrets (data cores 40, caches 25), medal bonuses on level complete (bronze 50 / silver
  100 / gold 200), Ghost/Pacifist-segment bonuses (15 §5).
- **Workbench** (hub): per-weapon 3 tiers — T1 handling (reload −15 %, ADS −20 %), T2
  signature perk (Pistol silencer, Scattergun choke, SMG extended mag 36→50, Pulse compensator
  (pattern ×0.7), Rail fast-charge, Arc sticky rounds, Rivet triple-pin, Shard 3rd bounce,
  EMP radius +30 %), T3 damage +18 %. Costs 150/300/500 scaled; total economy tuned so
  full-clear ≈ 65 % of all upgrades by L9 (choices matter; NG+ carries over → completionism).
  Suit tracks: shield cap ×3, hull ×3, grenade pouch ×2, medkit pouch ×2.
- Refund: full respec at workbench (friction-free experimentation — showcase, not grind).
- All tables in config; purchases → profile.json instantly.

## 2. Scoring & medals (`meta/scoring.zia`)

Per-level results card (LEVEL_COMPLETE state): time vs par, accuracy (shots-hit/fired,
pellets counted per-pellet), secrets x/y, deaths, style ticks (headshot %, environment kills,
takedowns). Medals: bronze = complete; silver = 2-of-3 (par time, accuracy ≥ par, all
secrets); gold = all three + deaths ≤ 1. Ghost badge (L2/L5: zero LOCKDOWNs), Wrecker badge
(L7: ≥ 15 environment kills). Medals gate nothing (arena wave-plus is the only unlockable —
respect players' time) but render on the mission table + title cards. Speedrun timer option:
always-on HUD timer + per-level splits table, IGT = fixed ticks (determinism makes leaderboard
claims verifiable — a Viper flex worth documenting in the README).

## 3. Difficulty (`meta/difficulty.zia`)

| Knob | Scout | Soldier | Nightmare |
|---|---|---|---|
| Enemy damage ×| 0.7 | 1.0 | 1.4 |
| Player shield regen delay | 2.5 s | 3.5 s | 5 s |
| Director tokens (melee/ranged) | 1/3 | 2/4 | 3/5 |
| Awareness fill × | 0.8 | 1.0 | 1.3 |
| Wave sizes × | 0.8 | 1.0 | 1.25 |
| Drop bias strength | 0.5 | 0.35 | 0.2 |
Selected at campaign start, changeable at hub (stats flag "mixed"). All scalars route through
one `difficulty.scalar(K_*)` accessor (probe: no raw difficulty branches outside the module).

## 4. NG+ mutators (post-credits unlock; combinable at campaign start)

Ironman (no manual checkpoint reload; death = level restart), Glass (player ×0.5 HP, ×1.5
damage dealt), Swarm (wave sizes ×1.5, tokens +1), Overclock (game speed ×1.15 — fixed-step
rate stays 60, world-scale time multiplier through dt consumers; probe: determinism holds),
Cinematic (locks Cinematic tier + photo-mode anywhere). Mutator flags stamp the results
cards + stats.

## 5. Stats & profile (`meta/stats.zia`)

Lifetime + per-campaign counters (kills by archetype/weapon, accuracy, distance, salvage,
deaths by cause, playtime by level, choices from dialogue prompts) — fed entirely by event-bus
consumption (zero inline counters in gameplay code; probe asserts stat deltas from a scripted
event script). Stats page at hub + title; feeds the credits stats card (21 §3).

## 6. Probes

Economy conservation (income − spend == balance across save round-trips; respec lossless);
medal math truth table; difficulty scalar coverage scan; each mutator's contract (Overclock
determinism, Ironman checkpoint behavior); stats-from-events golden; profile.json version
migration (v1 → v1+field). VM==native.

## 7. Verification gate

Probes green; hub loop hands-on: earn → buy → feel the T2 perk difference in the range
(27 §2 spot rows); results card math matches a hand-computed run. Full build green.
