# VulkanExp

3D renderer written to gain some experience with Vulkan, and modern rendering techniques (mainly hardware raytracing).
![image](https://user-images.githubusercontent.com/1338143/174816896-d6c1cb4f-dbf4-464f-ba7b-c82672f5e50f.png)

# Features
  - Renderer
    - Rasterisation and Raytracing hybrid: Rasterized GBuffer and additional raytraced passes for effects (shadows/reflections). 
    - [Dynamic Diffuse Global Illumination (DDGI)](https://morgan3d.github.io/articles/2019-04-01-ddgi/) : [Presentation](https://www.gdcvault.com/play/1026182/)
    - Raytraced Shadows (Single source for now, a directional sun, could be easily expanded to a second one using the same output buffer.)
      - Single ray per pixel
      - Filtered by a depth-aware gaussian kernel.
      - Accumulated over multiple frames with reprojection and history invalidation.
    - Raytraced Reflections
      - Also 1spp and filtered in a similar way as shadows. 

## Todos

### Bugs
  - Decouple the Renderer from the Editor
  - GI
    - Probes update sometimes generate a extremely bright spot, cause unknown (but disabling the sky doesn't solves the problem).
  - Probably a lot.

### Major Features
 - Remove vertex color?
 - Scene
   - Save (!)
     - Decide on a format...
     - Actually support all existing components and resources
     - Convert images to a common format? QOI?
   - Select Nodes
     - Edit Material
     - Create empty node, add 'components'
     - Delete Nodes
     - Disable Rendering (Update BLAS/TLAS and re-record Command Buffers)
   - Refer to assets with their path (avoid reloading the same one twice)
     - Refer to Materials by path/name (make sure names are unique on load)
   - Manually load textures
   - Manually create new materials
 - Motions vectors
 - Some sort of (small scale) AO. Also raytraced? or SSAO? or even baked? 
   - Also allow the use of texture AO.
 
### Improvements 
- Reflections
  - Start by using SSR and raytrace only as a fallback (will probably need a bit of restructuring, doing SSR in a raygen shader seems to be horrible)
  - Correctly reproject reflections for temporal filtering (See http://bitsquid.blogspot.com/2017/06/reprojecting-reflections_22.html, needs a depth hystory)
  - Use motions vectors for reprojection and accumulation
- Shadows
  - Sun Direction in UBO
  - Use motions vectors for reprojection and accumulation 
- GI
  - Optimise probe placement (not sure how yet! try moving them out of the walls (i.e. when not receiving light?), but having an offset seems rather complicated)
  - More refined probes state?
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
 - FileWatch [https://github.com/ThomasMonkman/filewatch] (included in ext/) For automatic shader recompilation. 
 - stb_image v2.27 [https://github.com/nothings/stb] (included in ext/)
