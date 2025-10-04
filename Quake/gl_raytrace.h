#ifndef GL_RAYTRACE_H
#define GL_RAYTRACE_H

#include "quakedef.h"

typedef struct gl_raytrace_constants_s
{
        float screen_size_rcp_x;
        float screen_size_rcp_y;
        float aspect_ratio;
        float origin_x;
        float origin_y;
        float origin_z;
        float forward_x;
        float forward_y;
        float forward_z;
        float right_x;
        float right_y;
        float right_z;
        float down_x;
        float down_y;
        float down_z;
} gl_raytrace_constants_t;

void GL_Raytrace_UpdateDescriptorSet (const VkDescriptorImageInfo *output_image_info, VkAccelerationStructureKHR tlas);
void GL_Raytrace_Render (cb_context_t *cbx, int width, int height, const gl_raytrace_constants_t *constants);

#endif /* GL_RAYTRACE_H */
