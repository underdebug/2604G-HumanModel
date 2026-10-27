#pragma once
#include "makehuman.h"
#include "CudaCloth.h"
#include <array>
#include <vector>

// Position-based skirt with automatic CUDA acceleration, in metres. The waist is pinned to a static body.
class ClothSimulation {
public:
    void reset(const std::vector<makehuman::MeshVertex> &body);
    void step(float dt, float wind);
    std::vector<makehuman::MeshVertex> mesh() const;
    const std::vector<glm::vec3> &positions() const { return points; }
    bool usingCuda() const { return bool(cuda); }
    float bodyRadius(float y, float angle) const;
private:
    static constexpr int columns = 48, rows = 28, slices = 96, angles = 96;
    std::unique_ptr<CudaCloth> cuda;
    std::vector<glm::vec3> points, previous, anchors;
    std::vector<ClothLink> links;
    std::array<float, slices * angles> envelope{};
    float bottom = 0, height = 1, time = 0;
    void collide(glm::vec3 &point) const;
    void collideSelf();
};
