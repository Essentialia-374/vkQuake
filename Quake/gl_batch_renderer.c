/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "quakedef.h"

#include <string.h>
#include <stdlib.h>

#include "gl_batch_renderer.h"

typedef enum gl_batch_pipeline_e
{
        GL_BATCH_PIPELINE_INVALID = -1,
        GL_BATCH_PIPELINE_TEXTURED_OPAQUE = 0,
        GL_BATCH_PIPELINE_TEXTURED_BLEND,
        GL_BATCH_PIPELINE_NOTEX_BLEND,
} gl_batch_pipeline_t;

typedef struct gl_batch_draw_s
{
        gl_batch_pipeline_t pipeline;
        gltexture_t        *texture;
        uint32_t            first_vertex;
        uint32_t            vertex_count;
} gl_batch_draw_t;

typedef struct gl_batch_state_s
{
        basicvertex_t      *vertices;
        size_t              vertex_count;
        size_t              vertex_capacity;

        gl_batch_draw_t    *draws;
        size_t              draw_count;
        size_t              draw_capacity;

        cb_context_t       *cbx;
} gl_batch_state_t;

static gl_batch_state_t gl_batch_state;

static void GL_BatchRenderer_EnsureVertexCapacity (size_t additional_vertices)
{
        const size_t needed = gl_batch_state.vertex_count + additional_vertices;
        if (needed <= gl_batch_state.vertex_capacity)
                return;

        size_t new_capacity = gl_batch_state.vertex_capacity ? gl_batch_state.vertex_capacity : 256;
        while (new_capacity < needed)
                new_capacity *= 2;

        basicvertex_t *new_vertices = (basicvertex_t *)realloc (gl_batch_state.vertices, new_capacity * sizeof (basicvertex_t));
        if (!new_vertices)
                Sys_Error ("GL_BatchRenderer: failed to allocate %zu vertices", new_capacity);

        gl_batch_state.vertices = new_vertices;
        gl_batch_state.vertex_capacity = new_capacity;
}

static void GL_BatchRenderer_EnsureDrawCapacity (size_t additional_draws)
{
        const size_t needed = gl_batch_state.draw_count + additional_draws;
        if (needed <= gl_batch_state.draw_capacity)
                return;

        size_t new_capacity = gl_batch_state.draw_capacity ? gl_batch_state.draw_capacity : 64;
        while (new_capacity < needed)
                new_capacity *= 2;

        gl_batch_draw_t *new_draws = (gl_batch_draw_t *)realloc (gl_batch_state.draws, new_capacity * sizeof (gl_batch_draw_t));
        if (!new_draws)
                Sys_Error ("GL_BatchRenderer: failed to allocate %zu draws", new_capacity);

        gl_batch_state.draws = new_draws;
        gl_batch_state.draw_capacity = new_capacity;
}

static inline void GL_BatchRenderer_BindTexture (cb_context_t *cbx, gltexture_t *tex)
{
        vulkan_globals.vk_cmd_bind_descriptor_sets (
                cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_pipeline_layout.handle, 0, 1, &tex->descriptor_set,
                0, NULL);
}

static inline void GL_BatchRenderer_BindTexturedPipeline (cb_context_t *cbx, qboolean alpha_blend)
{
        R_BindPipeline (
                cbx, VK_PIPELINE_BIND_POINT_GRAPHICS,
                alpha_blend ? vulkan_globals.basic_blend_pipeline[cbx->render_pass_index]
                            : vulkan_globals.basic_alphatest_pipeline[cbx->render_pass_index]);
}

static inline void GL_BatchRenderer_BindNoTexBlendPipeline (cb_context_t *cbx)
{
        R_BindPipeline (
                cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_notex_blend_pipeline[cbx->render_pass_index]);
}

static void GL_BatchRenderer_FlushInternal (void)
{
        if (!gl_batch_state.cbx || gl_batch_state.vertex_count == 0)
        {
                gl_batch_state.vertex_count = 0;
                gl_batch_state.draw_count = 0;
                return;
        }

        cb_context_t *cbx = gl_batch_state.cbx;

        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize buffer_offset = 0;
        basicvertex_t *dst = (basicvertex_t *)R_VertexAllocate (
                (int)(gl_batch_state.vertex_count * sizeof (basicvertex_t)), &buffer, &buffer_offset);

        memcpy (dst, gl_batch_state.vertices, gl_batch_state.vertex_count * sizeof (basicvertex_t));

        vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);

        gl_batch_pipeline_t bound_pipeline = GL_BATCH_PIPELINE_INVALID;
        gltexture_t *bound_texture = NULL;

        for (size_t i = 0; i < gl_batch_state.draw_count; ++i)
        {
                const gl_batch_draw_t *draw = &gl_batch_state.draws[i];

                if (draw->pipeline != bound_pipeline)
                {
                        switch (draw->pipeline)
                        {
                        case GL_BATCH_PIPELINE_TEXTURED_OPAQUE:
                                GL_BatchRenderer_BindTexturedPipeline (cbx, false);
                                break;
                        case GL_BATCH_PIPELINE_TEXTURED_BLEND:
                                GL_BatchRenderer_BindTexturedPipeline (cbx, true);
                                break;
                        case GL_BATCH_PIPELINE_NOTEX_BLEND:
                                GL_BatchRenderer_BindNoTexBlendPipeline (cbx);
                                break;
                        case GL_BATCH_PIPELINE_INVALID:
                        default:
                                Sys_Error ("GL_BatchRenderer: encountered invalid pipeline");
                        }

                        bound_pipeline = draw->pipeline;
                        bound_texture = NULL;
                }

                if (draw->pipeline != GL_BATCH_PIPELINE_NOTEX_BLEND && draw->texture != bound_texture)
                {
                        GL_BatchRenderer_BindTexture (cbx, draw->texture);
                        bound_texture = draw->texture;
                }

                vulkan_globals.vk_cmd_draw (cbx->cb, draw->vertex_count, 1, draw->first_vertex, 0);
        }

        gl_batch_state.vertex_count = 0;
        gl_batch_state.draw_count = 0;
}

static void GL_BatchRenderer_SwitchContext (cb_context_t *cbx)
{
        if (gl_batch_state.cbx == cbx)
                return;

        GL_BatchRenderer_FlushInternal ();
        gl_batch_state.cbx = cbx;
}

static uint32_t GL_BatchRenderer_AppendQuad (const basicvertex_t quad[4])
{
        GL_BatchRenderer_EnsureVertexCapacity (6);

        basicvertex_t *out = &gl_batch_state.vertices[gl_batch_state.vertex_count];
        out[0] = quad[0];
        out[1] = quad[1];
        out[2] = quad[2];
        out[3] = quad[0];
        out[4] = quad[2];
        out[5] = quad[3];

        const uint32_t first_vertex = (uint32_t)gl_batch_state.vertex_count;
        gl_batch_state.vertex_count += 6;
        return first_vertex;
}

static void GL_BatchRenderer_AddDraw (gl_batch_pipeline_t pipeline, gltexture_t *texture, uint32_t first_vertex)
{
        if (gl_batch_state.draw_count > 0)
        {
                gl_batch_draw_t *last = &gl_batch_state.draws[gl_batch_state.draw_count - 1];
                if (last->pipeline == pipeline && last->texture == texture)
                {
                        last->vertex_count += 6;
                        return;
                }
        }

        GL_BatchRenderer_EnsureDrawCapacity (1);
        gl_batch_draw_t *draw = &gl_batch_state.draws[gl_batch_state.draw_count++];
        draw->pipeline = pipeline;
        draw->texture = texture;
        draw->first_vertex = first_vertex;
        draw->vertex_count = 6;
}

void GL_BatchRenderer_SubmitTexturedQuad (
        cb_context_t *cbx, gltexture_t *texture, qboolean alpha_blend, const basicvertex_t quad[4])
{
        if (!cbx || !texture)
                return;

        GL_BatchRenderer_SwitchContext (cbx);

        const uint32_t first_vertex = GL_BatchRenderer_AppendQuad (quad);
        GL_BatchRenderer_AddDraw (
                alpha_blend ? GL_BATCH_PIPELINE_TEXTURED_BLEND : GL_BATCH_PIPELINE_TEXTURED_OPAQUE, texture, first_vertex);
}

void GL_BatchRenderer_SubmitColorQuad (cb_context_t *cbx, const basicvertex_t quad[4])
{
        if (!cbx)
                return;

        GL_BatchRenderer_SwitchContext (cbx);

        const uint32_t first_vertex = GL_BatchRenderer_AppendQuad (quad);
        GL_BatchRenderer_AddDraw (GL_BATCH_PIPELINE_NOTEX_BLEND, NULL, first_vertex);
}

void GL_BatchRenderer_Flush (cb_context_t *cbx)
{
        GL_BatchRenderer_FlushInternal ();
        gl_batch_state.cbx = cbx;
}
