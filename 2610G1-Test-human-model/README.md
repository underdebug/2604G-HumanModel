# Human model viewer — QML and Vulkan

The human mesh generator and model assets are migrated from `2604G1-Test-human-model`.
`MainWindow` replaces `VolumeRendering`, using the current native Vulkan window,
depth picking, coordinate axes and rulers. The QML panel provides human creation,
height and body proportions, OBJ loading, a cube, and camera controls. Human model
coordinates are converted from metres to millimetres for the current navigation.
The source directory is preserved.

Requires Linux/X11, C++17, Qt 6.4+ Widgets/Quick/QuickControls2, GLM, Vulkan,
X11 development headers, glslangValidator, and the CUDA toolkit at
`/usr/local/cuda`. GDCM, Open3D and OpenMesh are not required.

```bash
cmake -S . -B build-human -DCMAKE_BUILD_TYPE=Debug
cmake --build build-human -j4
QT_QPA_PLATFORM=xcb ./build-human/test
```

Shaders are compiled into `build-human/shader`, and models are copied to
`build-human/models`. Keep both directories next to the executable. Model lookup
uses the executable directory first, so launching from another directory works.
If the MakeHuman OBJ is missing, the original parametric mannequin fallback is used.
Body edits use the source's approximate deformation model.

Mouse operations are preserved in `MouseTool.cpp`:

- Left drag or Shift + movement: pan.
- Right drag or Ctrl + movement: rotate (Shift takes priority).
- Wheel: zoom around the cursor.
- Alt/Meta + movement: move the rotation pivot in the view plane.
- Alt + wheel: move pivot depth.
- Alt + left double click: pick a pivot on the mesh.

`./build-human/test --validate` checks QML loading and finite human mesh geometry
without an X display. It does not exercise the Vulkan device or mouse gestures.
The top menu restores File → Open; View → Front/Right/Top View, View parameters,
Panel Left/Right; Shape → Cube; Human → Make Human; Tools → Depth Buffer;
and Test → lscm. Menus use native Qt Widgets popups alongside the QML panel,
so dropdowns can extend over the embedded Vulkan viewport. Ctrl+O opens an OBJ.
View parameters selects the camera page; Make Human restores the body controls.

Depth Buffer captures the Vulkan depth attachment, offers black/white level
sliders, Refresh, and PNG saving. LSCM runs the source solver on the packaged
`models/qian.OBJ` and displays the flattened result; intermediate output uses a
temporary directory. Headless validation also exercises view presets and panel
placement. Interactive dropdowns and GPU depth capture require a working X display.


## Cloth simulation

Choose **Human → Make Human**, then **Tools → Cloth Simulation → Wear skirt**
(or enable **Wear simulated skirt** in the QML panel). Pause/Resume freezes or
continues the simulation; Reset cloth restores its initial drape. Wind controls
lateral forcing from 0 to 4. Body parameter edits refit and reset the garment;
opening an OBJ, a cube, or an LSCM result removes the simulated garment.

This is a position-based skirt simulation with CUDA acceleration with gravity, damping, structural,
shear and bending distance constraints, and a pinned waist. A horizontal radial
envelope derived from body triangle slices keeps the skirt outside the static
body. Collision encloses both legs together and uses a small safety margin;
it is approximate and can leave gaps near concave regions. Particle self-collision is supported. Sewing, body animation, and imported
garment fitting are not implemented.
Vulkan draws the blue cloth with the body and preserves depth picking and all
existing mouse controls. The GPU vertex buffer is reused during animation.
Simulation pauses while the viewport is hidden and limits catch-up after a stall.

```bash
./build/test --validate
```

The application validation checks cloth on the supplied human mesh and the
menu/panel connections.
Interactive rendering still requires a working X11 display.


The human now wears a fitted red short-sleeved T-shirt by default. Toggle **Wear shirt**
in the QML panel or use **Tools → Cloth Simulation → Wear/Remove shirt**. The shirt
is derived from the torso surface with neck, hem and arm openings, and refits when
body parameters change. It is a fitted static layer; the blue skirt remains the
physically simulated garment. Shirt and skirt visibility are independent.

## CUDA cloth physics

CUDA is required at build time. CMake uses `/usr/local/cuda/bin/nvcc`, C++17,
CUDA17, and Debug settings for host and device debugging:

```sh
cmake -S . -B build
cmake --build build -j2
```

The runtime selects CUDA when a working NVIDIA device is available and prints
`cloth: CUDA backend active`. Without a device, or after a CUDA error, it uses
the CPU solver. Set `CLOTH_BACKEND=cpu` to force the reference CPU solver.
The build includes CUDA architectures 75, 80, 86, 87, 89, 90, 100, 110, and 120.
CUDA compilation uses `-G -g -O0`, and host compilation disables optimization
and preserves debug symbols and frame pointers.

`CudaCloth.cu` runs Verlet integration, gravity/wind, stretch/shear/bend links,
particle self-collision, sampled body/ground collision, and skirt triangle
normals/colors on CUDA. Device allocations persist until garment reset. Edge
constraints use disjoint batches to avoid concurrent writes; contacts gather
from a snapshot into a separate buffer. The CUDA solver uses a different
constraint ordering from the CPU solver, so results are not bit-identical.
Self-collision currently checks all particle pairs on the GPU; its cost grows
quadratically with particle count. The current skirt has 1,344 particles.

Body-envelope construction, initial garment topology/rest lengths, and static
T-shirt generation still run on the CPU. Vulkan rendering remains unchanged;
positions and generated mesh data are downloaded rather than using CUDA/Vulkan
buffer interop. This changes where skirt physics runs, not the fidelity of the
body-envelope or particle-based collision models.

Standalone cloth test targets and the CPU-only build stub have been removed.
Use `./build/test --validate` for the retained application validation. A successful
CUDA build alone does not validate GPU execution or performance.
