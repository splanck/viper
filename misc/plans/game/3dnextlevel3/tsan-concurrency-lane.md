# NL3-004 TSan Concurrency Lane

Recorded: 2026-05-30 23:07 EDT

Host:

- macOS 26.5 build 25F71
- Darwin 25.5.0 arm64
- Apple M4 Max
- AppleClang 21.0.0.21000101

Command:

```sh
scripts/g3d_tsan_concurrency_lane.sh --no-configure
```

The initial run configured `/Users/stephen/git/viper/cmake-build-tsan-g3d` with:

```sh
cmake -S /Users/stephen/git/viper -B /Users/stephen/git/viper/cmake-build-tsan-g3d \
  -DCMAKE_BUILD_TYPE=Debug \
  -DIL_SANITIZE_THREAD=ON \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++
```

Targets built:

- `viper`
- `test_rt_threadpool`
- `test_rt_parallel`
- `test_rt_parallel_reduce`
- `test_rt_concurrency`
- `test_rt_g3d_commit_queue`
- `test_rt_gltf`
- `test_rt_game3d`
- `g3d_3dnext2_surface_probe`

CTest lane:

```sh
ctest --test-dir /Users/stephen/git/viper/cmake-build-tsan-g3d \
  -R '^(test_rt_threadpool|test_rt_parallel|test_rt_parallel_reduce|test_rt_concurrency|test_rt_g3d_commit_queue|test_rt_gltf|test_rt_game3d|g3d_3dnext2_surface_probe|g3d_openworld_slice_streaming_hitch_probe)$' \
  --output-on-failure \
  --timeout 120 \
  -j 1
```

Result:

- 9/9 tests passed under ThreadSanitizer.
- Covered worker pool, ordered parallel map/reduce, general runtime concurrency,
  Graphics3D main-thread commit queue, glTF/AssetHandle worker decode paths,
  Game3D worker-count parity, and the open-world streaming hitch probe.
- `test_rt_parallel_reduce` keeps the isolated child trap assertion in TSan
  builds while requiring the exact trap stderr text only in non-TSan builds,
  because Apple TSan does not preserve the normal forked child abort text.
