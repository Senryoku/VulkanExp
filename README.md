# VulkanExp

## Todos

### Bugs
  - TLAS update slowed down significantly? (is it really because of EnTT, or did I mess something else up?)
  - Probably a lot.

### Major Features
 - Remove vertex color?
 - Scene
   - Save (!)
     - Decide on a format...
   - Select Nodes
     - Edit Material
     - Create empty node, add 'components'
     - Delete Nodes
     - Disable Rendering (Update BLAS/TLAS and re-record Command Buffers)
   - Refer to assets with their path (avoid reloading the same one twice)
     - Refer to Materials by path/name (make sure names are unique on load)
   - Manually load textures
   - Manually create new materials
 - Reflections
   - Start by using SSR and raytrace only as a fallback (will probably need a bit of restructuring, doing SSR in a raygen shader seems to be horrible)
 - Sun Direction in UBO
   - Use the actual sun intensity/size for the direct lights? (it is way too high for our renderer rn)
 - GI. Irradiance Probes for Indirect lightning.
   - With visibility term: https://www.gdcvault.com/play/1026182/
   - Optimise probe placement (not sure how yet! try moving them out of the walls (i.e. when not receiving light?), but having an offset seems rather complicated)
   - More refined probes state (rn they're "Off", "On"), maybe re-introduce the reduce refresh-rate state?
   - Light leaks. Especially visible in the sun temple. May not be a direct consequence of the statistical nature of the probes visibility term: Walls in the sun temple are simply too thin.
 - Some sort of (small scale) AO
 
### Improvements 
- Correctly reproject reflections for temporal filtering (See http://bitsquid.blogspot.com/2017/06/reprojecting-reflections_22.html)
- Move all rendering related code to its own class (Mostly remove all rendering code from Scene)
- Many

### Nice to have
 - Multisampling (Raster & RT)

### Major Features, but not priorities
 - Skinned Meshes
   - Notes: Since the raytracing pass will require a BLAS update anyway, we'll use the underlying vertex buffer for rastering too, rather than computing the updated vertex position in a vertex shader, as we should in a raster-only pipeline.
   - FIXME: (Perf) Way too many GPU synchonisation everywhere (transfer wait queue idle).
   - TODO: Generate proper motion vectors.
   - TODO: Separate Skeleton Animation updates and Skinning.
   - TODO: Use a different mask for acceleration structure instances of dynamic meshes and static meshes. Do not disable irradiance probe inside dynamic meshes (since they're expected to move) by using this mask to avoid tracing dynamic instances on probe initialisation.
   - TODO: Support everything tied to skinning and animation with our scene format (save/load).

## Scene Format

Heavily inspired by the glTF binary format, it consists of a header, followed by a series of chunks, each with their own header.
```
struct Header {
    uint32_t magic;    // "SCEN" (0x4e454353)
    uint32_t version;
    uint32_t length;   // Total file length, in bytes.
};

struct ChunkHeader {
    uint32_t length; // Length in bytes
    uint32_t type;   // "JSON" (0x4E4F534A) or " BIN" (0x004E4942)
};
// Immediately followed by 'length' bytes of binary data.
```

## Build

Build using Visual Studio 2022 with c++20 preview support (/std:c++latest)

## Dependencies

 - Vulkan SDK
 - GLFW 3.3.6 [https://www.glfw.org/] (included in ext/)
 - GLM 0.9.9.9 [https://glm.g-truc.net] (included in ext/)
 - Dear ImGui docking branch [https://github.com/ocornut/imgui] (included in ext/)
   - ImGuizmo [https://github.com/CedricGuillemet/ImGuizmo] (included in ext/)
   - ImPlot [https://github.com/epezent/implot] (included in ext/)
 - FMT 7.1.3 [https://fmt.dev/]
 - EnTT 3.9.0 [https://github.com/skypjack/entt] (included in ext/)
