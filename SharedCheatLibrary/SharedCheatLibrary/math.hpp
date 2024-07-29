#pragma once
#include <corecrt_math.h>
#include "vec3.hpp"

namespace math {
    float ManhattanDistance(Vector3 c1, Vector3 c2)
    {
        float dx = abs(c2.x - c1.x);
        float dy = abs(c2.y - c1.y);
        float dz = abs(c2.z - c1.z);

        return dx + dy + dz;
    }
}