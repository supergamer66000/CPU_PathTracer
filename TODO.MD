# Features
- [ ] Logger
### Rendering
- [x] Mutithreaded rendering
- [ ] Oren-Nayar BRDF's
- [ ] BVH
- [x] Mesh
- [ ] Textures (Broke it)
- [ ] Material

### GLTF Loading
- [x] Mesh
- [ ] Textures (Broke it)
- [ ] Materials

### ImGui (NOT DONE)
- [ ] Scene graph
- [ ] Info screen
  - CPU Memory
  - CPU Usage
  - Ray Count
- [ ] Settings
  - Core count
  - BRDF estimation
  - Monte Carlo options
- [ ] Display logging
- [ ] File Bar
  - Loading scene
  - Settings

# TODO
- [x] Fix dangling pointer in engine::create_world()
- [x] Fix copies with `scene.h`

## Make add_geometry better
instead of passing
```c++
    [[maybe_unused]] entt::entity add_geometry(
        glm::vec3 position,
        std::vector<glm::vec3> vertices,
        std::vector<glm::uvec3> indices,
        std::vector<glm::vec2> tex_coords,
        glTFLoader::Texture texture,
        glTFLoader::Material material
    );
```
we can do
```c++
    struct Object {
        std::vector<glm::vec3> vertices,
        std::vector<glm::uvec3> indices,
        std::vector<glm::vec2> tex_coords,
        glTFLoader::Texture texture,
        glTFLoader::Material material
    }
    
    [[maybe_unused]] entt::entity add_geometry(
        glm::vec3 position,
        Object object
    );
```

```c++
cpt::scene create_world(entt::registry &registry) {
    auto scene = cpt::scene(registry);
    auto gltf1 = cpt::glTFLoader(...);   // <-- local, stack-allocated

    gltf1.iterator([&](...) {
    scene.add_geometry(..., *texture);
    });

    scene.commit();
    return scene;
}   // <-- gltf1 destroyed here
```
### Instead:
```c++
engine() {
    // Store the gltf in the scene rather than a helper
    scene.load(GLTFLoader loader);
    scene.get(); // <-- returns loader
}
```
Now I can make ```EmbreeUserData()``` reference  
#### model.h
```c++
struct EmbreeUserData {
    glTFLoader::Texture texture;
    glTFLoader::Material material;
};
```
#### model.cpp
```c++
auto* data = new EmbreeUserData();
data->texture = texture;
data->material = material;
```
### Instead
#### model.h
```c++
struct EmbreeUserData {
    glTFLoader::Texture *texture;
    glTFLoader::Material *material;
};
```
#### model.cpp
```c++
auto* data = new EmbreeUserData();
data->texture = &texture;
data->material = &material;
```


Fix scene pass in renderer.cpp constructor and in engine.cpp line 67