#include <cstdio>
#include <limits>

#include "nau/render/vector_lines.h"
int main()
{
    using namespace nau::render;
    SceneFrame f;
    f.drawable = {800, 600};
    f.viewProjection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    VectorPresentation p;
    p.owner = "test";
    p.world.push_back({
        { -2, 0, -2},
        {.5f, 0,  0}
    });
    f.vectors.push_back(p);
    auto mesh = tessellateVectors(f);
    if (!mesh || mesh->world.size() != 6)
        return 1;
    for (const auto& v : mesh->world)
        for (float x : v.position)
            if (!std::isfinite(x) || std::abs(x) > 1.01f)
                return 2;
    f.vectors[0].world[0] = {
        {0, 0, 0},
        {0, 0, 0}
    };
    if (!tessellateVectors(f)->world.empty())
        return 3;
    f.vectors[0].world[0].a[0] = std::numeric_limits<float>::infinity();
    if (tessellateVectors(f))
        return 4;
    f.vectors[0].world.assign(VectorPresentation::WorldLimit + 1, {});
    if (tessellateVectors(f))
        return 5;
    f.vectors[0].world.clear();
    f.vectors[0].overlay.push_back({
        {100, 100, 0},
        {200, 100, 0}
    });
    f.drawable = {1600, 600};
    mesh = tessellateVectors(f);
    if (!mesh || mesh->overlay.size() != 6 || std::abs(mesh->overlay[0].position[0] + .375f) > .01f)
        return 6;
    f.drawable = {0, 0};
    if (!tessellateVectors(f)->overlay.empty())
        return 7;
    f.vectors.clear();
    f.drawable = {800, 600};
    if (!tessellateVectors(f)->world.empty())
        return 8;
    f.vectors.push_back(p);
    f.vectors[0].world[0] = {
        {-std::numeric_limits<float>::max(), 0, 0},
        { std::numeric_limits<float>::max(), 0, 0}
    };
    if (tessellateVectors(f))
        return 9;
    std::puts("PASS vector clipping, finite data, capacity, aspect, zero size and removal");
}
