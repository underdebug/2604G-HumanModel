#include "CudaCloth.h"
#include <cuda_runtime.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace {
constexpr int columns=48, rows=28, count=columns*rows, samples=96;
constexpr float pi=3.14159265358979323846f;
struct Vec { float x,y,z; };
struct Vertex { Vec pos, normal, color; };
__device__ Vec add(Vec a,Vec b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
__device__ Vec sub(Vec a,Vec b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
__device__ Vec mul(Vec a,float s) { return {a.x*s,a.y*s,a.z*s}; }
__device__ float dot(Vec a,Vec b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
__device__ float length(Vec a) { return sqrtf(dot(a,a)); }
__device__ Vec cross(Vec a,Vec b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
__global__ void integrate(Vec *p,Vec *prev,float h,float wind,float time) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i<columns || i>=count) return;
    Vec old=p[i];
    Vec acceleration={wind*(1.5f+sinf(time*2+old.y*3)),-9.81f,wind*sinf(time*1.4f)*.6f};
    p[i]=add(add(old,mul(sub(old,prev[i]),.995f)),mul(acceleration,h*h));
    prev[i]=old;
}
// Each batch contains disjoint edges. No two threads write the same vertex.
__global__ void constrain(Vec *p,const ClothLink *links,int start,int size) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i>=size) return;
    ClothLink link=links[start+i];
    Vec delta=sub(p[link.b],p[link.a]);
    float d=length(delta),wa=link.a<columns?0:1,wb=link.b<columns?0:1;
    if(d<1e-8f || wa+wb==0) return;
    Vec correction=mul(delta,(d-link.length)/d/(wa+wb));
    if(wa) p[link.a]=add(p[link.a],correction);
    if(wb) p[link.b]=sub(p[link.b],correction);
}
__device__ float radius(const float *e,float y,float angle,float bottom,float height) {
    float s=fminf(fmaxf((y-bottom)/height*(samples-1),0),float(samples-1));
    float k=fmodf(angle+4*pi,2*pi)/(2*pi)*samples;
    int row=int(s),a=int(k)%samples,b=(a+1)%samples,next=min(row+1,samples-1);
    float f=k-int(k),g=s-row;
    return (e[row*samples+a]*(1-f)+e[row*samples+b]*f)*(1-g)
        +(e[next*samples+a]*(1-f)+e[next*samples+b]*f)*g;
}
// Gather contacts from an immutable snapshot. Each thread owns one output;
// pinned vertices participate in contact but receive no correction.
__global__ void contacts(const Vec *in,Vec *out,const Vec *anchors,const float *e,float bottom,float height) {
    int i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i>=count) return;
    if(i<columns) { out[i]=anchors[i]; return; }
    Vec p=in[i],correction={0,0,0};
    float thickness=height*.012f;
    for(int j=0;j<count;++j) {
        int dk=abs(i%columns-j%columns); dk=min(dk,columns-dk);
        if(abs(i/columns-j/columns)<=2 && dk<=2) continue;
        Vec delta=sub(p,in[j]);
        // Cheap broad phase before the square root.
        if(fabsf(delta.x)>=thickness || fabsf(delta.y)>=thickness || fabsf(delta.z)>=thickness) continue;
        float d=length(delta);
        if(d>=thickness) continue;
        Vec normal=d>1e-8f?mul(delta,1/d):sub(anchors[i],anchors[j]);
        if(d<=1e-8f) normal=mul(normal,1/fmaxf(length(normal),1e-8f));
        correction=add(correction,mul(normal,(thickness-d)/(j<columns?1.0f:2.0f)));
    }
    // Limit aggregate correction in dense folds to avoid a Jacobi overshoot.
    float amount=length(correction);
    if(amount>thickness) correction=mul(correction,thickness/amount);
    p=add(p,correction);
    p.y=fmaxf(p.y,bottom+height*.005f);
    float r=hypotf(p.x,p.z),target=radius(e,p.y,atan2f(p.z,p.x),bottom,height)+height*.006f;
    if(r<target) {
        if(r>1e-8f) { p.x*=target/r; p.z*=target/r; }
        else { p.x=target; p.z=0; }
    }
    out[i]=p;
}
__global__ void triangles(const Vec *p,Vertex *out) {
    int t=blockIdx.x*blockDim.x+threadIdx.x;
    if(t>=(rows-1)*columns*2) return;
    int cell=t/2,row=cell/columns,k=cell%columns;
    int a=row*columns+k,b=row*columns+(k+1)%columns;
    int ids[3];
    if(t%2==0) { ids[0]=a; ids[1]=a+columns; ids[2]=b; }
    else { ids[0]=b; ids[1]=a+columns; ids[2]=b+columns; }
    Vec n=cross(sub(p[ids[1]],p[ids[0]]),sub(p[ids[2]],p[ids[0]]));
    float d=length(n); n=d>1e-8f?mul(n,1/d):Vec{0,1,0};
    Vec light={.4f,.8f,.5f}; light=mul(light,1/length(light));
    float shade=.35f+.65f*fabsf(dot(n,light));
    for(int v=0;v<3;++v) out[t*3+v]={p[ids[v]],n,mul(Vec{.12f,.42f,.85f},shade)};
}
void check(cudaError_t error) {
    if(error!=cudaSuccess) throw std::runtime_error(cudaGetErrorString(error));
}
template<class T> void allocate(T *&p,size_t n) { check(cudaMalloc(reinterpret_cast<void **>(&p),n*sizeof(T))); }
std::vector<Vec> pack(const std::vector<glm::vec3> &p) {
    std::vector<Vec> out; out.reserve(p.size());
    for(auto v:p) out.push_back({v.x,v.y,v.z});
    return out;
}
class Backend final : public CudaCloth {
    Vec *p=nullptr,*prev=nullptr,*anchors=nullptr,*scratch=nullptr;
    float *envelope=nullptr;
    ClothLink *edges=nullptr;
    Vertex *vertices=nullptr;
    std::vector<int> batches;
    float bottom,height;
public:
    Backend(float b,float h):bottom(b),height(h) {}
    ~Backend() override {
        cudaFree(p); cudaFree(prev); cudaFree(anchors); cudaFree(scratch);
        cudaFree(envelope); cudaFree(edges); cudaFree(vertices);
    }
    void initialize(const std::vector<glm::vec3> &points,const std::vector<ClothLink> &links,const float *env) {
        allocate(p,count); allocate(prev,count); allocate(anchors,count); allocate(scratch,count);
        allocate(envelope,samples*samples); allocate(vertices,(rows-1)*columns*6);
        auto packed=pack(points);
        for(auto buffer:{p,prev,anchors}) check(cudaMemcpy(buffer,packed.data(),count*sizeof(Vec),cudaMemcpyHostToDevice));
        check(cudaMemcpy(envelope,env,samples*samples*sizeof(float),cudaMemcpyHostToDevice));
        // Greedy edge colouring is setup-only; all constraint math runs on CUDA.
        std::vector<std::vector<ClothLink>> groups;
        std::vector<std::vector<bool>> used;
        for(auto edge:links) {
            size_t g=0;
            for(;g<groups.size();++g) if(!used[g][edge.a] && !used[g][edge.b]) break;
            if(g==groups.size()) { groups.emplace_back(); used.emplace_back(count,false); }
            groups[g].push_back(edge); used[g][edge.a]=used[g][edge.b]=true;
        }
        std::vector<ClothLink> sorted;
        batches.push_back(0);
        for(const auto &group:groups) {
            sorted.insert(sorted.end(),group.begin(),group.end()); batches.push_back(int(sorted.size()));
        }
        allocate(edges,sorted.size());
        check(cudaMemcpy(edges,sorted.data(),sorted.size()*sizeof(ClothLink),cudaMemcpyHostToDevice));
    }
    bool step(float dt,float wind,float time,std::vector<glm::vec3> &points,std::vector<glm::vec3> &previous) override {
        try {
            float h=dt/4;
            for(int substep=0;substep<4;++substep) {
                integrate<<<(count+127)/128,128>>>(p,prev,h,wind,time+(substep+1)*h);
                check(cudaGetLastError());
                for(int iteration=0;iteration<8;++iteration) {
                    for(size_t g=1;g<batches.size();++g) {
                        int n=batches[g]-batches[g-1];
                        constrain<<<(n+127)/128,128>>>(p,edges,batches[g-1],n);
                        check(cudaGetLastError());
                    }
                    contacts<<<(count+127)/128,128>>>(p,scratch,anchors,envelope,bottom,height);
                    check(cudaGetLastError()); std::swap(p,scratch);
                }
            }
            std::vector<Vec> host(count),old(count);
            check(cudaMemcpy(host.data(),p,count*sizeof(Vec),cudaMemcpyDeviceToHost));
            check(cudaMemcpy(old.data(),prev,count*sizeof(Vec),cudaMemcpyDeviceToHost));
            // Commit together so a failed download leaves the CPU snapshot intact.
            for(int i=0;i<count;++i) {
                points[i]={host[i].x,host[i].y,host[i].z};
                previous[i]={old[i].x,old[i].y,old[i].z};
            }
            return true;
        } catch(const std::exception &e) {
            std::fprintf(stderr,"cloth CUDA step failed; reverting to CPU: %s\n",e.what()); return false;
        }
    }
    bool mesh(std::vector<makehuman::MeshVertex> &out) override {
        try {
            constexpr int n=(rows-1)*columns*6;
            triangles<<<(n/3+127)/128,128>>>(p,vertices); check(cudaGetLastError());
            std::vector<Vertex> host(n);
            check(cudaMemcpy(host.data(),vertices,n*sizeof(Vertex),cudaMemcpyDeviceToHost));
            out.resize(n);
            for(int i=0;i<n;++i) {
                auto v=host[i]; out[i]={{v.pos.x,v.pos.y,v.pos.z},{v.normal.x,v.normal.y,v.normal.z},{v.color.x,v.color.y,v.color.z}};
            }
            return true;
        } catch(const std::exception &e) {
            std::fprintf(stderr,"cloth CUDA mesh failed; using CPU mesh: %s\n",e.what()); return false;
        }
    }
};
}
std::unique_ptr<CudaCloth> createCudaCloth(const std::vector<glm::vec3> &points,
    const std::vector<ClothLink> &links,const float *envelope,float bottom,float height) {
    const char *choice=std::getenv("CLOTH_BACKEND");
    if(choice && std::string(choice)=="cpu") return {};
    int devices=0; auto status=cudaGetDeviceCount(&devices);
    if(status!=cudaSuccess || devices==0) {
        std::fprintf(stderr,"cloth: CPU backend (CUDA unavailable: %s)\n",status==cudaSuccess?"no devices":cudaGetErrorString(status));
        return {};
    }
    try {
        auto backend=std::make_unique<Backend>(bottom,height);
        backend->initialize(points,links,envelope);
        std::fprintf(stderr,"cloth: CUDA backend active\n"); return backend;
    } catch(const std::exception &e) {
        std::fprintf(stderr,"cloth: CPU backend (CUDA initialization failed: %s)\n",e.what()); return {};
    }
}
