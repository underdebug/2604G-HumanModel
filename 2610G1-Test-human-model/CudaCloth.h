#pragma once
#include "makehuman.h"
#include <memory>
#include <vector>

struct ClothLink { int a, b; float length; };
// CUDA types stay private so CPU-only builds do not require the toolkit.
class CudaCloth {
public:
    virtual ~CudaCloth() = default;
    virtual bool step(float dt, float wind, float time,
                      std::vector<glm::vec3> &points, std::vector<glm::vec3> &previous) = 0;
    virtual bool mesh(std::vector<makehuman::MeshVertex> &out) = 0;
};
std::unique_ptr<CudaCloth> createCudaCloth(const std::vector<glm::vec3> &points,
    const std::vector<ClothLink> &links, const float *envelope, float bottom, float height);
