# VulkanExp

## Todos

### Major Features
 - Scene structure
 - - Uniform Buffer all Model Matrices
 - - Push Constant of an Index into this buffer
 - Better lightning model
 - Glossy lightning term by raytracing the entire screen first reflect bounce and then sampling that texture (using mipmaps for roughness approximation)
 - GI. Irradiance Probes for Indirect lightning.
 - - With visibility term: https://www.gdcvault.com/play/1026182/

### Improvements 
- RT: Discard hit on transparency.

### Nice to have
 - load glTF by drag&drop?
 - Multisampling (Raster & RT)
 - Debug display of textures in ImGui
 - Shader library (a way to #include stuff. Need to re-think the way shaders are auto-compiled, maybe simply switching to glslc, which supports #include out of the box, is enough)

### Major Features, but not priorities
 - Rigging

## Build

Build using Visual Studio 2022 with c++20 preview support (/std:c++latest)

## Dependencies

 - Vulkan SDK
 - GLFW 3.3.4
 - GLM 0.9.9.8
 - FMT 7.1.3 (https://fmt.dev/)