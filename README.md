# EarthSim

EarthSim is a C++17/OpenGL 4.3 terrain and weather simulator. It renders procedurally generated mountain ranges, water, volumetric weather and explosion clouds, particle-based lava and rain, meteors, lightning, atmospheric effects, and a day-night cycle. Explosion plumes use a localized GPU fluid volume for buoyant smoke, heat, wind advection, and a rolling mushroom-cloud cap. GLFW, GLEW, and Dear ImGui are included under `externals`.

Use **Explosion**, choose an **Explosion size**, and click the terrain to create a crater, ejecta, an expanding blast shell, and a simulated plume. The current implementation keeps one high-resolution plume active at a time; a new explosion replaces the previous plume.

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

<img width="2880" height="1704" alt="Screenshot 2026-09-05 051925" src="https://github.com/user-attachments/assets/0b48c92f-be99-49ba-9cef-bc1bc0fcad9e" />
<img width="2880" height="1800" alt="Screenshot 2026-09-05 200550" src="https://github.com/user-attachments/assets/e2a95484-1be3-4e95-8a39-b97b57a24f1e" />
