#ifndef UI_H
#define UI_H

#include <entt/entt.h>
#include <string>

namespace cpt {
    class renderer;
    class scene;

    class ui {
    public:
        ui(entt::registry &registry,
            renderer* renderer);

        /**
         * Shows the ImGUI windows
         */
        void show(scene &scene);
    private:
        /*
         *  Shows the current Viewport Texture
         */
        void view_port();

        /**
         *  Adjust Render settings
         */
        void render_settings();

        void scene_info(scene &scene);

        void scene_tree_node(scene& scene, entt::entity entity, entt::entity& to_unload);
        void scene_tree_asset(scene& scene, entt::entity entity, entt::entity& to_unload);
        void inspector_panel(scene& scene);
        
        entt::registry &reg;
        renderer* renderer;

        entt::entity selected_entity = entt::null;
        std::string load_error;
    };
};

#endif //UI_H
