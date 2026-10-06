#ifndef RANDOM_H
#define RANDOM_H

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <random>
#include <ziggurat.hpp>
#include <cmath>

namespace cpt {

    template<typename T>
    class random_distribution {
    public:
        explicit random_distribution(glm::vec<2, T> distribution = {T{0}, T{1}})
            : uniform_dist(distribution.x, distribution.y),
              normal(distribution.x, distribution.y)
        {
            std::random_device rd;
            rng.seed(rd());
        }

        T uniform()
        {
            return uniform_dist(rng);
        }

        glm::vec<2, T> uniform2()
        {
            return {
                uniform(),
                uniform()
            };
        }

        glm::vec<3, T> uniform3()
        {
            return {
                uniform(),
                uniform(),
                uniform()
            };
        }

        T normal_random()
        {
            return normal(rng);
        }

        glm::vec<2, T> normal2()
        {
            return {
                normal_random(),
                normal_random()
            };
        }

        glm::vec<3, T> normal3()
        {
            return {
                normal_random(),
                normal_random(),
                normal_random()
            };
        }

        struct FrisVadInfo {
            glm::vec<3, T> F;
            glm::vec<3, T> R;
        };

        FrisVadInfo frisvad(glm::vec<3, T> N)
        {
            FrisVadInfo info{};
            if (N.z < T{-0.999999})
            {
                info.F = glm::vec<3, T>{0, -1, 0};
                info.R = glm::vec<3, T>{-1, 0, 0};
            }
            else
            {
                const T a = T{1} / (T{1} + N.z);
                const T b = -N.x * N.y * a;
                info.F = {
                    T{1} - N.x * N.x * a,
                    b,
                    -N.x
                };
                info.R = {
                    b,
                    T{1} - N.y * N.y * a,
                    -N.y
                };
            }
            return info;
        }

        struct SampleHemisphereInfo {
            glm::vec<3, T> direction;
            T pdf;
        };

        SampleHemisphereInfo sample_uniform_hemisphere(glm::vec<3, T> N)
        {
            SampleHemisphereInfo info{};
            const auto rand = uniform2();
            const T z = rand.x;
            const T r = glm::sqrt(
                glm::max(T{0}, T{1} - z * z)
            );
            const T phi = glm::two_pi<T>() * rand.y;

            const T x = r * glm::cos(phi);
            const T y = r * glm::sin(phi);

            const auto fris = frisvad(N);

            info.pdf = glm::one_over_two_pi<T>();
            info.direction =
                x * fris.F +
                y * fris.R +
                z * N;

            return info;
        }

    private:
        std::mt19937 rng;
        std::uniform_real_distribution<T> uniform_dist;
        cxx::ziggurat_normal_distribution<T> normal;
    };

}

#endif // RANDOM_H