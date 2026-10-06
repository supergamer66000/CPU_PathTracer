//
// Created by super on 09/01/2026.
//

#include "model.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include "../components.h"
#include "../types/types.h"

namespace cpt {
    model::model(RTCDevice device, RTCScene scene, entt::registry& registry)
        : device(device), scene(scene), registry(registry) {}

    void model::create(entt::entity entity) const {
        auto* mesh = registry.try_get<Mesh>(entity);
        if (!mesh) {
            throw std::runtime_error("Error Loading Model: entity has no Mesh");
        }
        if (!registry.all_of<Material>(entity)) {
            throw std::runtime_error("Error Loading Model: entity has no Material");
        }
        if (mesh->vertices.empty() || mesh->indices.empty()) {
            throw std::runtime_error("Error Loading Model: mesh has no triangles");
        }
        if (mesh->tangents)
            if (mesh->uv.size() != mesh->vertices.size() ||
                mesh->tangents.value().size() != mesh->vertices.size()) {
                throw std::runtime_error("Error Loading Model: vertex attribute size mismatch");
            }

        RTCScene object_scene = rtcNewScene(device);
        RTCGeometry geometry = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_TRIANGLE);

        rtcSetSharedGeometryBuffer(
            geometry, RTC_BUFFER_TYPE_VERTEX, 0,
            RTC_FORMAT_FLOAT3, mesh->vertices.data(), 0,
            sizeof(glm::vec3), mesh->vertices.size()
        );

        rtcSetSharedGeometryBuffer(
            geometry, RTC_BUFFER_TYPE_INDEX, 0,
            RTC_FORMAT_UINT3, mesh->indices.data(), 0,
            sizeof(glm::uvec3), mesh->indices.size()
        );

        rtcSetGeometryVertexAttributeCount(geometry, mesh->tangents ? 2 : 1);

        rtcSetSharedGeometryBuffer(
            geometry, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 0,
            RTC_FORMAT_FLOAT2, mesh->uv.data(), 0,
            sizeof(glm::vec2), mesh->uv.size()
        );

        if (mesh->tangents)
            rtcSetSharedGeometryBuffer(
                geometry, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 1,
                RTC_FORMAT_FLOAT4, mesh->tangents.value().data(), 0,
                sizeof(glm::vec4), mesh->tangents.value().size()
            );

        rtcCommitGeometry(geometry);
        [[maybe_unused]] unsigned int geoID = rtcAttachGeometry(object_scene, geometry);
        rtcReleaseGeometry(geometry); // fine — shared buffers aren't owned by the geometry handle
        rtcCommitScene(object_scene);

        // Instance the geometry so we can transform the whole object as a
        // unit rather than rewriting vertex data per-move.
        RTCGeometry instance = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_INSTANCE);
        rtcSetGeometryInstancedScene(instance, object_scene);
        rtcSetGeometryTimeStepCount(instance, 1);

        const auto* position = registry.try_get<components::Position>(entity);
        const glm::vec3 offset = position ? glm::vec3(*position) : glm::vec3(0.0f);
        float transform[12] = {
            1,0,0,offset.x,
            0,1,0,offset.y,
            0,0,1,offset.z
        };
        rtcSetGeometryTransform(instance, 0, RTC_FORMAT_FLOAT3X4_ROW_MAJOR, transform);

        auto* user_data = new EmbreeUserData();
        user_data->entity = entity;
        user_data->object_scene = object_scene;
        user_data->has_tangents = mesh->tangents.has_value();
        rtcSetGeometryUserData(instance, user_data);

        rtcCommitGeometry(instance);
        [[maybe_unused]] unsigned int instance_id = rtcAttachGeometry(scene, instance);
        rtcReleaseGeometry(instance);

        registry.patch<Mesh>(entity, [this, object_scene, instance_id, geoID](Mesh& entity_mesh) { // Update the mesh
            entity_mesh.embree_info.owner_scene = scene;
            entity_mesh.embree_info.object_scene = object_scene;
            entity_mesh.embree_info.instance = instance_id;
            entity_mesh.embree_info.geometry = geoID;
        });
    }

    void model::destroy(entt::entity entity) const {
        if (auto* mesh = registry.try_get<Mesh>(entity)) {
            if (mesh->embree_info.owner_scene != scene)
                return;
            RTCGeometry geometry = rtcGetGeometry(scene, mesh->embree_info.instance);
            delete static_cast<EmbreeUserData*>(rtcGetGeometryUserData(geometry));
            rtcDetachGeometry(scene, mesh->embree_info.instance);
            rtcReleaseScene(mesh->embree_info.object_scene);
            mesh->embree_info = {};
        }
    }

    std::optional<HitInfo> model::intersect(const Ray& ray) const {
        auto ray_ctx = RTCRayQueryContext();
        rtcInitRayQueryContext(&ray_ctx);
        auto embree_ray = RTCRayHit();

                // Origin //                                // Direction //
        embree_ray.ray.org_x = ray.origin.x;        embree_ray.ray.dir_x = ray.direction.x;
        embree_ray.ray.org_y = ray.origin.y;        embree_ray.ray.dir_y = ray.direction.y;
        embree_ray.ray.org_z = ray.origin.z;        embree_ray.ray.dir_z = ray.direction.z;

        embree_ray.ray.tnear = 0.0001f;
        embree_ray.ray.tfar = 1e20f;
        embree_ray.ray.mask = -1;
        embree_ray.hit.geomID = RTC_INVALID_GEOMETRY_ID;

        auto args = RTCIntersectArguments();
        rtcInitIntersectArguments(&args);
        args.flags = RTC_RAY_QUERY_FLAG_NONE;
        args.feature_mask = static_cast<RTCFeatureFlags>(
            RTC_FEATURE_FLAG_INSTANCE | RTC_FEATURE_FLAG_TRIANGLE);
        args.context = &ray_ctx;
        args.filter = nullptr;
        args.intersect = nullptr;

        rtcIntersect1(scene, &embree_ray, &args);

        return instance(
            embree_ray.hit.instID[0], embree_ray.hit.geomID, embree_ray.hit.primID,
            embree_ray.hit.u, embree_ray.hit.v, embree_ray.ray.tnear, embree_ray.ray.tfar,
            glm::vec3(embree_ray.hit.Ng_x, embree_ray.hit.Ng_y, embree_ray.hit.Ng_z)
        );
    }

    std::array<std::optional<HitInfo>, 4> model::intersect4(const std::array<Ray, 4> &rays, int active_count) const {
        alignas(32) int valid[4];
        alignas(32) RTCRayHit4 packet{};

        for (int i = 0; i < 4; ++i) {
            valid[i] = (i < active_count) ? -1 : 0;
            packet.ray.org_x[i] = rays[i].origin.x;
            packet.ray.org_y[i] = rays[i].origin.y;
            packet.ray.org_z[i] = rays[i].origin.z;
            packet.ray.dir_x[i] = rays[i].direction.x;
            packet.ray.dir_y[i] = rays[i].direction.y;
            packet.ray.dir_z[i] = rays[i].direction.z;
            packet.ray.tnear[i] = 0.0001f;
            packet.ray.tfar[i]  = 1e20f;
            packet.ray.mask[i]  = -1;
            packet.hit.geomID[i] = RTC_INVALID_GEOMETRY_ID;
        }

        RTCRayQueryContext ray_ctx;
        rtcInitRayQueryContext(&ray_ctx);

        RTCIntersectArguments args;
        rtcInitIntersectArguments(&args);
        args.context = &ray_ctx;
        args.flags = RTC_RAY_QUERY_FLAG_COHERENT;
        args.feature_mask = static_cast<RTCFeatureFlags>(
            RTC_FEATURE_FLAG_INSTANCE | RTC_FEATURE_FLAG_TRIANGLE);

        rtcIntersect4(valid, scene, &packet, &args);

        std::array<std::optional<HitInfo>, 4> results;
        for (int i = 0; i < active_count; ++i) {
            if (packet.hit.geomID[i] == RTC_INVALID_GEOMETRY_ID) {
                results[i] = std::nullopt;
                continue;
            }
            results[i] = instance(
                packet.hit.instID[0][i], packet.hit.geomID[i], packet.hit.primID[i],
                packet.hit.u[i], packet.hit.v[i],
                packet.ray.tnear[i], packet.ray.tfar[i],
                glm::vec3(packet.hit.Ng_x[i], packet.hit.Ng_y[i], packet.hit.Ng_z[i]));
        }
        return results;
    }

    std::array<std::optional<HitInfo>, 8> model::intersect8(const std::array<Ray, 8> &rays, int active_count) const {
        alignas(32) int valid[8];
        alignas(32) RTCRayHit8 packet{};

        for (int i = 0; i < 8; ++i) {
            valid[i] = (i < active_count) ? -1 : 0;
            packet.ray.org_x[i] = rays[i].origin.x;
            packet.ray.org_y[i] = rays[i].origin.y;
            packet.ray.org_z[i] = rays[i].origin.z;
            packet.ray.dir_x[i] = rays[i].direction.x;
            packet.ray.dir_y[i] = rays[i].direction.y;
            packet.ray.dir_z[i] = rays[i].direction.z;
            packet.ray.tnear[i] = 0.0001f;
            packet.ray.tfar[i]  = 1e20f;
            packet.ray.mask[i]  = -1;
            packet.hit.geomID[i] = RTC_INVALID_GEOMETRY_ID;
        }

        RTCRayQueryContext ray_ctx;
        rtcInitRayQueryContext(&ray_ctx);

        RTCIntersectArguments args;
        rtcInitIntersectArguments(&args);
        args.context = &ray_ctx;
        args.flags = RTC_RAY_QUERY_FLAG_COHERENT;
        args.feature_mask = static_cast<RTCFeatureFlags>(
            RTC_FEATURE_FLAG_INSTANCE | RTC_FEATURE_FLAG_TRIANGLE);

        rtcIntersect8(valid, scene, &packet, &args);

        std::array<std::optional<HitInfo>, 8> results;
        for (int i = 0; i < active_count; ++i) {
            results[i] = instance(
                packet.hit.instID[0][i], packet.hit.geomID[i], packet.hit.primID[i],
                packet.hit.u[i], packet.hit.v[i],
                packet.ray.tnear[i], packet.ray.tfar[i],
                glm::vec3(packet.hit.Ng_x[i], packet.hit.Ng_y[i], packet.hit.Ng_z[i]));
        }
        return results;
    }

    std::array<std::optional<HitInfo>, 16> model::intersect16(const std::array<Ray, 16> &rays, int active_count) const {
        alignas(64) int valid[16];
        alignas(64) RTCRayHit16 packet{};

        for (int i = 0; i < 16; ++i) {
            valid[i] = (i < active_count) ? -1 : 0;
            packet.ray.org_x[i] = rays[i].origin.x;
            packet.ray.org_y[i] = rays[i].origin.y;
            packet.ray.org_z[i] = rays[i].origin.z;
            packet.ray.dir_x[i] = rays[i].direction.x;
            packet.ray.dir_y[i] = rays[i].direction.y;
            packet.ray.dir_z[i] = rays[i].direction.z;
            packet.ray.tnear[i] = 0.0001f;
            packet.ray.tfar[i]  = 1e20f;
            packet.ray.mask[i]  = -1;
            packet.hit.geomID[i] = RTC_INVALID_GEOMETRY_ID;
        }

        RTCRayQueryContext ray_ctx;
        rtcInitRayQueryContext(&ray_ctx);

        RTCIntersectArguments args;
        rtcInitIntersectArguments(&args);
        args.context = &ray_ctx;
        args.flags = RTC_RAY_QUERY_FLAG_COHERENT;
        args.feature_mask = static_cast<RTCFeatureFlags>(
            RTC_FEATURE_FLAG_INSTANCE | RTC_FEATURE_FLAG_TRIANGLE);

        rtcIntersect16(valid, scene, &packet, &args);

        std::array<std::optional<HitInfo>, 16> results;
        for (int i = 0; i < active_count; ++i) {
            results[i] = instance(
                packet.hit.instID[0][i], packet.hit.geomID[i], packet.hit.primID[i],
                packet.hit.u[i], packet.hit.v[i],
                packet.ray.tnear[i], packet.ray.tfar[i],
                glm::vec3(packet.hit.Ng_x[i], packet.hit.Ng_y[i], packet.hit.Ng_z[i]));
        }
        return results;
    }

    std::optional<HitInfo> model::instance(unsigned instID, unsigned geomID, unsigned primID,
                               float u, float v, float tnear, float tfar, glm::vec3 Ng) const {
        if (geomID == RTC_INVALID_GEOMETRY_ID)
            return std::nullopt;

        HitInfo hit_info{};
        auto instance_geom = rtcGetGeometry(scene, instID);
        auto user_ptr = static_cast<EmbreeUserData*>(rtcGetGeometryUserData(instance_geom));
        hit_info.material = user_ptr->entity;
        hit_info.t_near = tnear;
        hit_info.t_far = tfar;
        hit_info.normal = glm::normalize(Ng);

        auto data_geom = rtcGetGeometry(user_ptr->object_scene, geomID);

        rtcInterpolate0(data_geom, primID, u, v,
            RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 0, &hit_info.uv.x, 2);

        if (user_ptr->has_tangents) {
            hit_info.tangent.emplace(0.0f);
            rtcInterpolate0(data_geom, primID, u, v,
                RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 1, &hit_info.tangent->x, 4);
        } else {
            hit_info.tangent.reset();
        }

        glm::vec3 dPdu, dPdv;
        rtcInterpolate1(data_geom, primID, u, v, RTC_BUFFER_TYPE_VERTEX, 0,
            nullptr, &dPdu.x, &dPdv.x, 3);
        glm::vec2 dUVdu, dUVdv;
        rtcInterpolate1(data_geom, primID, u, v, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE, 0,
            nullptr, &dUVdu.x, &dUVdv.x, 2);

        hit_info.hit = true;
        const float world_area2 = glm::length(glm::cross(dPdu, dPdv));
        const float uv_area2 = std::abs(dUVdu.x * dUVdv.y - dUVdu.y * dUVdv.x);
        hit_info.uv_density = world_area2 > 1e-12f ? (uv_area2 / world_area2) : 0.0f;

        return hit_info;
    }

    bool model::occluded(Ray ray, float max_distance) const {
        RTCRay shadow{};
        shadow.org_x = ray.origin.x;
        shadow.org_y = ray.origin.y;
        shadow.org_z = ray.origin.z;
        shadow.dir_x = ray.direction.x;
        shadow.dir_y = ray.direction.y;
        shadow.dir_z = ray.direction.z;
        shadow.tnear = 0.0001f;
        shadow.tfar = max_distance;
        shadow.mask = -1;

        RTCRayQueryContext context{};
        rtcInitRayQueryContext(&context);
        RTCOccludedArguments args{};
        rtcInitOccludedArguments(&args);
        args.context = &context;
        args.feature_mask = static_cast<RTCFeatureFlags>(
            RTC_FEATURE_FLAG_INSTANCE | RTC_FEATURE_FLAG_TRIANGLE);
        rtcOccluded1(scene, &shadow, &args);
        return shadow.tfar < 0.0f;
    }
} // cpt
