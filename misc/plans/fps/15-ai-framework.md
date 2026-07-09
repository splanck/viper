# 15 — Game: AI Framework — Perception, Director, Cover, Stealth

> **STATUS: PLANNED (2026-07-07)** · Baseline `3166d1dc2` · Track G · 1–2 sessions.
> Prereqs: 11-architecture (events, registry), 12-core-loop (noise stances), navmesh baked
> per level (19-22). Delivers the shared AI substrate all 11 archetypes + 3 bosses run on,
> plus the stealth ruleset. Individual enemies live in 16-enemies.

## 1. Perception (`ai/perception.zia`)

Per-AI senses evaluated on a stagger wheel (each AI every 4th tick; alert AIs every 2nd;
> 40 m every 8th — AI LOD):
- **Sight**: range (per archetype), view cone (horiz 110°/vert 60° default), LOS =
  `Physics3DWorld.Raycast` eye→player-chest with WORLD mask (one ray; +head ray when partial);
  **light-level factor** outdoors/dark zones: detection speed × player's zone light factor
  (level data marks dark zones 0.35–1.0; flashlight ON forces 1.0 within 8 m — tradeoff);
  stance factor (crouch 0.6); movement factor (still 0.5, sprint 1.4); distance falloff.
- **Awareness meter** 0→100 per AI: fills at `rate × factors` while sighted, decays 8/s after
  2 s unseen. Thresholds: 35 SUSPICIOUS (look-at, off-path glance), 70 SEARCHING (move to
  last-known-position, sweep), 100 COMBAT (squad alert broadcast `EV_ALERT_RAISED`).
  Player-facing UI: detection petals (23-ui) driven by the max filling AI.
- **Hearing**: consume event-bus noise events (`EV_GUNSHOT(pos, loudness)` from weapons,
  footsteps, explosions, decoys, glass); audible if dist < loudness × surface factor;
  unsilenced gunshot = zone-wide. Heard-not-seen → SEARCHING at the noise point.
- **Team memory**: squad shares COMBAT sightings (last-known-position broadcast 0.5 s cadence);
  SUSPICIOUS/SEARCHING remain individual (stealth remains local).

## 2. FSM base (`ai/ai_base.zia`)

Int-state machine per archetype (11-architecture registry slots): shared states
`IDLE, PATROL(waypoints from level data), SUSPICIOUS, SEARCHING, COMBAT, STAGGER, DISABLED
(EMP), DEAD` + archetype extensions (FLANK, COVER, CHARGE...). Tick order: senses (staggered)
→ state logic → steering (NavAgent3D target or kinematic for flyers) → animation intents
(18-anim consumes) → weapon/attack gates. All transitions push `EV_*` rows (probes assert
legal tables). Stagger: damage > threshold in 0.5 s window → 0.7 s stagger, cooldown 4 s
(no stunlock). EMP → DISABLED 4 s (drones drop kinematically, turrets slump, shields off).

## 3. Squad director (`ai/director.zia`)

Level-wide brain, one per level:
- **Active cap**: ≤ 12 engaged AIs; spawner queues beyond that (wave data from level
  manifests) — spawn-in staged behind cover/doors, never in player sight cone (spawn-point
  LOS check).
- **Attack tokens**: melee 2, ranged 4 (difficulty-scaled) — token holders may commit attacks;
  others reposition/suppress. Token grant by (distance, LOS, time-since-last) score. Kills the
  everyone-rushes problem and creates readable combat rhythm.
- **Morale**: squad loses members fast → survivors take SEARCHING retreat beats (machines
  "recalculate") — pacing valve, config per encounter.
- **Alert states** per zone: CALM → ALERT (searchers) → LOCKDOWN (turrets on, doors shut,
  reinforcement wave) — stealth consequences; resets after 45 s clean.
- **Difficulty hooks** (25-meta): token counts, awareness rates, damage scalars, wave sizes.

## 4. Cover system (`ai/cover.zia`)

Cover points authored in level manifests (pos + facing + height full/half) — ~40/combat space.
Runtime scoring on request (Ranger/Marauder/Vanguard): valid = `NavMesh3D.SamplePosition`
reachable + LOS-blocked to player at stand height + LOS-open at peek height + dist band
8–25 m + not-flanked (player-to-cover-facing dot) + unclaimed (parallel claim list).
Score = safety 0.5 + proximity-band 0.3 + flank-angle 0.2; take best, claim, path via
NavAgent (avoidance on). Peek cycle: cover 1.2–2.2 s → peek-fire burst → cover; relocate on
flanked/suppressed (3 near-misses in 1 s → suppression flag) or token loss.

## 5. Stealth ruleset (consolidated; consumers: L2/L5 + open play)

Player tools: crouch noise 2.5 m, dark zones, silenced pistol (T2), wrench takedowns (14 §5),
decoys, EMP (disables alarms/turrets 4 s), corpse... none (machines slump; no body-carry —
scope control). AI counters: SEARCHING sweeps in pairs, LOCKDOWN on discovered kills
(a dead squadmate in patrol path → instant zone ALERT), Wraith ignores dark-zone factor
(it hunts by sound — 16-enemies). **No forced stealth**: every stealth space is winnable loud;
stealth = resource/economy advantage + medal category (25-meta "Ghost" bonus). Detection UI
honest: petals only fill from real senses (probe-asserted), no rubber-band omniscience.

## 6. Debug & tuning (`devtools` integration)

F3 sub-page: per-AI state/awareness/token/cover-claim overlay lines + `DebugDraw` navmesh
(engine has it) + sight-cone wireframes (DrawLine2D projections) behind `DEV_CHEATS`.
Encounter tuning loop: arena wave mode (22 §arena) with director knobs live-adjustable via
dev keys — tune tokens/waves without relaunching.

## 7. Probes

(a) Awareness math: scripted approach at stances/lights → meter crosses thresholds at golden
tick counts; (b) hearing: silenced vs loud shot → SEARCHING radius honored; (c) token
invariant: never > caps engaged attackers over 2,000-tick brawl (event-log scan);
(d) cover validity: every chosen point blocks stand-LOS and opens peek-LOS (geometric
recheck); flank forces relocate ≤ 1.5 s; (e) alert ladder: takedown seen vs unseen →
zone states diverge correctly; LOCKDOWN reinforcement wave spawns off-sight; (f) determinism:
full 15-AI scripted encounter replays identical event logs (VM==native, `-O0/-O2`);
(g) perf: 12 active + 4 queued AIs ≤ 1.8 ms/tick sim budget on M-class (recorded).

## 8. Verification gate

Probes green headless → arena hands-on vs mixed squad (Husk×4, Ranger×3, Drone×2): combat
reads as coordinated-but-fair (27 §2 checklist scored); stealth loop in the L2 graybox slice
(dark corridor + 3 patrols): ghost-clear and loud-clear both viable. Full build green.
