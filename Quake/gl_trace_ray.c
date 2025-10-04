#include "quakedef.h"
#include "gl_traceray.h"

void GL_TraceRay_UpdateDescriptorSet (const VkDescriptorImageInfo *output_image_info, VkAccelerationStructureKHR tlas)
{
        if (!vulkan_globals.ray_query || (vulkan_globals.ray_debug_set_layout.handle == VK_NULL_HANDLE) || !output_image_info)
                return;

        if (vulkan_globals.ray_debug_desc_set != VK_NULL_HANDLE)
                R_FreeDescriptorSet (vulkan_globals.ray_debug_desc_set, &vulkan_globals.ray_debug_set_layout);

        vulkan_globals.ray_debug_desc_set = R_AllocateDescriptorSet (&vulkan_globals.ray_debug_set_layout);

        int num_writes = 0;
        ZEROED_STRUCT_ARRAY (VkWriteDescriptorSet, writes, 2);

        writes[num_writes].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[num_writes].dstSet = vulkan_globals.ray_debug_desc_set;
        writes[num_writes].dstBinding = num_writes;
        writes[num_writes].descriptorCount = 1;
        writes[num_writes].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[num_writes].pImageInfo = output_image_info;
        num_writes += 1;

        ZEROED_STRUCT (VkWriteDescriptorSetAccelerationStructureKHR, acceleration_structure_write);

        if (tlas != VK_NULL_HANDLE)
        {
                acceleration_structure_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
                acceleration_structure_write.accelerationStructureCount = 1;
                acceleration_structure_write.pAccelerationStructures = &tlas;

                writes[num_writes].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[num_writes].pNext = &acceleration_structure_write;
                writes[num_writes].dstSet = vulkan_globals.ray_debug_desc_set;
                writes[num_writes].dstBinding = num_writes;
                writes[num_writes].descriptorCount = 1;
                writes[num_writes].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                num_writes += 1;
        }

        if (num_writes > 0)
                vkUpdateDescriptorSets (vulkan_globals.device, num_writes, writes, 0, NULL);
}

void GL_TraceRay_Render (cb_context_t *cbx, int width, int height, const gl_trace_ray_constants_t *constants)
{
        if (!cbx || !constants)
                return;
        if (!vulkan_globals.ray_query)
                return;
        if (vulkan_globals.ray_debug_pipeline.handle == VK_NULL_HANDLE)
                return;
        if (vulkan_globals.ray_debug_desc_set == VK_NULL_HANDLE)
                return;

        R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_globals.ray_debug_pipeline);
        vkCmdBindDescriptorSets (
                cbx->cb, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_globals.ray_debug_pipeline.layout.handle, 0, 1,
                &vulkan_globals.ray_debug_desc_set, 0, NULL);

        R_PushConstants (cbx, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (*constants), constants);

        const uint32_t group_count_x = (uint32_t)((width + 7) / 8);
        const uint32_t group_count_y = (uint32_t)((height + 7) / 8);
        vkCmdDispatch (cbx->cb, group_count_x, group_count_y, 1);
}
