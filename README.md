# VulkanExp

## Todos

### Bugs
  - Probably a lot.

### Major Features
 - Scene
 - - Import model(s) INTO a scene;
 - - Save
 - - Select Nodes
 - - - Edit Material
 - - - Disable Rendering (Update BLAS/TLAS and re-record Command Buffers)
 - Use a sRGB render target
 - - Contrast is really bad right now (irradiance only start being visible when the direct lights are blinding)
 - - ImGui doesn't work well with sRGB render target, not sure how to fix it (just convert all the colors of the default style? =/)
 - Sun Direction in UBO
 - - Fix time of day scale/math
 - - Use the actual sun intensity/size for the direct lights? (it is way too high for our renderer rn)
 - - Display it in the main render path
 - Better lightning model < !
 - - Glossy Term: Correctly sample a BRDF; Generate screen reflections mip maps and pick the correct one based on material (and depth?)
 - GI. Irradiance Probes for Indirect lightning.
 - - With visibility term: https://www.gdcvault.com/play/1026182/
 - - Add a customizable bias as a uniform
 - - Optimise probe placement (not sure how yet! try moving them out of the walls (i.e. when not receiving light?), but having an offset seems rather complicated)
 - Performance Metrics (GPU)
 - Some sort of AO
 
### Improvements 
- Many

### Nice to have
 - load glTF by drag&drop?
 - Multisampling (Raster & RT)

### Major Features, but not priorities
 - Rigging

## Build

Build using Visual Studio 2022 with c++20 preview support (/std:c++latest)

## Dependencies

 - Vulkan SDK
 - GLFW 3.3.6 [https://www.glfw.org/] (included in ext/)
 - GLM 0.9.9.9 [https://glm.g-truc.net] (included in ext/)
 - Dear ImGui docking branch [https://github.com/ocornut/imgui] (included in ext/)
 - FMT 7.1.3 [https://fmt.dev/]