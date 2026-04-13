#ifndef RENDER_H
#define RENDER_H

#include "sokol_gfx.h"
#include "HandmadeMath.h"
#include "lod.h"
#include "camera.h"

typedef struct {
    sg_pipeline planet_pip;
    sg_pass_action pass_action;
    float sun_angle;           // sun orbit angle (radians)
    float planet_radius;
    float atmosphere_radius;
} Renderer;

void renderer_init(Renderer* r, float planet_radius);
void renderer_frame(Renderer* r, Camera* cam, LodTree* lod, float dt);
void renderer_shutdown(Renderer* r);

#endif
