#ifndef GL_TRACE_RAY_H
#define GL_TRACE_RAY_H

#include "quakedef.h"

typedef struct gl_trace_ray_constants_s
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
} gl_trace_ray_constants_t;

#if defined(_DEBUG)
void GL_TraceRay_UpdateDescriptorSet (const VkDescriptorImageInfo *output_image_info, VkAccelerationStructureKHR tlas);
void GL_TraceRay_Render (cb_context_t *cbx, int width, int height, const gl_trace_ray_constants_t *constants);
#else
static inline void GL_TraceRay_UpdateDescriptorSet (
        const VkDescriptorImageInfo *output_image_info, VkAccelerationStructureKHR tlas)
{
        (void)output_image_info;
        (void)tlas;
}

static inline void GL_TraceRay_Render (cb_context_t *cbx, int width, int height, const gl_trace_ray_constants_t *constants)
{
        (void)cbx;
        (void)width;
        (void)height;
        (void)constants;
}
#endif /* _DEBUG */

#endif /* GL_TRACE_RAY_H */
