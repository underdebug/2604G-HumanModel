#include "ClothSimulation.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <glm/geometric.hpp>

namespace {
constexpr float pi = 3.14159265358979323846f;
float cross2(glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; }
}
void ClothSimulation::reset(const std::vector<makehuman::MeshVertex> &body)
{
    cuda.reset();
    points.clear(); previous.clear(); anchors.clear(); links.clear(); envelope.fill(0); time = 0;
    if (body.empty()) return;
    bottom = body.front().pos.y;
    float top = bottom;
    for (const auto &v : body) { bottom = std::min(bottom, v.pos.y); top = std::max(top, v.pos.y); }
    height = std::max(top - bottom, 0.1f);
    // Slice triangles into horizontal segments, then intersect radial rays.
    // This gives a conservative skirt collider enclosing the hips and both legs.
    for (size_t t = 0; t + 2 < body.size(); t += 3) {
        glm::vec3 v[3] = {body[t].pos, body[t+1].pos, body[t+2].pos};
        float lo = std::min({v[0].y, v[1].y, v[2].y});
        float hi = std::max({v[0].y, v[1].y, v[2].y});
        int first = std::clamp(int(std::ceil((lo-bottom)/height*(slices-1))), 0, slices-1);
        int last = std::clamp(int(std::floor((hi-bottom)/height*(slices-1))), 0, slices-1);
        for (int s = first; s <= last; ++s) {
            float y = bottom + height * s / (slices-1);
            std::vector<glm::vec2> hits;
            for (int e = 0; e < 3; ++e) {
                const auto a = v[e], b = v[(e+1)%3];
                if (std::abs(b.y-a.y) < 1e-8f) continue;
                float f = (y-a.y)/(b.y-a.y);
                if (f >= 0 && f <= 1) { auto p = a + f*(b-a); hits.emplace_back(p.x, p.z); }
            }
            if (hits.size() < 2) continue;
            auto a = hits[0], edge = hits[1]-a;
            for (int k = 0; k < angles; ++k) {
                float angle = 2*pi*k/angles;
                glm::vec2 ray(std::cos(angle), std::sin(angle));
                float d = cross2(ray, edge);
                if (std::abs(d) < 1e-8f) continue;
                float r = cross2(a, edge)/d, f = cross2(a, ray)/d;
                if (r >= 0 && f >= 0 && f <= 1)
                    envelope[s*angles+k] = std::max(envelope[s*angles+k], r);
            }
        }
    }
    // Fill isolated angular gaps conservatively and pad sampling error.
    auto original = envelope;
    for (int s = 0; s < slices; ++s) for (int k = 0; k < angles; ++k) {
        float radius = 0;
        for (int ds = -1; ds <= 1; ++ds) for (int dk = -1; dk <= 1; ++dk)
            radius = std::max(radius, original[std::clamp(s+ds,0,slices-1)*angles+(k+dk+angles)%angles]);
        envelope[s*angles+k] = radius;
    }
    const float waist = bottom + height * 0.57f;
    for (int j = 0; j < rows; ++j) for (int k = 0; k < columns; ++k) {
        float f = float(j)/(rows-1), angle = 2*pi*k/columns;
        float y = waist - height*0.43f*f;
        float radius = bodyRadius(waist, angle) + height*(0.008f + 0.08f*f);
        radius = std::max(radius, bodyRadius(y, angle) + height*0.012f);
        points.emplace_back(radius*std::cos(angle), y, radius*std::sin(angle));
    }
    previous = anchors = points;
    auto link = [this](int a, int b) { links.push_back({a,b,glm::length(points[a]-points[b])}); };
    for (int j = 0; j < rows; ++j) for (int k = 0; k < columns; ++k) {
        int a = j*columns+k;
        link(a,j*columns+(k+1)%columns);
        if (j+1 < rows) {
            link(a,a+columns);
            link(a,(j+1)*columns+(k+1)%columns);
            link(a,(j+1)*columns+(k+columns-1)%columns);
        }
        if (j+2 < rows) link(a,a+2*columns);
        link(a,j*columns+(k+2)%columns);
    }
    cuda = createCudaCloth(points, links, envelope.data(), bottom, height);
}
float ClothSimulation::bodyRadius(float y, float angle) const
{
    float s = std::clamp((y-bottom)/height*(slices-1),0.0f,float(slices-1));
    angle = std::fmod(angle+4*pi,2*pi);
    float k = angle/(2*pi)*angles;
    int i = int(s), a = int(k)%angles, b = (a+1)%angles;
    auto at = [this,a,b,k](int row) {
        return envelope[row*angles+a]*(1-(k-int(k))) + envelope[row*angles+b]*(k-int(k));
    };
    return at(i)*(1-(s-i))+at(std::min(i+1,slices-1))*(s-i);
}
void ClothSimulation::collide(glm::vec3 &p) const
{
    float r = std::hypot(p.x,p.z);
    float target = bodyRadius(p.y,std::atan2(p.z,p.x)) + height*0.006f;
    if (r < target) {
        if (r > 1e-8f) { p.x *= target/r; p.z *= target/r; }
        else { p.x = target; p.z = 0; }
    }
    p.y = std::max(p.y,bottom+height*0.005f);
}
void ClothSimulation::collideSelf()
{
    // Particle thickness is smaller than the rest spacing. Ignore the local
    // two-ring neighbourhood, whose shape is already controlled by links.
    const float thickness = height * 0.012f;
    using Cell = std::array<int, 3>;
    std::map<Cell, std::vector<int>> grid;
    auto cellFor = [thickness](glm::vec3 p) -> Cell {
        return {int(std::floor(p.x/thickness)), int(std::floor(p.y/thickness)),
                int(std::floor(p.z/thickness))};
    };
    // Freeze the broad-phase positions for this pass; the next solver iteration
    // rebuilds the grid after constraint and contact corrections.
    const auto snapshot = points;
    for (int i = 0; i < int(points.size()); ++i) grid[cellFor(snapshot[i])].push_back(i);
    for (int i = 0; i < int(points.size()); ++i) {
        const auto cell = cellFor(snapshot[i]);
        for (int x = -1; x <= 1; ++x) for (int y = -1; y <= 1; ++y)
            for (int z = -1; z <= 1; ++z) {
                auto found = grid.find({cell[0]+x, cell[1]+y, cell[2]+z});
                if (found == grid.end()) continue;
                for (int j : found->second) {
                    if (j <= i) continue;
                    int dk = std::abs(i%columns-j%columns);
                    dk = std::min(dk, columns-dk);
                    if (std::abs(i/columns-j/columns) <= 2 && dk <= 2) continue;
                    float wi = i < columns ? 0.0f : 1.0f;
                    float wj = j < columns ? 0.0f : 1.0f;
                    if (wi+wj == 0) continue;
                    auto delta = points[j]-points[i];
                    float distance = glm::length(delta);
                    if (distance >= thickness) continue;
                    // Rest direction provides a deterministic normal for coincident particles.
                    auto normal = distance > 1e-8f ? delta/distance
                        : glm::normalize(anchors[j]-anchors[i]);
                    auto correction = normal*((thickness-distance)/(wi+wj));
                    points[i] -= wi*correction;
                    points[j] += wj*correction;
                }
            }
    }
}
void ClothSimulation::step(float dt, float wind)
{
    if (points.empty() || dt <= 0) return;
    dt = std::clamp(dt, 0.0f, 1.0f/60);
    if (cuda) {
        if (cuda->step(dt, wind, time, points, previous)) { time += dt; return; }
        cuda.reset();
    }
    constexpr int substeps = 4;
    float h = dt/substeps;
    for (int sub = 0; sub < substeps; ++sub) {
        time += h;
        for (size_t i = columns; i < points.size(); ++i) {
            auto old = points[i];
            glm::vec3 acceleration(wind*(1.5f+std::sin(time*2+old.y*3)), -9.81f,
                                    wind*std::sin(time*1.4f)*0.6f);
            points[i] += (points[i]-previous[i])*0.995f + acceleration*h*h;
            previous[i] = old;
        }
        for (int iteration = 0; iteration < 8; ++iteration) {
            for (const auto &link : links) {
                auto delta = points[link.b]-points[link.a];
                float length = glm::length(delta);
                if (length < 1e-8f) continue;
                float wa = link.a < columns ? 0 : 1, wb = link.b < columns ? 0 : 1;
                if (wa+wb == 0) continue;
                auto correction = delta*((length-link.length)/length/(wa+wb));
                points[link.a] += wa*correction; points[link.b] -= wb*correction;
            }
            collideSelf();
            for (size_t i = columns; i < points.size(); ++i) collide(points[i]);
            for (int k = 0; k < columns; ++k) points[k] = anchors[k];
        }
    }
}
std::vector<makehuman::MeshVertex> ClothSimulation::mesh() const
{
    std::vector<makehuman::MeshVertex> out;
    if (points.empty()) return out;
    if (cuda && cuda->mesh(out)) return out;
    out.reserve((rows-1)*columns*6);
    auto triangle = [&out,this](int a, int b, int c) {
        auto n = glm::cross(points[b]-points[a],points[c]-points[a]);
        float len = glm::length(n); n = len > 1e-8f ? n/len : glm::vec3(0,1,0);
        float light = 0.35f+0.65f*std::abs(glm::dot(n,glm::normalize(glm::vec3(.4f,.8f,.5f))));
        auto color = glm::vec3(.12f,.42f,.85f)*light;
        for (int i : {a,b,c}) out.push_back({points[i], n, color});
    };
    for (int j = 0; j+1 < rows; ++j) for (int k = 0; k < columns; ++k) {
        int a = j*columns+k, b = j*columns+(k+1)%columns;
        triangle(a,a+columns,b); triangle(b,a+columns,b+columns);
    }
    return out;
}
