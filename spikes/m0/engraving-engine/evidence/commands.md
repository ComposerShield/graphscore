# Reproducible Commands

Run from the repository root. Removing these ignored build directories is
intentional so the configure/build logs are clean.

```sh
cmake -E remove_directory spikes/m0/engraving-engine/build
cmake -S spikes/m0/engraving-engine -B spikes/m0/engraving-engine/build -G Ninja
cmake --build spikes/m0/engraving-engine/build
ctest --test-dir spikes/m0/engraving-engine/build --output-on-failure
spikes/m0/engraving-engine/build/engraving_spike_test --gtest_list_tests
spikes/m0/engraving-engine/build/engraving_spike_evaluator
spikes/m0/engraving-engine/build/engraving_spike_evaluator --measure

cmake -E remove_directory spikes/m0/engraving-engine/build-asan
cmake -S spikes/m0/engraving-engine -B spikes/m0/engraving-engine/build-asan -G Ninja -DSPIKE_ENABLE_ASAN=ON
cmake --build spikes/m0/engraving-engine/build-asan
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir spikes/m0/engraving-engine/build-asan --output-on-failure
ASAN_OPTIONS=detect_leaks=0 spikes/m0/engraving-engine/build-asan/engraving_spike_evaluator
ASAN_OPTIONS=detect_leaks=0 spikes/m0/engraving-engine/build-asan/engraving_spike_evaluator --measure
```

For an offline source cache, provide
`-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/absolute/path/to/googletest`; after
populating the dependency cache, add `-DFETCHCONTENT_FULLY_DISCONNECTED=ON`.
