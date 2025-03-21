
#ifndef MODEL_H
#define MODEL_H
#include "DirectXMath.h"
#include <vector>
#include <string>
struct mVertex
{
    mVertex() {}
    
    mVertex(
        const DirectX::XMFLOAT3& p,
        const DirectX::XMFLOAT3& n,
        const DirectX::XMFLOAT3& t,
        const DirectX::XMFLOAT2& uv) :
        Position(p),
        Normal(n),
        TangentU(t),
        TexC(uv) {
    }
    mVertex(
        float px, float py, float pz,
        float nx, float ny, float nz,
        float tx, float ty, float tz,
        float u, float v) :
        Position(px, py, pz),
        Normal(nx, ny, nz),
        TangentU(tx, ty, tz),
        TexC(u, v) {
    }

    DirectX::XMFLOAT3 Position;
    DirectX::XMFLOAT3 Normal;
    DirectX::XMFLOAT3 TangentU;
    DirectX::XMFLOAT2 TexC;
};

typedef mVertex Vert;
using namespace DirectX;
struct polygon {
    Vert verts[3];
    polygon() = default;
    polygon(Vert v1, Vert v2, Vert v3) : verts{ v1,v2,v3 } {}
};
class Model {
    
private:
    std::vector<XMFLOAT3> verts_;
    std::vector<polygon> faces_;
    std::vector<XMFLOAT3> normals_;
    std::vector<XMFLOAT2> uv_coords_;
   // TGAImage diffuse_map_;
public:
    Model(std::string filename);
    ~Model();
    int nverts();
    int nfaces();
    XMFLOAT3 vert(int i);
    XMFLOAT3 normal(int i);
    XMFLOAT2 uv_coords(int i);
    //void load_texture(std::string filename,std::string suffix, TGAImage& img);
    //TGAColor diffuse_color(Vector2 uv);
    polygon face(int idx);
};

#endif