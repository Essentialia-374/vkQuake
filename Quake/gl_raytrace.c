#include "quakedef.h"
#include "gl_raytrace.h"

void GL_Raytrace_UpdateDescriptorSet (const VkDescriptorImageInfo *output_image_info, VkAccelerationStructureKHR tlas, const VkDescriptorImageInfo *accum_image_info,
									  const VkDescriptorImageInfo *env_tex_info, const VkDescriptorBufferInfo *materials_ssbo_info, const VkDescriptorBufferInfo *lights_ssbo_info)
{
	if (!vulkan_globals.ray_query || (vulkan_globals.raytrace_set_layout.handle == VK_NULL_HANDLE) || !output_image_info)
		return;

	if (vulkan_globals.raytrace_desc_set != VK_NULL_HANDLE)
		R_FreeDescriptorSet (vulkan_globals.raytrace_desc_set, &vulkan_globals.raytrace_set_layout);

	vulkan_globals.raytrace_desc_set = R_AllocateDescriptorSet (&vulkan_globals.raytrace_set_layout);

	VkWriteDescriptorSet writes[6];
	memset (writes, 0, sizeof (writes));
	int num_writes = 0;

	// binding 0 : output_image (rgba16f) STORAGE_IMAGE
	writes[num_writes].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[num_writes].dstSet = vulkan_globals.raytrace_desc_set;
	writes[num_writes].dstBinding = 0;
	writes[num_writes].descriptorCount = 1;
	writes[num_writes].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	writes[num_writes].pImageInfo = output_image_info;
	num_writes++;

	// binding 1 : TLAS (acceleration structure)
	if (tlas != VK_NULL_HANDLE)
	{
		VkWriteDescriptorSetAccelerationStructureKHR as_info = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR, .accelerationStructureCount = 1, .pAccelerationStructures = &tlas};
		writes[num_writes].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[num_writes].pNext = &as_info;
		writes[num_writes].dstSet = vulkan_globals.raytrace_desc_set;
		writes[num_writes].dstBinding = 1;
		writes[num_writes].descriptorCount = 1;
		writes[num_writes].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		num_writes++;
	}

	// binding 2 : accum_image (rgba32f) STORAGE_IMAGE (read/write)
	if (accum_image_info)
	{
		writes[num_writes].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[num_writes].dstSet = vulkan_globals.raytrace_desc_set;
		writes[num_writes].dstBinding = 2;
		writes[num_writes].descriptorCount = 1;
		writes[num_writes].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		writes[num_writes].pImageInfo = accum_image_info;
		num_writes++;
	}

	// binding 3 : env_tex (COMBINED_IMAGE_SAMPLER)
	if (env_tex_info)
	{
		writes[num_writes].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[num_writes].dstSet = vulkan_globals.raytrace_desc_set;
		writes[num_writes].dstBinding = 3;
		writes[num_writes].descriptorCount = 1;
		writes[num_writes].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[num_writes].pImageInfo = env_tex_info;
		num_writes++;
	}

	// binding 4 : materials SSBO (optional)
	if (materials_ssbo_info)
	{
		writes[num_writes].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[num_writes].dstSet = vulkan_globals.raytrace_desc_set;
		writes[num_writes].dstBinding = 4;
		writes[num_writes].descriptorCount = 1;
		writes[num_writes].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[num_writes].pBufferInfo = materials_ssbo_info;
		num_writes++;
	}

	// binding 5 : lights SSBO (optional)
	if (lights_ssbo_info)
	{
		writes[num_writes].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[num_writes].dstSet = vulkan_globals.raytrace_desc_set;
		writes[num_writes].dstBinding = 5;
		writes[num_writes].descriptorCount = 1;
		writes[num_writes].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[num_writes].pBufferInfo = lights_ssbo_info;
		num_writes++;
	}

	if (num_writes > 0)
		vkUpdateDescriptorSets (vulkan_globals.device, num_writes, writes, 0, NULL);
}

void GL_Raytrace_Render (cb_context_t *cbx, int width, int height, const gl_raytrace_constants_t *constants)
{
	if (!cbx || !constants)
		return;
	if (!vulkan_globals.ray_query)
		return;
	if (vulkan_globals.raytrace_pipeline.handle == VK_NULL_HANDLE)
		return;
	if (vulkan_globals.raytrace_desc_set == VK_NULL_HANDLE)
		return;

	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_globals.raytrace_pipeline);
	vkCmdBindDescriptorSets (
		cbx->cb, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_globals.raytrace_pipeline.layout.handle, 0, 1, &vulkan_globals.raytrace_desc_set, 0, NULL);

	R_PushConstants (cbx, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (*constants), constants);

	const uint32_t group_count_x = (uint32_t)((width + 7) / 8);
	const uint32_t group_count_y = (uint32_t)((height + 7) / 8);
	vkCmdDispatch (cbx->cb, group_count_x, group_count_y, 1);
}
