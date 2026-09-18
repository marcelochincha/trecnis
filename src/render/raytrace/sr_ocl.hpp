#pragma once
#include <cstdint>















namespace ocl {

bool init(int max_ray_depth, float ambient, float shadow_eps);
bool available();


const char* device_name();



void set_room(const float* node_bounds, const int* node_links,
              const float* tris, int nnodes, int ntris);



void set_sky(const uint32_t* pixels, int npx,
             const int* off, const int* w, const int* h);


void set_dynamic(const float* node_bounds, const int* node_links,
                 const float* tris, int nnodes, int ntris);




void set_emissive(const float* tris, int count);







void render(float cpx, float cpy, float cpz,
            float axx, float axy, float axz,
            float ayx, float ayy, float ayz,
            float azx, float azy, float azz,
            float tb, float ta,
            float sunx, float suny, float sunz,
            int spp, int skybox, int reflections,
            int W, int H, uint32_t* out);

}
