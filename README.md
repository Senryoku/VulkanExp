# VulkanExp

## Todos

### Bugs
  - Reflections (or just the filter?) is broken at the bottom of the screen.
  - Probably a lot.

### Major Features
 - Scene
   - Save
   - Select Nodes
     - Edit Material
     - Disable Rendering (Update BLAS/TLAS and re-record Command Buffers)
 - Sun Direction in UBO
   - Fix time of day scale/math
   - Use the actual sun intensity/size for the direct lights? (it is way too high for our renderer rn)
 - Better lightning model < !
   - Glossy Term: Correctly sample a BRDF; Generate screen reflections mip maps and pick the correct one based on material (and depth?)
 - GI. Irradiance Probes for Indirect lightning.
   - With visibility term: https://www.gdcvault.com/play/1026182/
   - Optimise probe placement (not sure how yet! try moving them out of the walls (i.e. when not receiving light?), but having an offset seems rather complicated)
   - More refined probes state (rn they're "Off", "On"), maybe re-introduce the reduce refresh-rate state?
   - Optimize update scheduling? We could provide a cpu-generated list of probes to update, rather than have instantly killed workgroups for off probes (also related to previous point).
   - Light leaks. Especially visible in the sun temple. May not be a direct consequence of the statistical nature of the probes visibility term: Walls in the sun temple are simply too thin.
 - Synchronisation. "Works on my machine", probably nowhere else because it seems I really struggle with understanding how synchronisation works in vulkan (But I'm pretty good at crashing the sync. validation layers)
 - Some sort of (small scale) AO
 
### Improvements 
- Many

### Nice to have
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
 - OpenVR (included, not used yet)