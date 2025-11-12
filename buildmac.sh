cmake --build build --target clean-all
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j8
sudo cmake --install build --prefix /usr/local
ctest --test-dir build --output-on-failure
