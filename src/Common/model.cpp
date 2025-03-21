#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include "model.h"

Model::Model(std::string filename) : verts_(), faces_() {
    std::ifstream in;
    in.open (filename, std::ifstream::in);
    if (in.fail()) { std::cerr << ":("; }
    std::string line;
    while (!in.eof()) {
        std::getline(in, line);
        std::istringstream iss(line.c_str());
        char trash;
        if (!line.compare(0, 2, "v ")) {
            iss >> trash;
            XMFLOAT3 v;
            iss >> v.x >> v.y >> v.z;
            verts_.push_back(v);
        } else if (!line.compare(0, 3, "vn ")) {
            iss >> trash >> trash;
            XMFLOAT3 n;
            iss >> n.x >> n.y >> n.z;
            normals_.push_back(n);

        }else if (!line.compare(0, 3, "vt ")) {
            iss >> trash >> trash;
            XMFLOAT2 uv;
            iss >> uv.x >> uv.y;
            uv_coords_.push_back(uv);
        }
        else if (!line.compare(0, 2, "f ")) {
            polygon f;
            int v, uv, n, i = 0;
            iss >> trash;
            while (iss >> v >> trash >> uv >> trash >> n) {
                Vert vt;
                v--;
                uv--;
                n--;
                vt.Position = vert(v);
                vt.TexC = uv_coords(uv);
                vt.Normal = normal(n);
                f.verts[i] = vt;
                i++;
            }
            faces_.push_back(f);
        } else if (!line.compare(0, 3, "vn ")) {
            iss >> trash;
        }
    }
    in.close();
 //   load_texture(filename, "_diffuse.tga", diffuse_map_);
    std::cerr << "# v# " << verts_.size() << " f# "  << faces_.size() << std::endl;
}

Model::~Model() {
}
// number of verts
int Model::nverts() {
    return (int)verts_.size();
}
// number of faces
int Model::nfaces() {
    return (int)faces_.size();
}
// face
polygon Model::face(int idx) {
    return faces_[idx];
}
// array of verts
XMFLOAT3 Model::vert(int i) {
    return verts_[i];
}
XMFLOAT3 Model::normal(int i) {
    //XMStoreFloat3(&normals_[i], XMVector3Normalize(XMLoadFloat3(&normals_[i])))
    return normals_[i];
}
XMFLOAT2 Model::uv_coords(int i) {
 //   return Vector2(uv_coords_[i].x*diffuse_map_.get_width(), uv_coords_[i].y*diffuse_map_.get_height());
    return uv_coords_[i];
}
//TGAColor Model::diffuse_color(Vector2 uv) {
//    return diffuse_map_.get(uv.x, uv.y);
//}
//void Model::load_texture(std::string filename, std::string suffix, TGAImage& img) {
//    std::string texfile(filename);
//    size_t dot = texfile.find_last_of(".");
//    if (dot!=std::string::npos) {
//        texfile = texfile.substr(0,dot) + std::string(suffix);
//        std::cerr << "texture file " << texfile << " loading " << (img.read_tga_file(texfile.c_str()) ? "ok" : "failed") << std::endl;
//        img.flip_vertically();
//    }
//}
