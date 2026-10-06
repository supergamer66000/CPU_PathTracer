//
// Created by super on 10/11/2025.
//

#ifndef INTERSECTIONS_H
#define INTERSECTIONS_H

#include "ray.h"
#include <algorithm>
#include <cmath>

namespace cpt::intersections {
    inline bool solveQuadratic(const float &a,
                               const float &b,
                               const float &c,
                               float &x0,
                               float &x1)
    {
        float discr = b * b - 4 * a * c;
        if (discr < 0) return false;
        else if (discr == 0) x0 = x1 = - 0.5 * b / a;
        else {
            float q = (b > 0) ?
                -0.5 * (b + sqrt(discr)) :
                -0.5 * (b - sqrt(discr));
            x0 = q / a;
            x1 = c / q;
        }
        if (x0 > x1) std::swap(x0, x1);

        return true;
    }

    inline bool sphere(const Ray &ray,
                       glm::vec3 center,
                       float radius, float &t,
                       glm::vec3 &normal)
    {
        float t0, t1;

        // Analytic solution
        glm::vec3 L = ray.origin - center;
        float a = glm::dot(ray.direction, ray.direction);
        float b = 2.0f * glm::dot(ray.direction, L);
        float c = glm::dot(L, L) - radius * radius;

        if (!solveQuadratic(a, b, c, t0, t1))
            return false;

        if (t0 > t1) std::swap(t0, t1);

        if (t0 < 0) {
            t0 = t1;
            if (t0 < 0)
                return false;
        }

        t = t0;

        // Compute intersection point and normal
        glm::vec3 hitPoint = ray.origin + t * ray.direction;
        normal = glm::normalize(hitPoint - center);

        return true;
    }

    inline bool box(const Ray& ray,
                              const glm::vec3& boxMin,
                              const glm::vec3& boxMax,
                              float& t0,
                              glm::vec3& normal)
    {
        glm::vec3 t1 = (boxMin - ray.origin) * (1.0f / ray.direction);
        glm::vec3 t2 = (boxMax - ray.origin) * (1.0f / ray.direction);

        glm::vec3 tmin = glm::min(t1, t2);
        glm::vec3 tmax = glm::max(t1, t2);

        float tNear = std::max(std::max(tmin.x, tmin.y), tmin.z);
        float tFar  = std::min(std::min(tmax.x, tmax.y), tmax.z);

        // Missed or behind ray origin
        if (tFar < 0.0f || tNear > tFar)
            return false;

        t0 = tNear;

        // Determine which axis was hit (to find normal)
        if (tNear == tmin.x)
            normal = glm::vec3((ray.direction.x < 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f);
        else if (tNear == tmin.y)
            normal = glm::vec3(0.0f, (ray.direction.y < 0.0f) ? 1.0f : -1.0f, 0.0f);
        else
            normal = glm::vec3(0.0f, 0.0f, (ray.direction.z < 0.0f) ? 1.0f : -1.0f);

        return true;
    }
}

#endif //INTERSECTIONS_H
