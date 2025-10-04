/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers
Copyright (C) 2016 Axel Gneiting

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

// draw.c -- 2d drawing

#include "quakedef.h"

cvar_t scr_conalpha = {"scr_conalpha", "0.5", CVAR_ARCHIVE}; // johnfitz

qpic_t *draw_disc;
qpic_t *draw_backtile;

gltexture_t *char_texture;		// johnfitz
qpic_t		*pic_ovr, *pic_ins; // johnfitz -- new cursor handling
qpic_t		*pic_nul;			// johnfitz -- for missing gfx, don't crash

// johnfitz -- new pics
static const byte pic_ovr_data[8][8] = {
	{255, 255, 255, 255, 255, 255, 255, 255}, {255, 15, 15, 15, 15, 15, 15, 255}, {255, 15, 15, 15, 15, 15, 15, 2}, {255, 15, 15, 15, 15, 15, 15, 2},
	{255, 15, 15, 15, 15, 15, 15, 2},		  {255, 15, 15, 15, 15, 15, 15, 2},	  {255, 15, 15, 15, 15, 15, 15, 2}, {255, 255, 2, 2, 2, 2, 2, 2},
};

static const byte pic_ins_data[9][8] = {
	{15, 15, 255, 255, 255, 255, 255, 255}, {15, 15, 2, 255, 255, 255, 255, 255}, {15, 15, 2, 255, 255, 255, 255, 255},
	{15, 15, 2, 255, 255, 255, 255, 255},	{15, 15, 2, 255, 255, 255, 255, 255}, {15, 15, 2, 255, 255, 255, 255, 255},
	{15, 15, 2, 255, 255, 255, 255, 255},	{15, 15, 2, 255, 255, 255, 255, 255}, {255, 2, 2, 255, 255, 255, 255, 255},
};

static const byte pic_nul_data[8][8] = {
	{252, 252, 252, 252, 0, 0, 0, 0}, {252, 252, 252, 252, 0, 0, 0, 0}, {252, 252, 252, 252, 0, 0, 0, 0}, {252, 252, 252, 252, 0, 0, 0, 0},
	{0, 0, 0, 0, 252, 252, 252, 252}, {0, 0, 0, 0, 252, 252, 252, 252}, {0, 0, 0, 0, 252, 252, 252, 252}, {0, 0, 0, 0, 252, 252, 252, 252},
};

// johnfitz

typedef struct
{
	gltexture_t *gltexture;
	float		 sl, tl, sh, th;
} glpic_t;

//==============================================================================
//
//  PIC CACHING
//
//==============================================================================

typedef struct cachepic_s
{
	char   name[MAX_QPATH];
	qpic_t pic;
	byte   padding[32]; // for appended glpic
} cachepic_t;

#define MAX_CACHED_PICS 512 // Spike -- increased to avoid csqc issues.
cachepic_t menu_cachepics[MAX_CACHED_PICS];
int		   menu_numcachepics;

byte menuplyr_pixels[4096];

//  scrap allocation
//  Allocate all the little status bar obejcts into a single texture
//  to crutch up stupid hardware / drivers

#define MAX_SCRAPS	 2
#define BLOCK_WIDTH	 256
#define BLOCK_HEIGHT 256

int			 scrap_allocated[MAX_SCRAPS][BLOCK_WIDTH];
byte		 scrap_texels[MAX_SCRAPS][BLOCK_WIDTH * BLOCK_HEIGHT]; // johnfitz -- removed *4 after BLOCK_HEIGHT
qboolean	 scrap_dirty;
gltexture_t *scrap_textures[MAX_SCRAPS]; // johnfitz

/*
================
Scrap_AllocBlock

returns an index into scrap_texnums[] and the position inside it
================
*/
int Scrap_AllocBlock (int w, int h, int *x, int *y)
{
	int i, j;
	int best, best2;
	int texnum;

	for (texnum = 0; texnum < MAX_SCRAPS; texnum++)
	{
		best = BLOCK_HEIGHT;

		/* allow placement at the last valid column as well */
		for (i = 0; i <= BLOCK_WIDTH - w; i++)
		{
			best2 = 0;

			for (j = 0; j < w; j++)
			{
				if (scrap_allocated[texnum][i + j] >= best)
					break;
				if (scrap_allocated[texnum][i + j] > best2)
					best2 = scrap_allocated[texnum][i + j];
			}
			if (j == w)
			{ // this is a valid spot
				*x = i;
				*y = best = best2;
			}
		}

		if (best + h > BLOCK_HEIGHT)
			continue;

		for (i = 0; i < w; i++)
			scrap_allocated[texnum][*x + i] = best + h;

		return texnum;
	}

	Sys_Error ("Scrap_AllocBlock: full");
	return 0;
}

/*
================
Scrap_Upload -- johnfitz -- now uses TexMgr
================
*/
void Scrap_Upload (void)
{
	char name[8];
	int	 i;

	for (i = 0; i < MAX_SCRAPS; i++)
	{
		q_snprintf (name, sizeof (name), "scrap%i", i);
		scrap_textures[i] = TexMgr_LoadImage (
			NULL, name, BLOCK_WIDTH, BLOCK_HEIGHT, SRC_INDEXED, scrap_texels[i], "", (src_offset_t)scrap_texels[i],
			TEXPREF_ALPHA | TEXPREF_OVERWRITE | TEXPREF_NOPICMIP);
	}

	scrap_dirty = false;
}

/*
================
Draw_PicFromWad
================
*/
qpic_t *Draw_PicFromWad2 (const char *name, unsigned int texflags)
{
	int			 i;
	cachepic_t	*pic;
	qpic_t		*p;
	glpic_t		 gl;
	src_offset_t offset; // johnfitz
	lumpinfo_t	*info;

	// Spike -- added cachepic stuff here, to avoid glitches if the function is called multiple times with the same image.
	for (pic = menu_cachepics, i = 0; i < menu_numcachepics; pic++, i++)
	{
		if (!strcmp (name, pic->name))
			return &pic->pic;
	}
	if (menu_numcachepics == MAX_CACHED_PICS)
		Sys_Error ("menu_numcachepics == MAX_CACHED_PICS");

	p = (qpic_t *)W_GetLumpName (name, &info);
	if (!p)
	{
		Con_SafePrintf ("W_GetLumpName: %s not found\n", name);
		return pic_nul; // johnfitz
	}
	if (info->type != TYP_QPIC)
		Sys_Error ("Draw_PicFromWad: lump \"%s\" is not a qpic", name);
	if (info->size < (int)(sizeof (int) * 2) || sizeof (int) * 2 + p->width * p->height > (size_t)info->size)
		Sys_Error ("Draw_PicFromWad: pic \"%s\" truncated", name);
	if (p->width < 0 || p->height < 0)
		Sys_Error ("Draw_PicFromWad: bad size (%dx%d) for pic \"%s\"", p->width, p->height, name);

	// load little ones into the scrap
	if (p->width < 64 && p->height < 64)
	{
		int	  x = 0, y = 0;
		int	  j, k;
		int	  texnum;
		byte *data = p->data;

		texnum = Scrap_AllocBlock (p->width, p->height, &x, &y);
		scrap_dirty = true;
		k = 0;
		for (i = 0; i < p->height; i++)
		{
			for (j = 0; j < p->width; j++, k++)
				scrap_texels[texnum][(y + i) * BLOCK_WIDTH + x + j] = data[k];
		}
		gl.gltexture = scrap_textures[texnum]; // johnfitz -- changed to an array
		// johnfitz -- no longer go from 0.01 to 0.99
		gl.sl = x / (float)BLOCK_WIDTH;
		gl.sh = (x + p->width) / (float)BLOCK_WIDTH;
		gl.tl = y / (float)BLOCK_HEIGHT;
		gl.th = (y + p->height) / (float)BLOCK_HEIGHT;
	}
	else
	{
		char texturename[64];														// johnfitz
		q_snprintf (texturename, sizeof (texturename), "%s:%s", WADFILENAME, name); // johnfitz

		offset = (src_offset_t)p - (src_offset_t)wad_base + sizeof (int) * 2; // johnfitz

		gl.gltexture = TexMgr_LoadImage (NULL, texturename, p->width, p->height, SRC_INDEXED, p->data, WADFILENAME, offset, texflags); // johnfitz -- TexMgr
		gl.sl = 0;
		gl.sh = 1;
		gl.tl = 0;
		gl.th = 1;
	}

	menu_numcachepics++;
	q_snprintf (pic->name, sizeof (pic->name), "%s", name);
	pic->pic = *p;
	memcpy (pic->pic.data, &gl, sizeof (glpic_t));

	return &pic->pic;
}

qpic_t *Draw_PicFromWad (const char *name)
{
	return Draw_PicFromWad2 (name, TEXPREF_ALPHA | TEXPREF_PAD | TEXPREF_NOPICMIP);
}
#if 0 // vso - unused 
static qpic_t *Draw_GetCachedPic (const char *path)
{
	cachepic_t *pic;
	int			i;

	for (pic = menu_cachepics, i = 0; i < menu_numcachepics; pic++, i++)
	{
		if (!strcmp (path, pic->name))
			return &pic->pic;
	}
	return NULL;
}
#endif
/*
================
Draw_CachePic
================
*/
qpic_t *Draw_TryCachePic (const char *path, unsigned int texflags)
{
	cachepic_t *pic;
	int			i;
	qpic_t	   *dat;
	glpic_t		gl;

	for (pic = menu_cachepics, i = 0; i < menu_numcachepics; pic++, i++)
	{
		if (!strcmp (path, pic->name))
			return &pic->pic;
	}
	if (menu_numcachepics == MAX_CACHED_PICS)
		Sys_Error ("menu_numcachepics == MAX_CACHED_PICS");

	//
	// load the pic from disk
	//
	dat = (qpic_t *)COM_LoadFile (path, NULL);
	if (!dat)
		return NULL;
	SwapPic (dat);

	menu_numcachepics++;
	q_snprintf (pic->name, sizeof (pic->name), "%s", path);

	// HACK HACK HACK --- we need to keep the bytes for
	// the translatable player picture just for the menu
	// configuration dialog
	if (!strcmp (path, "gfx/menuplyr.lmp"))
		memcpy (menuplyr_pixels, dat->data, dat->width * dat->height);

	pic->pic.width = dat->width;
	pic->pic.height = dat->height;

	gl.gltexture = TexMgr_LoadImage (
		NULL, path, dat->width, dat->height, SRC_INDEXED, dat->data, path, sizeof (int) * 2, texflags | TEXPREF_NOPICMIP); // johnfitz -- TexMgr
	gl.sl = 0;
	gl.sh = 1;
	gl.tl = 0;
	gl.th = 1;

	memcpy (pic->pic.data, &gl, sizeof (glpic_t));

	Mem_Free (dat);

	return &pic->pic;
}

qpic_t *Draw_CachePic (const char *path)
{
	qpic_t *pic = Draw_TryCachePic (path, TEXPREF_ALPHA | TEXPREF_PAD | TEXPREF_NOPICMIP);
	if (!pic)
		Sys_Error ("Draw_CachePic: failed to load %s", path);
	return pic;
}

/*
================
Draw_MakePic -- johnfitz -- generate pics from internal data
================
*/
static qpic_t *Draw_MakePic (const char *name, int width, int height, const byte *data)
{
	int		flags = TEXPREF_NEAREST | TEXPREF_ALPHA | TEXPREF_PERSIST | TEXPREF_NOPICMIP | TEXPREF_PAD;
	qpic_t *pic;
	glpic_t gl;

	pic = (qpic_t *)Mem_Alloc (sizeof (qpic_t) - 4 + sizeof (glpic_t));
	pic->width = width;
	pic->height = height;

	gl.gltexture = TexMgr_LoadImage (NULL, name, width, height, SRC_INDEXED, (byte *)data, "", (src_offset_t)data, flags);
	gl.sl = 0;
	gl.sh = 1;
	gl.tl = 0;
	gl.th = 1;
	memcpy (pic->data, &gl, sizeof (glpic_t));

	return pic;
}

//==============================================================================
//
//  INIT
//
//==============================================================================

/*
===============
Draw_LoadPics -- johnfitz
===============
*/
static void Draw_LoadPics (void)
{
	byte		*data;
	src_offset_t offset;
	lumpinfo_t	*info;

	data = (byte *)W_GetLumpName ("conchars", &info);
	if (!data)
		Sys_Error ("Draw_LoadPics: couldn't load conchars");
	offset = (src_offset_t)data - (src_offset_t)wad_base;
	char_texture = TexMgr_LoadImage (
		NULL, WADFILENAME ":conchars", 128, 128, SRC_INDEXED, data, WADFILENAME, offset, TEXPREF_ALPHA | TEXPREF_NEAREST | TEXPREF_NOPICMIP | TEXPREF_CONCHARS);

	draw_disc = Draw_PicFromWad ("disc");
	draw_backtile = Draw_PicFromWad ("backtile");
}

/*
===============
Draw_NewGame -- johnfitz
===============
*/
void Draw_NewGame (void)
{
	cachepic_t *pic;
	int			i;

	// empty scrap and reallocate gltextures
	memset (scrap_allocated, 0, sizeof (scrap_allocated));
	memset (scrap_texels, 255, sizeof (scrap_texels));

	Scrap_Upload (); // creates 2 empty gltextures

	// empty lmp cache
	for (pic = menu_cachepics, i = 0; i < menu_numcachepics; pic++, i++)
		pic->name[0] = 0;
	menu_numcachepics = 0;

	// reload wad pics
	W_LoadWadFile (); // johnfitz -- filename is now hard-coded for honesty
	Draw_LoadPics ();
	SCR_LoadPics ();
	Sbar_LoadPics ();
	PR_ReloadPics (false);
}

/*
===============
Draw_Init -- johnfitz -- rewritten
===============
*/
void Draw_Init (void)
{
	Cvar_RegisterVariable (&scr_conalpha);

	// clear scrap and allocate gltextures
	memset (scrap_allocated, 0, sizeof (scrap_allocated));
	memset (scrap_texels, 255, sizeof (scrap_texels));

	Scrap_Upload (); // creates 2 empty textures

	// create internal pics
	pic_ins = Draw_MakePic ("ins", 8, 9, &pic_ins_data[0][0]);
	pic_ovr = Draw_MakePic ("ovr", 8, 8, &pic_ovr_data[0][0]);
	pic_nul = Draw_MakePic ("nul", 8, 8, &pic_nul_data[0][0]);

	// load game pics
	Draw_LoadPics ();
}

//==============================================================================
//
//  2D DRAWING
//
//==============================================================================

/* ==============================================================================
   The following code is making draw code explicit and not duplicated
   ============================================================================== */

static inline byte FloatToByteClamp (float a)
{
	if (a <= 0.0f)
		return 0;
	if (a >= 1.0f)
		return 255;
	return (byte)(a * 255.0f + 0.5f);
}

static inline void EnsureScrapUploaded (void)
{
	if (scrap_dirty)
		Scrap_Upload ();
}

static inline void EmitQuadAsTris (const basicvertex_t c[4], basicvertex_t *out6)
{
	out6[0] = c[0];
	out6[1] = c[1];
	out6[2] = c[2];
	out6[3] = c[2];
	out6[4] = c[3];
	out6[5] = c[0];
}

static inline void FillXYWH (basicvertex_t c[4], float x, float y, float w, float h)
{
	c[0].position[0] = x;
	c[0].position[1] = y;
	c[0].position[2] = 0.0f;
	c[1].position[0] = x + w;
	c[1].position[1] = y;
	c[1].position[2] = 0.0f;
	c[2].position[0] = x + w;
	c[2].position[1] = y + h;
	c[2].position[2] = 0.0f;
	c[3].position[0] = x;
	c[3].position[1] = y + h;
	c[3].position[2] = 0.0f;
}

static inline void FillUV (basicvertex_t c[4], float s1, float t1, float s2, float t2)
{
	c[0].texcoord[0] = s1;
	c[0].texcoord[1] = t1;
	c[1].texcoord[0] = s2;
	c[1].texcoord[1] = t1;
	c[2].texcoord[0] = s2;
	c[2].texcoord[1] = t2;
	c[3].texcoord[0] = s1;
	c[3].texcoord[1] = t2;
}

static inline void FillColor (basicvertex_t c[4], byte r, byte g, byte b, byte a)
{
	int i;
	for (i = 0; i < 4; ++i)
	{
		c[i].color[0] = r;
		c[i].color[1] = g;
		c[i].color[2] = b;
		c[i].color[3] = a;
	}
}

static inline void BindTexture (cb_context_t *cbx, gltexture_t *tex)
{
	vulkan_globals.vk_cmd_bind_descriptor_sets (
		cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_pipeline_layout.handle, 0, 1, &tex->descriptor_set, 0, NULL);
}

static inline void BindTexturedPipeline (cb_context_t *cbx, qboolean alpha_blend)
{
	R_BindPipeline (
		cbx, VK_PIPELINE_BIND_POINT_GRAPHICS,
		alpha_blend ? vulkan_globals.basic_blend_pipeline[cbx->render_pass_index] : vulkan_globals.basic_alphatest_pipeline[cbx->render_pass_index]);
}

static inline void BindNoTexBlendPipeline (cb_context_t *cbx)
{
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_notex_blend_pipeline[cbx->render_pass_index]);
}

#define CHAR_ST_SIZE	 (1.0f / 16.0f)
#define CHAR_TEXEL_EPS (0.001f)

	/*
================
Draw_FillCharacterQuad
================
*/
static void Draw_FillCharacterQuad (float x, float y, char num, basicvertex_t *output, int rotation)
{
	const int	row = (num >> 4) & 15;
	const int	col = num & 15;
	const float frow = row * CHAR_ST_SIZE;
	const float fcol = col * CHAR_ST_SIZE;

	const float s1 = fcol + CHAR_TEXEL_EPS;
	const float t1 = frow + CHAR_TEXEL_EPS;
	const float s2 = fcol + CHAR_ST_SIZE - CHAR_TEXEL_EPS;
	const float t2 = frow + CHAR_ST_SIZE - CHAR_TEXEL_EPS;

	/* rotate the screen-space quad by permuting corners; UVs stay axis-aligned per glyph */
	const float xy[4][2] = {{x, y}, {x + CHARACTER_SIZE, y}, {x + CHARACTER_SIZE, y + CHARACTER_SIZE}, {x, y + CHARACTER_SIZE}};

	basicvertex_t c[4];
	memset (c, 0, sizeof (c));
	c[0].position[0] = xy[(rotation + 0) % 4][0];
	c[0].position[1] = xy[(rotation + 0) % 4][1];
	c[0].position[2] = 0.0f;
	c[1].position[0] = xy[(rotation + 1) % 4][0];
	c[1].position[1] = xy[(rotation + 1) % 4][1];
	c[1].position[2] = 0.0f;
	c[2].position[0] = xy[(rotation + 2) % 4][0];
	c[2].position[1] = xy[(rotation + 2) % 4][1];
	c[2].position[2] = 0.0f;
	c[3].position[0] = xy[(rotation + 3) % 4][0];
	c[3].position[1] = xy[(rotation + 3) % 4][1];
	c[3].position[2] = 0.0f;

	FillUV (c, s1, t1, s2, t2);
	FillColor (c, 255, 255, 255, 255);
	EmitQuadAsTris (c, output);
}

/*
================
Draw_Character
================
*/
void Draw_Character (cb_context_t *cbx, float x, float y, int num)
{
	if (y <= -CHARACTER_SIZE)
		return; // totally off screen

	const int rotation = (num / 256) % 4;
	num &= 255;

	if (num == 32)
		return; // don't waste verts on spaces

	VkBuffer	   buffer;
	VkDeviceSize   buffer_offset;
	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (6 * sizeof (basicvertex_t), &buffer, &buffer_offset);

	Draw_FillCharacterQuad (x, y, (char)num, vertices, rotation);

	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	BindTexturedPipeline (cbx, false);
	BindTexture (cbx, char_texture);
	vulkan_globals.vk_cmd_draw (cbx->cb, 6, 1, 0, 0);
}

/*
================
Draw_String
================
*/
void Draw_String (cb_context_t *cbx, float x, float y, const char *str)
{
	int			num_verts = 0;
	int			i;
	const char *tmp;

	if (y <= -CHARACTER_SIZE)
		return; // totally off screen

	for (tmp = str; *tmp != 0; ++tmp)
		if (*tmp != 32)
			num_verts += 6;

	if (num_verts <= 0)
		return;

	VkBuffer	   buffer;
	VkDeviceSize   buffer_offset;
	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (num_verts * sizeof (basicvertex_t), &buffer, &buffer_offset);

	for (i = 0; *str != 0; ++str)
	{
		if (*str != 32)
		{
			Draw_FillCharacterQuad (x, y, *str, vertices + i * 6, 0);
			i++;
		}
		x += CHARACTER_SIZE;
	}

	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	BindTexturedPipeline (cbx, false);
	BindTexture (cbx, char_texture);
	vulkan_globals.vk_cmd_draw (cbx->cb, num_verts, 1, 0, 0);
}

/*
=============
Draw_Pic -- johnfitz -- modified
=============
*/
void Draw_Pic (cb_context_t *cbx, float x, float y, qpic_t *pic, float alpha, qboolean alpha_blend)
{
	glpic_t gl;
	alpha = CLAMP (0.0f, alpha, 1.0f);
	EnsureScrapUploaded ();
	memcpy (&gl, pic->data, sizeof (glpic_t));
	if (!gl.gltexture)
		return;

	memcpy (&gl, pic->data, sizeof (glpic_t));

	VkBuffer	   buffer;
	VkDeviceSize   buffer_offset;
	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (6 * sizeof (basicvertex_t), &buffer, &buffer_offset);

	basicvertex_t c[4];
	memset (c, 0, sizeof (c));
	FillXYWH (c, x, y, (float)pic->width, (float)pic->height);
	FillUV (c, gl.sl, gl.tl, gl.sh, gl.th);
	FillColor (c, 255, 255, 255, FloatToByteClamp (alpha));
	EmitQuadAsTris (c, vertices);

	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	BindTexturedPipeline (cbx, alpha_blend);
	BindTexture (cbx, gl.gltexture);
	vulkan_globals.vk_cmd_draw (cbx->cb, 6, 1, 0, 0);
}

void Draw_SubPic (cb_context_t *cbx, float x, float y, float w, float h, qpic_t *pic, float s1, float t1, float s2, float t2, float *rgb, float alpha)
{
	glpic_t	 gl;
	qboolean alpha_blend = alpha < 1.0f;
	if (alpha <= 0.0f)
		return;

	/* s2/t2 are extents; convert to end coords here */
	const float s_end = s1 + s2;
	const float t_end = t1 + t2;

	EnsureScrapUploaded ();
	memcpy (&gl, pic->data, sizeof (glpic_t));
	if (!gl.gltexture)
		return;

	VkBuffer	   buffer;
	VkDeviceSize   buffer_offset;
	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (6 * sizeof (basicvertex_t), &buffer, &buffer_offset);

	basicvertex_t c[4];
	memset (c, 0, sizeof (c));
	FillXYWH (c, x, y, w, h);
	/* lerp sub-rect in atlas space */
	const float u1 = gl.sl * (1.0f - s1) + s1 * gl.sh;
	const float v1 = gl.tl * (1.0f - t1) + t1 * gl.th;
	const float u2 = gl.sl * (1.0f - s_end) + s_end * gl.sh;
	const float v2 = gl.tl * (1.0f - t_end) + t_end * gl.th;
	FillUV (c, u1, v1, u2, v2);
	FillColor (c, (byte)(rgb[0] * 255.0f), (byte)(rgb[1] * 255.0f), (byte)(rgb[2] * 255.0f), FloatToByteClamp (alpha));
	EmitQuadAsTris (c, vertices);

	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	BindTexturedPipeline (cbx, alpha_blend);
	BindTexture (cbx, gl.gltexture);
	vulkan_globals.vk_cmd_draw (cbx->cb, 6, 1, 0, 0);
}

/*
=============
Draw_TransPicTranslate -- johnfitz -- rewritten to use texmgr to do translation

Only used for the player color selection menu
=============
*/
void Draw_TransPicTranslate (cb_context_t *cbx, float x, float y, qpic_t *pic, int top, int bottom)
{
	static int oldtop = -2;
	static int oldbottom = -2;

	if (top != oldtop || bottom != oldbottom)
	{
		glpic_t p;
		memcpy (&p, pic->data, sizeof (glpic_t));
		gltexture_t *glt = p.gltexture;
		oldtop = top;
		oldbottom = bottom;
		TexMgr_ReloadImage (glt, top, bottom);
	}
	Draw_Pic (cbx, x, y, pic, 1.0f, false);
}

/*
================
Draw_ConsoleBackground -- johnfitz -- rewritten
================
*/
void Draw_ConsoleBackground (cb_context_t *cbx)
{
	qpic_t *pic;
	float	alpha;

	pic = Draw_CachePic ("gfx/conback.lmp");
	pic->width = vid.conwidth;
	pic->height = vid.conheight;

	alpha = (con_forcedup) ? 1.0 : scr_conalpha.value;

	GL_SetCanvas (cbx, CANVAS_CONSOLE); // in case this is called from weird places

	if (alpha > 0.0)
	{
		Draw_Pic (cbx, 0, 0, pic, alpha, alpha < 1.0f);
	}
}

/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void Draw_TileClear (cb_context_t *cbx, float x, float y, float w, float h)
{
	glpic_t gl;
	memcpy (&gl, draw_backtile->data, sizeof (glpic_t));

	VkBuffer	   buffer;
	VkDeviceSize   buffer_offset;
	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (6 * sizeof (basicvertex_t), &buffer, &buffer_offset);

	basicvertex_t c[4];
	memset (c, 0, sizeof (c));
	FillXYWH (c, x, y, w, h);
	FillUV (c, x / 64.0f, y / 64.0f, (x + w) / 64.0f, (y + h) / 64.0f);
	FillColor (c, 255, 255, 255, 255);
	EmitQuadAsTris (c, vertices);

	BindTexturedPipeline (cbx, true /* blend to be safe for tile edges */);
	BindTexture (cbx, gl.gltexture);
	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	vulkan_globals.vk_cmd_draw (cbx->cb, 6, 1, 0, 0);
}

/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (cb_context_t *cbx, float x, float y, float w, float h, int c, float alpha) // johnfitz -- added alpha
{
	byte *pal = (byte *)d_8to24table; // johnfitz -- use d_8to24table instead of host_basepal
	if (w <= 0 || h <= 0 || alpha <= 0.0f)
		return;

	VkBuffer	   buffer;
	VkDeviceSize   buffer_offset;
	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (6 * sizeof (basicvertex_t), &buffer, &buffer_offset);

	basicvertex_t q[4];
	memset (q, 0, sizeof (q));
	FillXYWH (q, x, y, w, h);
	FillColor (q, pal[c * 4 + 0], pal[c * 4 + 1], pal[c * 4 + 2], FloatToByteClamp (alpha));
	EmitQuadAsTris (q, vertices);

	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	BindNoTexBlendPipeline (cbx);
	vulkan_globals.vk_cmd_draw (cbx->cb, 6, 1, 0, 0);
}

/*
================
Draw_FadeScreen
================
*/
void Draw_FadeScreen (cb_context_t *cbx)
{
	GL_SetCanvas (cbx, CANVAS_DEFAULT);

	VkBuffer	   buffer;
	VkDeviceSize   buffer_offset;
	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (6 * sizeof (basicvertex_t), &buffer, &buffer_offset);

	basicvertex_t q[4];
	memset (q, 0, sizeof (q));
	FillXYWH (q, 0.0f, 0.0f, (float)glwidth, (float)glheight);
	FillColor (q, 0, 0, 0, 128);
	EmitQuadAsTris (q, vertices);

	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	BindNoTexBlendPipeline (cbx);
	vulkan_globals.vk_cmd_draw (cbx->cb, 6, 1, 0, 0);
}

/*
================
GL_OrthoMatrix
================
*/
static void GL_OrthoMatrix (cb_context_t *cbx, float left, float right, float bottom, float top, float n, float f)
{
	float tx = -(right + left) / (right - left);
	float ty = (top + bottom) / (top - bottom);
	float tz = -(f + n) / (f - n);

	float matrix[16];
	memset (&matrix, 0, sizeof (matrix));

	// First column
	matrix[0 * 4 + 0] = 2.0f / (right - left);

	// Second column
	matrix[1 * 4 + 1] = -2.0f / (top - bottom);

	// Third column
	matrix[2 * 4 + 2] = -2.0f / (f - n);

	// Fourth column
	matrix[3 * 4 + 0] = tx;
	matrix[3 * 4 + 1] = ty;
	matrix[3 * 4 + 2] = tz;
	matrix[3 * 4 + 3] = 1.0f;

	R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16 * sizeof (float), matrix);
}

/*
================
GL_Viewport
================
*/
void GL_Viewport (cb_context_t *cbx, float x, float y, float width, float height, float min_depth, float max_depth)
{
	VkViewport viewport;
	viewport.x = x;
	viewport.y = vid.height - (y + height);
	viewport.width = width;
	viewport.height = height;
	viewport.minDepth = min_depth;
	viewport.maxDepth = max_depth;

	vkCmdSetViewport (cbx->cb, 0, 1, &viewport);
}

/*
================
GL_SetCanvas -- johnfitz -- support various canvas types
================
*/
void GL_SetCanvas (cb_context_t *cbx, canvastype newcanvas)
{
	if (newcanvas == cbx->current_canvas)
		return;

	extern vrect_t scr_vrect;
	float		   s, u, v;
	int			   lines;

	cbx->current_canvas = newcanvas;

	switch (newcanvas)
	{
	case CANVAS_NONE:
		break;
	case CANVAS_DEFAULT:
		GL_OrthoMatrix (cbx, 0, glwidth, glheight, 0, -99999, 99999);
		GL_Viewport (cbx, 0, 0, glwidth, glheight, 0.0f, 1.0f);
		break;
	case CANVAS_CONSOLE:
		lines = vid.conheight - (scr_con_current * vid.conheight / glheight);
		GL_OrthoMatrix (cbx, 0, vid.conwidth, vid.conheight + lines, lines, -99999, 99999);
		GL_Viewport (cbx, 0, 0, glwidth, glheight, 0.0f, 1.0f);
		break;
	case CANVAS_MENU:
		s = q_min ((float)glwidth / 320.0, (float)glheight / 200.0);
		s = CLAMP (1.0, M_GetScale (), s);
		u = (glwidth - (320.0f * s)) / (2.0f * s);
		v = (glheight - (200.0f * s)) / (2.0f * s);
		GL_OrthoMatrix (cbx, -u, 320.0f + u, 200.0f + v, -v, -99999, 99999);
		GL_Viewport (cbx, 0, 0, glwidth, glheight, 0.0f, 1.0f);
		break;
	case CANVAS_CSQC:
		s = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
		GL_OrthoMatrix (cbx, 0, glwidth / s, glheight / s, 0, -99999, 99999);
		GL_Viewport (cbx, 0, 0, glwidth, glheight, 0.0f, 1.0f);
		break;
	case CANVAS_SBAR:
		s = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
		if (cl.gametype == GAME_DEATHMATCH)
		{
			GL_OrthoMatrix (cbx, 0, glwidth / s, 48, 0, -99999, 99999);
			GL_Viewport (cbx, 0, 0, glwidth, 48 * s, 0.0f, 1.0f);
		}
		else
		{
			GL_OrthoMatrix (cbx, 0, 320, 48, 0, -99999, 99999);
			GL_Viewport (cbx, (glwidth - 320 * s) / 2, 0, 320 * s, 48 * s, 0.0f, 1.0f);
		}
		break;
	case CANVAS_WARPIMAGE:
		GL_OrthoMatrix (cbx, 0, 128, 0, 128, -99999, 99999);
		GL_Viewport (cbx, 0, glheight - WARPIMAGESIZE, WARPIMAGESIZE, WARPIMAGESIZE, 0.0f, 1.0f);
		break;
	case CANVAS_CROSSHAIR: // 0,0 is center of viewport
		s = CLAMP (1.0, scr_crosshairscale.value, 10.0);
		GL_OrthoMatrix (cbx, scr_vrect.width / -2 / s, scr_vrect.width / 2 / s, scr_vrect.height / 2 / s, scr_vrect.height / -2 / s, -99999, 99999);
		GL_Viewport (cbx, scr_vrect.x, glheight - scr_vrect.y - scr_vrect.height, scr_vrect.width & ~1, scr_vrect.height & ~1, 0.0f, 1.0f);
		break;
	case CANVAS_BOTTOMLEFT:				   // used by devstats
		s = (float)glwidth / vid.conwidth; // use console scale
		GL_OrthoMatrix (cbx, 0, 320, 200, 0, -99999, 99999);
		GL_Viewport (cbx, 0, 0, 320 * s, 200 * s, 0.0f, 1.0f);
		break;
	case CANVAS_BOTTOMRIGHT:			   // used by fps/clock
		s = (float)glwidth / vid.conwidth; // use console scale
		GL_OrthoMatrix (cbx, 0, 320, 200, 0, -99999, 99999);
		GL_Viewport (cbx, glwidth - 320 * s, 0, 320 * s, 200 * s, 0.0f, 1.0f);
		break;
	case CANVAS_TOPRIGHT: // used by disc
		s = 1;
		GL_OrthoMatrix (cbx, 0, 320, 200, 0, -99999, 99999);
		GL_Viewport (cbx, glwidth - 320 * s, glheight - 200 * s, 320 * s, 200 * s, 0.0f, 1.0f);
		break;
	default:
		Sys_Error ("GL_SetCanvas: bad canvas type");
	}
}

//==============================================================================
//
//  3D BILLBOARD DRAWING
//
//==============================================================================

/*
================
Draw_FillCharacterQuad_3D
================
*/
static void Draw_FillCharacterQuad_3D (vec3_t coords, float xoff, float yoff, float size, char num, basicvertex_t *output)
{
	int	  row, col;
	float frow, fcol, tile_size;

	xoff *= size;
	yoff *= size;

	row = num >> 4;
	col = num & 15;

	frow = row * 0.0625;
	fcol = col * 0.0625;
	tile_size = 0.0625;

	basicvertex_t corner_verts[4];
	memset (&corner_verts, 0, sizeof (corner_verts));

	VectorMA (coords, size / 2 - yoff, vup, &corner_verts[0].position[0]);
	VectorMA (&corner_verts[0].position[0], -size / 2 + xoff, vright, &corner_verts[0].position[0]);
	corner_verts[0].texcoord[0] = fcol;
	corner_verts[0].texcoord[1] = frow;

	VectorMA (&corner_verts[0].position[0], size, vright, &corner_verts[1].position[0]);
	corner_verts[1].texcoord[0] = fcol + tile_size;
	corner_verts[1].texcoord[1] = frow;

	VectorMA (&corner_verts[1].position[0], -size, vup, &corner_verts[2].position[0]);
	corner_verts[2].texcoord[0] = fcol + tile_size;
	corner_verts[2].texcoord[1] = frow + tile_size;

	VectorMA (&corner_verts[2].position[0], -size, vright, &corner_verts[3].position[0]);
	corner_verts[3].texcoord[0] = fcol;
	corner_verts[3].texcoord[1] = frow + tile_size;
	FillColor (corner_verts, 255, 255, 255, 255);

	output[0] = corner_verts[0];
	output[1] = corner_verts[1];
	output[2] = corner_verts[2];
	output[3] = corner_verts[2];
	output[4] = corner_verts[3];
	output[5] = corner_verts[0];
}

/*
================
Draw_String_3D
================
*/
void Draw_String_3D (cb_context_t *cbx, vec3_t coords, float size, const char *str)
{
	int			num_verts = 0;
	int			i;
	const char *tmp;
	float		xoff;

	for (tmp = str; *tmp != 0; ++tmp)
		if (*tmp != 32)
			num_verts += 6;

	VkBuffer	   buffer;
	VkDeviceSize   buffer_offset;
	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (num_verts * sizeof (basicvertex_t), &buffer, &buffer_offset);

	xoff = -0.5f * strlen (str) + 0.5f;

	for (i = 0; *str != 0; ++str)
	{
		if (*str != 32)
		{
			Draw_FillCharacterQuad_3D (coords, xoff, 0, size, *str, vertices + i * 6);
			i++;
		}
		xoff += 1.0f;
	}

	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_alphatest_pipeline[cbx->render_pass_index]);
	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	BindTexture (cbx, char_texture);
	vulkan_globals.vk_cmd_draw (cbx->cb, num_verts, 1, 0, 0);
}
