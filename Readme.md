# Face Detection on Web

This is the sample project for the medium post : [**Face Detection on Web**](https://blog.seeso.io/face-detection-on-web-tflite-wasm-simd-introduction-8b5156336fe0)
Each sample directory is correspond to each blog post.
- sample1 : __[Build TFLite & OpenCV to WASM](https://blog.seeso.io/face-detection-on-web-tflite-wasm-simd-462975e0f628)__
- sample2 : __WASM and SIMD with a Sample__ ([Korean](https://medium.com/@woang1892/5b3f0bd52d19), [English](https://medium.com/@woang1892/f5cd5cd8bb82))

## How to run

### Common
- need emsdk version=2.0.15
```
> emcc -v
emcc (Emscripten gcc/clang-like replacement + linker emulating GNU ld) 2.0.15
...
```
- Need node
- Cannot build with ARM Mac

---

### Sample1
- Simple WebAssembly running test using node

```
> cd sample1
> mkdir cmake-build-wasm
> cd cmake-build-wasm

# build with SIMD instructions
> emcmake cmake .. -DTFLITE_WITH_WASM_SIMD=ON -DCMAKE_BUILD_TYPE=Release
# build without SIMD instructions
> emcmake cmake .. -DTFLITE_WITH_WASM_SIMD=OFF -DCMAKE_BUILD_TYPE=Release

> make -j 4
...
...
[100%] Linking CXX executable WasmSample.js
[100%] Built target WasmSample

# with SIMD
> node --experimental-wasm-threads --experimental-wasm-simd --experimental-wasm-bulk-memory WasmSample.js


# without SIMD
> node --experimental-wasm-threads WasmSample.js

```

---

### Sample2
- Face Detection web demo

**Run**
```
> cd sample2
> cd app
> node main.js
Server is running on http://localhost:8000
...
# Open browser and connect to http://localhost:8000/
```
**Build and Run**
```
> cd sample2
> mkdir cmake-build-wasm
> cd cmake-build-wasm

> emcmake cmake .. -DTFLITE_WITH_WASM_SIMD=ON -DCMAKE_BUILD_TYPE=Release
> make -j 4
...
[100%] Linking CXX executable WasmSample.js
[100%] Built target WasmSample
# Move files to app dir
> mv WasmSample.js WasmSample.wasm WasmSample.worker.js ../app/wasm/simd
> emcmake cmake .. -DTFLITE_WITH_WASM_SIMD=OFF -DCMAKE_BUILD_TYPE=Release
> make -j 4
...
[ 20%] Linking CXX executable WasmSample.js
[100%] Built target WasmSample
# Move again
> mv WasmSample.js WasmSample.wasm WasmSample.worker.js ../app/wasm/nonsimd
> cd ../app
> node main.js
Server is running on http://localhost:8000
...

# Open browser and connect to http://localhost:8000/
```

---

**Demo**

<img src="./res/demo.gif" width="320" height="180">
