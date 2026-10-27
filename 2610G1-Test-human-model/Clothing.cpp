#include "Clothing.h"
#include <algorithm>
#include <cmath>
#include <array>
#include <map>
#include <glm/geometric.hpp>

std::vector<makehuman::MeshVertex> buildShirt(const std::vector<makehuman::MeshVertex> &body)
{
    if (body.empty()) return {};
    float bottom = body.front().pos.y, top = bottom;
    for (auto v : body) { bottom = std::min(bottom,v.pos.y); top = std::max(top,v.pos.y); }
    float height = top-bottom;
    if (height < 1e-6f) return {};
    const float hem = bottom+height*.545f, collar = bottom+height*.84f;
    const float width = height*.115f, clearance = height*.005f;
    // Share offsets across triangle seams, and require clearance from every
    // incident face plane. A smoothed normal alone can point nearly tangent
    // to a sharp collar face and leave the garment intersecting it.
    struct Contact { glm::vec3 offset{0}; std::vector<glm::vec3> normals; };
    using Key = std::array<float,3>;
    auto key = [](glm::vec3 p) -> Key { return {p.x,p.y,p.z}; };
    std::map<Key, Contact> contacts;
    for (size_t t=0; t+2<body.size(); t+=3) {
        auto n = glm::cross(body[t+1].pos-body[t].pos, body[t+2].pos-body[t].pos);
        float length = glm::length(n);
        if (length < 1e-10f) continue;
        n /= length;
        for (size_t i=t; i<t+3; ++i) contacts[key(body[i].pos)].normals.push_back(n);
    }
    for (auto &entry : contacts) {
        auto &contact = entry.second;
        for (int pass=0; pass<64; ++pass) {
            float violation = 0;
            for (auto n : contact.normals) {
                float missing = clearance-glm::dot(contact.offset,n);
                if (missing > 0) {
                    contact.offset += missing*n;
                    violation = std::max(violation,missing);
                }
            }
            if (violation < clearance*1e-4f) break;
        }
    }
    std::vector<makehuman::MeshVertex> result;
    // Partition at the sleeve roots so torso and sleeves meet without overlapping.
    // Oblique cuffs follow the upper arms in the model's relaxed A pose instead
    // of slicing the whole garment with two vertical planes.
    for (size_t t=0; t+2<body.size(); t+=3) {
      for (int region=0; region<3; ++region) {
        std::vector<glm::vec3> polygon;
        for (size_t i=t; i<t+3; ++i)
            polygon.push_back(body[i].pos + contacts[key(body[i].pos)].offset);
        for (int plane=0; plane<(region == 0 ? 4 : 5) && !polygon.empty(); ++plane) {
            auto distance = [=](glm::vec3 p) {
                if (plane == 0) return p.y-hem;
                if (plane == 1) return collar-p.y;
                if (region == 0) return plane == 2 ? p.x+width : width-p.x;
                float side = region == 1 ? -p.x : p.x;
                if (plane == 2) return side-width;
                if (plane == 3) return p.y-(bottom+height*.68f);
                // Cuff normal follows the upper arm, angled down from shoulder.
                return height*(-.281f) - (.8f*side-.6f*(p.y-bottom));
            };
            std::vector<glm::vec3> clipped;
            auto previous = polygon.back(); float dp=distance(previous);
            for (auto current : polygon) {
                float dc=distance(current);
                if ((dp>=0)!=(dc>=0)) clipped.push_back(previous+(current-previous)*(dp/(dp-dc)));
                if (dc>=0) clipped.push_back(current);
                previous=current; dp=dc;
            }
            polygon=std::move(clipped);
        }
        for (size_t i=1; i+1<polygon.size(); ++i) {
            auto n=glm::cross(polygon[i]-polygon[0],polygon[i+1]-polygon[0]);
            float length=glm::length(n);
            if (length<1e-10f) continue;
            n/=length;
            float lighting=.4f+.6f*std::abs(glm::dot(n,glm::normalize(glm::vec3(.4f,.8f,.5f))));
            auto color=glm::vec3(.82f,.22f,.16f)*lighting;
            for (auto p : {polygon[0],polygon[i],polygon[i+1]}) result.push_back({p,n,color});
        }
      }
    }
    return result;
}
