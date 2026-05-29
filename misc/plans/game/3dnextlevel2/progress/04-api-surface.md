# API surface tracker

Tracks the public surface from `../api-spec.md`. All rows start `todo`. Many
systems are internal C and expose only control/capability getters — those are
marked "(ctrl)".

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| API-CARRY-001 | `Light3D.CastsShadows` get/set | Carryover | todo |  |  |  |  | CO-6 |
| API-CARRY-002 | `BackendSupports("rt-finalize")` + RT finalization | Carryover | todo |  |  |  |  | CO-4 |
| API-JOB-001 | `World3D.workerCount`/`setWorkerCount`/`jobsEnabled` (ctrl) | Concurrency | todo |  |  |  |  | internal job API |
| API-ORIG-001 | `World3D.floatingOrigin` + `setOriginRebaseThreshold` + `worldOrigin` | Large coords | todo |  |  |  |  |  |
| API-IDX-001 | `Scene3D.queryAABB/querySphere/raycastNodes` + `visibleNodeCount` | Spatial | todo |  |  |  |  |  |
| API-ASYNC-001 | `Assets3D.LoadModelAsync/LoadModelAssetAsync` + `ModelHandle` | Async assets | todo |  |  |  |  | handle lifetime D (zia-feasibility) |
| API-ASYNC-002 | `Assets3D.SetResidencyBudget` + `Evict` | Async assets | todo |  |  |  |  |  |
| API-STREAM-001 | `WorldStream.New/setCenter/setRadii/update` | Streaming | todo |  |  |  |  |  |
| API-STREAM-002 | `WorldStream.mountTiledTerrain` / `mountCells` + resident counts | Streaming | todo |  |  |  |  |  |
| API-VIS-001 | `Canvas3D.SetOcclusionCulling` (real) / `SetFrustumCulling` | Visibility | todo |  |  |  |  | CO-8 |
| API-VIS-002 | `SceneNode3D.SetAutoLOD` / `SetImpostor`; `OccludedDrawCount` | Visibility | todo |  |  |  |  |  |
| API-LIT-001 | `Canvas3D.SetClusteredLighting` + `MaxActiveLights` | Lighting | todo |  |  |  |  |  |
| API-LIT-002 | `Canvas3D.SetShadowCascades` | Lighting | todo |  |  |  |  |  |
| API-PHYS-001 | `Physics3DWorld.SetSolverIterations`; `CollisionEvent3D.ContactCount` >1 | Physics | todo |  |  |  |  |  |
| API-PHYS-002 | `HingeJoint3D` / `RopeJoint3D` / `SixDOFJoint3D` + `AddJoint` types | Physics | todo |  |  |  |  | closes overclaim |
| API-PHYS-003 | `Collider3D.NewConvexHull` becomes real convex (no sig change) | Physics | todo |  |  |  |  |  |
| API-NAV-001 | `NavMesh3D.Bake` / `BakeTiled` (autogen) | Navigation | todo |  |  |  |  |  |
| API-NAV-002 | `NavMesh3D.RebuildTile`/`AddObstacle`/`AddOffMeshLink` | Navigation | todo |  |  |  |  |  |
| API-NAV-003 | `NavAgent3D.AvoidanceEnabled`/`AvoidanceRadius` | Navigation | todo |  |  |  |  |  |
| API-ANIM-001 | `IK3D.TwoBone/LookAt/FABRIK` + `setTarget/setWeight/solve` | Animation | todo |  |  |  |  |  |
| API-ANIM-002 | `AnimController3D.PlayLayerAdditive` (true additive) | Animation | todo |  |  |  |  |  |
| API-ANIM-003 | `BlendTree3D.New1D/New2D` + samples/params | Animation | todo |  |  |  |  |  |
| API-ANIM-004 | `Animation3D.Retarget` + `AnimController3D.SetAnimationLOD` | Animation | todo |  |  |  |  |  |
| API-TEX-001 | `Material3D.SetAlbedoCompressed` + `BackendSupports("bc7"/"astc"/"etc2")` | Asset pipeline | todo |  |  |  |  |  |
| API-TEX-002 | `Model3D.GetCameraCount/GetCamera/SelectScene` | Asset pipeline | todo |  |  |  |  | glTF camera/scene |
| API-EX-001 | Open-world hello (manual loop) compiles | Hello | todo |  |  |  |  | callback sugar pending CO-2 |
| API-GAME-001 | `Game3D.Animator3D` ik/blendTree/additive wrappers | Game3D | todo |  |  |  |  |  |
| API-GAME-002 | `World3D` owns optional `WorldStream` | Game3D | todo |  |  |  |  |  |
