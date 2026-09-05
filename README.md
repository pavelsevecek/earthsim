# EarthSim

A C++17 / OpenGL 4.3 mountain landscape using the bundled GLFW and Dear ImGui.
Each launch generates a random terrain seed and uses it to create three to five
mountain ranges. Every range has a randomized center, compass orientation, length,
width, curvature, phase, and amplitude. The strongest local range envelope shapes
seven octaves of seeded ridged gradient noise and domain warping. Height and slope
determine grass, rock, and snow.
Two layers of smoothly interpolated value noise vary the terrain materials without
visible square hash cells.
Terrain elevation is scaled to half its original height for gentler slopes.
The sky, sunlight, twilight, moonlight, stars, and distance haze follow a continuous
day-night cycle: **one simulated day takes 60 real seconds at 1× speed**.
Stars cover the full sky sphere and rotate with the sun and moon. Daylight and
atmosphere opacity control their visibility; terrain and clouds still occlude them.
At night, procedural aurora curtains shimmer around the northern sky. Broad
animated folds, layered turbulent noise, fast rippling vertical rays, and changing intensity form green
lower emissions that transition toward violet at high elevation. The aurora is
fixed to world-space magnetic north, fades through twilight, disappears during
the day, and feeds the existing HDR bloom pass. Terrain and clouds occlude it.
Mountains cast shadows from both sunlight and moonlight, using two 2048² depth
maps updated as the lights move. Filtered depth comparisons soften shadow edges;
ambient lighting remains visible in shadow. Shadow coverage spans the entire
terrain independently of camera position. The shadow shaders are external files.

Random lightning strikes form complete cloud-to-ground channels in a single
frame, approximating instantaneous propagation. Each strike follows a jagged main
path from elevation 370–500 to the terrain and adds one to four shorter branches.
The HDR lines feed the bloom pass, remain depth-occluded by terrain, hold near full
brightness briefly, and fade out by 400 simulation milliseconds. The **Lightning
frequency** slider ranges from 0 to 60 expected strikes per simulation minute and
defaults to 6. Strike timing follows a Poisson distribution, so the spacing is
irregular rather than periodic. Pausing or changing time speed affects both strike
frequency and lifetime.

Each launch starts with no volcanic vents or springs. Click **Create volcano** or
**Create spring**, then left-click the terrain to place the selected source.
Repeat to add more. Placement uses the
visible terrain depth, so the nearest mountain surface receives the source.
Clicking empty sky keeps placement active. **Escape** or **Cancel placement**
cancels; otherwise Escape exits. Right-drag rotation and scroll zoom remain
available during placement. Vents and springs are session-only and are not saved on exit.

**Terrain up** and **Terrain down** activate persistent sculpting tools. The
**Terrain brush radius** slider adjusts their radius from 10 to 300 world units
and defaults to 85. Each terrain click changes the center by 18 elevation units
and tapers continuously to zero at the brush edge. The mesh heights,
normals, particle collision texture, terrain bounds, and attached volcano and
spring elevations update immediately. Escape or **Cancel placement** exits the
active sculpting tool; terrain edits remain for the current session.

Each vent
continuously emits 90 hot particles per second as compact emissive disks.
Placed sources sit 2 world units above the terrain. Each emitted particle is also
raised beyond its slope-adjusted collision radius, preventing terrain projection
from imparting an artificial upward displacement on its first simulation step.
Volcano particles and impact ejecta are opaque; cooling dims their emission rather
than making their surfaces transparent. They test and write scene depth. Meteor
trails remain additive.
Each spring emits 90 cool water particles per second. Water uses the same GPU
gravity, terrain contact, friction, and position-based fluid interactions as lava,
including interaction with nearby lava particles. It renders as a blue,
semi-transparent surface and writes no bloom or SSGI emission. Water uses a fixed
global upward normal, making every particle behave like a small horizontal surface
for Fresnel sky reflections and sunlight highlights. Reflected daylight fades with
the day-night cycle, so water becomes almost black at night.
A flat water plane spans the terrain and defaults to elevation 0. The **Water
level** slider moves it from -100 to 250 world units. Terrain depth creates the
visible shoreline, while transparency leaves submerged terrain visible. The plane
uses a half-resolution HDR planar reflection rendered from a camera mirrored
across the current water level. Reflected terrain is clipped at the water plane
and reuses the primary terrain shadows and wetness data. The procedural sky,
stars, sun, moon, and aurora are reflected directly. A small animated distortion
breaks up the mirror image, Fresnel weighting strengthens reflections at grazing
angles, and the procedural sky remains as a fallback near reflection-texture
edges. Water particles sample the same planar reflection using their fixed upward
normal. Active lightning channels are drawn again with the mirrored camera,
clipped at the water surface, and retain their HDR intensity so their reflections
feed bloom. Particle simulation is not repeated; rain and particle geometry reuse
their existing simulation state. Lava, water, vapor,
meteor bodies and trails, rain streaks, rain vapor, and ripples are drawn with the
mirrored camera and clipped at the water surface. Water particles use their
procedural sky fallback during capture to avoid reading from the reflection target
while it is being written. Volumetric clouds remain omitted from the reflected
render to keep its cost bounded.
Water transparency uses the full unpolarized dielectric Fresnel equations with
an air refractive index of 1.0 and water refractive index of 1.333. The interface
therefore reflects about 2% at normal incidence and approaches complete reflection
at grazing angles. Views from below also account for total internal reflection.
Fresnel reflection is combined with Beer-Lambert-style transmission loss and
blue in-scattering through an approximate water column. This keeps the interface
physics intact while making the deep water plane mostly blue during daylight;
water particles use a thinner optical column and remain more transparent.
Procedural shoreline foam follows the live GPU terrain-height texture, including
sculpted terrain and meteor craters. A shallow-depth band combines two scales of
advected noise with moving wave fronts, producing broken white foam that becomes
denser and more opaque near contact while remaining dimly moonlit at night.
When a particle crosses below the adjustable water level, spring-water particles
merge into the water body and disappear. Lava and meteor-impact ejecta instead
convert immediately into white, rising vapor; their vapor age restarts at the
moment of conversion and they leave the fluid solver.
When a water particle touches a lava particle, the GPU spatial-hash pass converts
the water into vapor. Vapor is white and semi-transparent, expands as it ages,
and rises vertically at a constant 6 units per simulation second. It expands by
8% of its spawn size per simulation second, capped at three times its original
size. Its opacity is strongest at the center and falls smoothly to zero at the
edge. Vapor ignores gravity, terrain collision, and all particle-fluid
interactions; its age restarts at conversion and it uses the particle-life
setting. A spatially varying wind accelerates vapor sideways. Wind strength uses
absolute elevation as a terrain-distance approximation: it is zero below 20 units
and ramps to full strength by 260 units, with light horizontal drag limiting speed.
Vapor samples the sunlight layer of the existing terrain shadow map with a 3x3
filtered comparison. Fully illuminated vapor stays bright during the day, mountain
shadows reduce it to a small ambient contribution, and nighttime reduces it to a
faint 2.5% base level.
Cloud-vapor sorting uses a temporary rendering-order approximation. Below the
cloud base (elevation 290), vapor renders after the cloud composite and reuses
terrain depth for mountain occlusion, placing nearby vapor in front of the clouds.
At or above the cloud base, vapor renders before the cloud composite.
Screen-space bloom spreads emission into a soft glow. Gravity pulls them downhill; collision against
the rendered terrain triangles repels them from the surface, with almost no bounce
and strong sliding friction. Particle life defaults to 33 simulation seconds and
can be adjusted from 1–120 seconds. Particle emission fades during the final 20% of their
lifetime; cooling can make them invisible earlier. The particle pool is capped
at 16,000, so long lifetimes can temporarily limit new emissions.
Physics uses 120 Hz steps and limits catch-up after stalls to avoid long freezes.

Volcano and impact particles share a GPU position-based fluid solver implemented
as OpenGL compute-shader passes. Three density-constraint iterations, a small
anti-clumping pressure, viscosity, gravity, integration, terrain collision,
friction, lifetime, and cooling all operate directly on shader-storage buffers.
Terrain contact is enforced after every position correction. Isolated spray
remains ballistic. A GPU spatial hash with 4-unit cells searches the surrounding
27 cells and filters by exact distance; it is rebuilt each solver iteration so
moving neighbors are not missed. Hash collisions are checked against full cell
coordinates. The solver uses equal particle masses and fixed physical spacing;
sprite sizes affect appearance and cooling. Meteor-trail particles use the same
GPU integration path with gravity, terrain collision, and fluid interaction flags
disabled. The CPU only creates and uploads each particle's initial spawn record.
Based on
[Position Based Fluids](https://mmacklin.com/pbf_sig_preprint.pdf).
The **Particle interactions** checkbox defaults to on. Turning it off skips
density constraints, neighbor searches, and inter-particle viscosity while
preserving gravity, terrain collisions, friction, cooling, particle lifetimes,
and the lava-water phase-change query.

Click **Meteor strike**, then click the terrain to target an impact. A glowing
meteor starts 700 units above the target, approaches from a random compass direction
at 15–55° from vertical, and travels at 650–900 units per simulation second.
It aims at the selected point and impacts the first terrain surface along its path.
Swept triangle collision tests prevent fast diagonal strikes from tunneling through ridges.
Its trail drifts without gravity or terrain collisions and fades after 1.5–3
simulation seconds, independently of the particle-life slider. Trail emission
creates 600 particles per simulation second. Each trail particle expands to three
times its initial radius while its very bright teal emission transitions to red
as it fades. Impact ejects
1,200 hot particles at 8–20 units/s, with random azimuths and a fixed 45° angle
from global vertical (+Y), independent of the terrain normal. These use the volcano
gravity, friction, cooling, and lifetime settings. The impact excavates a shallow
spherical cap with radius 65 and depth 19.5, blending the rim into the surrounding
terrain without raising existing ground. Terrain normals, collision heights, picking, existing
vent positions, and mountain shadows update with the crater. Strikes can overlap;
terrain edits last for the current session. Time speed also affects meteors and trails.
Particles spawn with small random velocities (±1.2 units/s on each horizontal
axis and 0–1.5 units/s upward), with gravity at 12 units/s². Terrain collisions retain
only 1% of inward speed as rebound and damp tangential velocity at 1.5 per second.
Water uses one fifth of that terrain friction, damping tangential velocity at 0.3
per second, while lava keeps the original value.

Particles start at 1450–1650 K and cool exponentially toward 300 K with a base
time constant of 180 simulation seconds; larger particles cool more slowly. Glow
uses a cached integration of Planck's spectrum
against approximate CIE 1931 color-matching functions, converted to linear sRGB.
A fixed exposure lets the emission redden and dim naturally as temperature falls.
Sky, terrain, particles, and clouds remain in linear HDR through compositing.
Bloom uses a full HDR mip pyramid down to the first level smaller than 4 pixels,
with the same weighted 13-tap downsample and texel-sized 3x3 tent upsample as
SpaceSim's `BloomEffect`. Bright single-pixel sources retain their energy during
the first reduction instead of being suppressed by luminance weighting. It also
follows the approach in
[LearnOpenGL's physically based bloom article](https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom).
The accumulated pyramid is added to the scene at 4% before a single final tone-map
and sRGB conversion. The UI is drawn afterward
and does not bloom. Bloom also works with clouds disabled, resizes with the window,
and uses external shader files; particle shaders no longer draw artificial halos.
The **Bloom** checkbox defaults to on. Disabling it skips all pyramid passes while
retaining the same HDR tone mapping and sRGB conversion for the base scene.
The **Camera exposure** slider ranges from -5 to +5 EV and defaults to 0 EV. Its
power-of-two exposure multiplier is applied to the combined HDR scene and bloom
immediately before tone mapping.
The **Bloom intensity** slider sits below exposure, ranges from 0x to 2x, and
defaults to 1x, which preserves the original 4% bloom contribution.
The **Particle brightness** slider scales emission from 0× to 10× (default 1×)
before bloom. It affects volcano particles, impact ejecta, meteor trails, meteors,
and vent markers without changing cooling or physics.

Hot particles illuminate nearby visible terrain through a half-resolution
screen-space global-illumination pass. Particle emission is written to a separate
HDR buffer and mipmapped. Centered, trilinear samples across seven mip levels form
a continuous irradiance field without discrete offset copies of each particle.
Depth-aware upsampling adds
it before clouds, bloom, and tone mapping. This screen-space method illuminates
only surfaces and emitters visible to the camera, so off-screen or fully occluded
particles cannot contribute and disoccluded regions may change as the camera moves.
Particle-terrain contact uses each rendered disk's radius, and vent markers are
raised by their full radius, preventing coplanar depth fighting with the landscape.
The **SSGI** checkbox defaults to off. When disabled, EarthSim skips the emission
attachment, mip generation, indirect-light render, and depth-aware composite pass.
This is a visual cooling model, not a full lava heat-transfer simulation.
References: [Planck radiation](https://www.pbr-book.org/4ed/Radiometry%2C_Spectra%2C_and_Color/Light_Emission)
and [CIE function approximations](https://jcgt.org/published/0002/02/01/).

Volumetric clouds drift above the mountains between elevations 290 and 530.
Their density comes from layered, smoothly interpolated procedural 3D noise.
A prevailing wind advects the cloud field at 10 world units per simulation
second in the same average direction used by the vapor wind force.
A 64-step ray march integrates light and opacity with short shadow rays toward
the sun or moon. Clouds render at half resolution and are composited using scene
depth to preserve terrain silhouettes, including when viewed from above or inside
the cloud layer. The cloud buffers resize with the window. Cloud shaders are
external files alongside the terrain, sky, and particle shaders.

Rain uses a separate pool of 32,768 GPU particles. Inactive drops respawn within
a 1,800-unit camera-centered area only where the shared procedural cloud-density
field is sufficiently dense. Falling drops accelerate downward, approach a
wind-driven horizontal velocity, and render as thin motion-aligned streaks. They
do not enter the position-based fluid solver or its neighbor searches.

Drops sample the GPU terrain height texture and the adjustable water plane. Water
plane impacts become expanding one-second ripples. Terrain impacts atomically add
moisture to a 256x256 GPU wetness field, which decays over 45 simulation seconds;
the terrain shader reads it directly to darken wet ground and add a tight sunlight
reflection. Drops that contact nearby lava become white, non-interacting vapor in
the rain pool for three seconds. The **Rain intensity** slider controls spawning
from 0 to full density. Disabling clouds stops new rain while existing drops finish
falling, and the shared time-speed control affects all rain behavior.

## Build and run

Requires CMake 3.20+, a C++17 compiler, and an OpenGL 4.3-capable graphics driver.
Dependencies are taken from `externals/glfw` and `externals/imgui`; no downloads
are performed by the project. Linux additionally needs the development packages
for the GLFW window-system backends enabled on that machine.

For example, with Visual Studio 2022 installed, run from the project directory:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\EarthSim.exe
```

For a single-configuration generator:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/EarthSim
```

CMake copies `shaders/` beside the executable. Keep this folder with EarthSim
when moving the app. All shaders, including the ImGui and particle shaders, are loaded
from text files and compiled at startup. Read/compile/link failures are reported
to the console with the shader name and driver log. The working directory's
`shaders/` folder is used as a fallback if no adjacent folder exists.

## Controls

- EarthSim starts fullscreen on the primary monitor at its current resolution
  and refresh rate.
- **Quit:** the button in the upper-right corner closes EarthSim.
- **Left mouse drag:** pan the camera.
- **Left mouse double-click:** set the camera target to the visible terrain point.
- **Right mouse drag:** rotate around the current camera target.
- **Mouse wheel / trackpad scroll:** zoom.
- **Escape:** exit.

The overlay displays simulated time, controls, and atmosphere opacity and time-speed sliders.
The **Clouds** checkbox toggles the cloud layer independently of atmosphere opacity.
Clouds default to on; disabling them skips cloud rendering and compositing.
The **Cloud coverage** slider shifts the procedural density threshold from sparse
cloud fragments at 0 to broad overcast coverage at 1. Rain spawning uses the same
threshold, so precipitation tracks the visible cloud field.
The particle-life slider sets the lifetime of both existing and new particles
in simulation seconds. Shortening it removes particles already older than the
new limit on the next simulation step. Cooling rates are independent of this setting.
The **Time of day** slider directly edits the 24-hour day-night phase. The clock
continues advancing from the selected time when simulation time is running and
holds the selected time while paused, without changing particle ages, physics,
cloud positions, or weather animation state. Time speed ranges from 0× (paused)
to 4×, with 1× as the default. It scales the
day-night cycle, cloud drift, particle motion, emission, cooling, and lifetimes
together. Camera controls remain responsive while paused. Changing speed preserves
the current simulation time without jumping to a different time of day.
Opacity ranges from 0 (no sky atmosphere, clouds, or terrain haze) to 1 (full atmosphere).
Terrain, particle, and cloud haze keep the first 300 world units clear. Beyond
that distance, optical depth grows with distance to the power 2.4 before entering
the exponential transmittance function. This preserves nearby and middle-distance
contrast while making the far landscape become opaque quickly. Atmosphere opacity
scales this optical depth. Daytime starlight uses a separate strong exponential
extinction, leaving less than 0.005% visible at full daylight and atmosphere.
Camera zoom has no fixed distance limits, and the camera can orbit below the
horizon and through the terrain. The shared clock scales elapsed real time and
continues while minimized. Stall recovery is capped at 0.1 real seconds per frame
before speed scaling, so all effects slow together during prolonged stalls.

## Verification

The Debug C++ target builds successfully with Visual Studio 2022. A 10-second
inclined-plane check of the particle integration/contact equations confirmed
downhill movement without surface penetration. These checks
do not replace running the application with a working graphics context.
Cloud intersection checks cover cameras above, below, and inside the volume,
parallel rays, and perspective depth reconstruction. GPU output and performance
have not been verified.
Meteor numerical checks cover the crater cross-section, unchanged terrain
outside the crater, excavation without raising ground, the original vertical descent,
and constant-velocity trail drift. Runtime checks should also include overlapping
impacts, impacts near terrain edges or existing vents, and pause/resume in flight.
The compute and particle shaders were copied into the Debug output, but could not
be driver-compiled in the noninteractive verification environment. Their runtime
behavior and performance with the full particle pool remain unverified.

When running locally, check that the scene starts with no vents, placing a vent
creates continuous emissions, UI/sky clicks do not create vents, particles settle
and flow downhill, and terrain occludes particles behind mountains. Also check
camera rotation and zoom, resize and
minimize/restore, and observe a complete sunrise-to-sunrise cycle over 60 seconds.
Inspect clouds from above and below, pan into the layer, and check terrain edges
and the atmosphere slider at both endpoints.
To check startup diagnostics, temporarily rename a shader and restart the app.
