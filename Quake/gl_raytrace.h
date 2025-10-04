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

	// NEW:
	float	 aperture;		 // world units (0 = pinhole)
	float	 focus_distance; // meters
	float	 exposure;		 // multiplier for tonemapper
	uint32_t frame_index;	 // progressive sample index (1..N)
	uint32_t rng_seed;	 // per-dispatch random seed (monotonic)
} gl_raytrace_constants_t;

// NEW signature: add accumulation & environment, optional material/light buffers
void GL_Raytrace_UpdateDescriptorSet (
	const VkDescriptorImageInfo *output_image_info, VkAccelerationStructureKHR tlas, const VkDescriptorImageInfo *accum_image_info,
	const VkDescriptorImageInfo	 *env_tex_info,		   // combined image sampler
	const VkDescriptorBufferInfo *materials_ssbo_info, // may be NULL
	const VkDescriptorBufferInfo *lights_ssbo_info	   // may be NULL
);

void GL_Raytrace_Render (cb_context_t *cbx, int width, int height, const gl_raytrace_constants_t *constants);

#endif /* GL_RAYTRACE_H */
