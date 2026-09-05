# EarthSim

EarthSim is a C++17/OpenGL 4.3 terrain and weather simulator. It renders procedurally generated mountain ranges, water, volumetric clouds, particle-based lava and rain, meteors, lightning, atmospheric effects, and a day-night cycle. GLFW, GLEW, and Dear ImGui are included under `externals`.

## Build

Requirements:

- CMake 3.20 or newer
- A C++17 compiler
- An OpenGL 4.3-capable graphics driver

Visual Studio 2022:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Single-configuration generators:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

CMake uses the bundled GLFW, GLEW, and Dear ImGui dependencies and copies the `shaders` directory beside the executable.
