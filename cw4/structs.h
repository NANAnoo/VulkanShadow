#pragma once

#if !defined(GLM_FORCE_RADIANS)
#	define GLM_FORCE_RADIANS
#endif
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Local types/structures:
// Uniform data
namespace glsl
{
    struct SceneUniform {
        glm::mat4 M;
        glm::mat4 V;
        glm::mat4 P;
        glm::vec3 camPos;
        int light_count;
    };

    struct SpotLight {
        glm::vec4 color;
        glm::vec4 position;
        glm::vec3 direction;
        float fov;
    };
    
    template <typename T, int count>
    struct TArray {
        T array[count];
    };
}