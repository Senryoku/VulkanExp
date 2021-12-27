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
 - - Optimise probe placement (not sure how yet! try moving them out of the walls (i.e. when not receiving light?), but having an offset seems rather complicated)
 - - Manage probe states: Inative (in walls (will never change state or cast ray and be skipped during sampling), too far away), Asleep (Slow rate of change, will still cast rays from time to time to see if it should change state, I'll have to find a way to stagger these updates), Awake (Updated each frame)
 - - Automatic hysteris: Should start with a lower hysteris to converge faster over the first few frames. High rate of change should also decrease it (starting by using the probe state for example)
 - - Light leaks. Especially visible in the sun temple.
 - Some sort of (small scale) AO
 
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