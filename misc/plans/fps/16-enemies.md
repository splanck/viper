# 16 — Game: The Eleven Enemy Archetypes

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track G · **2-session chunk**
> (session A: Husk, Sapper, Drone, Turret, Mites — the P3 arena set with fallback bodies;
> session B: Ranger, Marauder, Vanguard, Stalker, Wraith, Shrike — the P6 set on real rigs).
> Prereqs: 15-ai-framework, 18-animation rigs (session B), 14-weapons (damage pipeline).
> Bosses live in 17. Every archetype: distinct silhouette, tell, counter, and death.

Per-archetype spec fields (all in `config.zia` tables + one module each under `ai/`):
HP/plate map, speed, senses (range/cone/rates), attacks (damage/telegraph/cooldown/range),
tokens used, anim set (18), audio set (24), salvage drop, spawn cost (director budget),
fallback body (procedural, 26 §fallbacks).

| # | Archetype | HP | Locomotion | Behavior core | Attacks (telegraph → effect) | Counter / weak point | Death |
|---|---|---|---|---|---|---|---|
| 1 | **Husk** | 70 | NavAgent 4.2 m/s | Swarm chase; melee token; pack-spacing via RVO; lunge uses root motion (E26 tree) | Swipe (0.4 s arm-raise → 18 dmg); Lunge ≥ 5 m (0.6 s crouch-glow → leap 22 dmg + knock) | Any sustained fire; scattergun deletes; legs shot → crawl mode (half speed, still swipes) | Collapse + spark burst; 15 % arc-flail 1.5 s |
| 2 | **Sapper** | 40 | NavAgent 5.6 m/s | Beeline charge; beep pitch ramps with proximity (E5); detonates at 2 m | Self-detonate (r 5 m, 55 dmg, barrel-class blast) | Shoot the **back tank** (2.5×, instant chain-boom damaging other machines — placement puzzle) | Detonation (its own blast pipeline) |
| 3 | **Scavenger Drone** | 55 | Kinematic flyer: seek→orbit 8 m→strafe; bob noise; SweepSphere obstacle probes ×3 | Harass: keeps lateral velocity, repositions on hit | Zapper (0.5 s charge whine + glow → 12 dmg hitscan zap) | Lead the strafe; EMP drops it 4 s (crash-bounce, then reboots airborne) | Spiral-down + explosion on impact |
| 4 | **Sentry Turret** | 160 fixed | Yoke pan/tilt (LookAt IK) | Scan sweep 90° w/ visible cone light (spot, zone-budgeted); spin-up on sight | Sustained fire (1.0 s spin-up whir → 8 dmg×10/s stream, suppresses) | Rear **power cell** (3×, disables 8 s then dead); EMP 4 s; hack via interact when disabled (L2+, flips IFF — turrets fight machines) | Head-pop + smoke column |
| 5 | **Swarm Mites** | 8 ×12–20 | InstanceBatch bodies (E22 SW-safe); boid cohesion/separation around a carrier point | Latch: 3 dmg/s drain + screen-edge static FX; distraction pressure | Latch (contact) | Explosions/sweeps clear clumps; any AoE | Pop-sparks (instanced particle burst) |
| 6 | **Ranger** | 110 | NavAgent 4.8; cover user | 15 §4 cover cycle; burst discipline; relocates when flanked; calls tokens | Rifle burst (laser-sight tell 0.3 s → 3×9 dmg); flush-grenade if player camps 8 s (arcs the same CCD grenade) | Flank (cover facing is directional); head 2× | Slump over cover (pose event) |
| 7 | **Marauder** | 320 + 4 plates ×60 | NavAgent 2.6, unstaggerable walk | Slow advance, suppression stream; plates ablate individually (compound colliders) | Cannon stream (2 s spin-up → 6 dmg×12/s, forces cover); stomp ≤ 3 m (ring shockwave 30 dmg + knock) | Strip a plate → core exposed (2.5×); Rail penetration ignores 1 plate; bait the stomp | Kneel → core meltdown → chain-pop plates |
| 8 | **Vanguard** | 130 + shield 200 | NavAgent 4.0; squad anchor | Directional energy shield (bubble quad, additive material) blocks frontal; pushes with squad, shield-bash at 2 m | Bash (0.4 s → 24 dmg + slide-knock); arm pistol while shielded (6 dmg pot-shots) | Shield is **frontal 140°**: flank, Shard bank shots, or EMP (drops it 4 s, panics) | Shield implode-flash → collapse |
| 9 | **Stalker** | 90 | NavAgent 6.5 + off-mesh links | 15 flank pathing (direct-route area cost ×6); vents/ledges via links (E29 state → leap anim); circles before pounce | Pounce (audible skitter + 0.5 s crouch → leap pin 26 dmg, wrench-mash QTE-free: any damage dislodges) | Watch flanks/ceilings; SMG hipfire; light it (dark-zone native) | Ragdoll-lite tumble (impulse + tumble anim) |
| 10 | **Wraith** | 140 | NavAgent 5.0, silent | Cloak: shimmer material (alpha+noise anim, 26 §materials), no minimap ping; hunts by *your* noise (inverts stealth); decoys hard-lure it; ignores dark-zone factor | Blade rake from cloak (decloaks 0.5 s before — shimmer intensifies + whisper-static audio tell → 34 dmg bleed) | Damage/EMP forces decloak 3 s; fight loud = it stays visible (loudness ≥ 40 pins cloak off) | Decloak-glitch cascade → fold |
| 11 | **Shrike** | 200 | Kinematic flyer, fast lanes between hover points | Strafing runs: telegraphed attack lane (dust line + siren), drops 4–6 Mites on pass; altitude swaps | Rocket pair per pass (CCD, 40 dmg blast) | Rail/EMP during hover-turn window (2.2 s); rockets shootable (tiny HP → premature boom) | Wing-off spiral crash (scripted impact blast) |

Shared systems detail:
- **Locational tables**: named collider regions per archetype (head/core/tank/plate0-3/cell/
  wing) resolved in the damage pipeline (13 §2); region → multiplier + special flags
  (plate-ablate, tank-chain, cell-disable).
- **Spawn/despawn**: director-owned; fade-in shader none — machines walk/fly/drop in via
  authored spawn nodes (doors, vents, sky lanes); despawn only DEAD after 10 s + off-screen
  (pool return).
- **Fallback bodies** (session A ships with these; GLB swap in P5): capsule+box frames with
  emissive eye strips, per-archetype color/silhouette blocking — game fully playable before
  any download (asset-optional rule).
- **Anim intents** (18 consumes): `AI_MOVE(speed2d), AI_AIM(dir), AI_ATTACK(id), AI_STAGGER,
  AI_LEAP(link), AI_DEATH(kind)` — enemies never touch AnimController3D directly.

## Probes

Per archetype scripted duels (headless, fixed seeds): (a) tell timing — attack lands exactly
telegraph+strike ticks after trigger; (b) counter validity — Sapper tank chain kills adjacent
Husk, Marauder plate order ablates, Vanguard frontal blocks 100 %/flank passes, Turret cell
disable window, Wraith decloak rules, Mites clear by single frag; (c) token/cover compliance
under the 15 invariants; (d) full-mix brawl (2 of each ground type) 5,000 ticks: zero traps,
budgets hold, deterministic event log VM==native; (e) perf per archetype: sim cost table
recorded (sum ≤ 15 §7 budget).

## Verification gate

Session A: arena playable vs Husk/Sapper/Drone/Turret/Mites on fallback bodies — P3 vertical
slice sign-off (fun check 27 §2). Session B: full roster on rigs, L2/L5-style graybox
encounters read clearly (streamer test: name the enemy from silhouette alone at 30 m).
Probes + full build green.
