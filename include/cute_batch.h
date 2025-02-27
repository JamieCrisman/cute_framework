/*
	Cute Framework
	Copyright (C) 2019 Randy Gaul https://randygaul.net

	This software is provided 'as-is', without any express or implied
	warranty.  In no event will the authors be held liable for any damages
	arising from the use of this software.

	Permission is granted to anyone to use this software for any purpose,
	including commercial applications, and to alter it and redistribute it
	freely, subject to the following restrictions:

	1. The origin of this software must not be misrepresented; you must not
	   claim that you wrote the original software. If you use this software
	   in a product, an acknowledgment in the product documentation would be
	   appreciated but is not required.
	2. Altered source versions must be plainly marked as such, and must not be
	   misrepresented as being the original software.
	3. This notice may not be removed or altered from any source distribution.
*/

#ifndef CUTE_BATCH_H
#define CUTE_BATCH_H

#include "cute_defines.h"
#include "cute_math.h"
#include "cute_error.h"
#include "cute_gfx.h"

// TODO - Customizeability of the shader.

namespace cute
{

/**
 * Represents a single image rendered as a quad.
 */
struct batch_sprite_t
{
	/**
	 * Unique identifier for this quad's image, as determined by you.
	 */
	uint64_t id;

	transform_t transform = make_transform(); // Position and location rotation of the quad.
	int w; // Width in pixels of the source image.
	int h; // Height in pixels of the source image.
	float scale_x; // Scaling along the quad's local x-axis in pixels.
	float scale_y; // Scaling along the quad's local y-axis in pixels.
	float alpha = 1.0f; // Applies additional alpha to this quad.

	int sort_bits = 0;
};

/**
 * The batch is used to buffer up many different drawable things and organize them into draw calls suitable for high-
 * performance rendering on the GPU. However, this batch is not your typical batcher. This one will build texture
 * atlases internally on the the fly, and periodically needs to fetch pixels to build atlases.
 * 
 * You don't have to worry about texture atlases at all and can build and ship your game with separate
 * images on disk.
 * 
 * If you'd like to read more about the implementation of the batcher and why this is a good idea, go ahead and read
 * the documentation in `cute_spritebatch.h` in the `cute` folder.
 */
struct batch_t;

/**
 * `get_pixels_fn` will be called periodically from within `batch_flush` whenever access to pixels in RAM are
 * needed to construct internal texture atlases to be sent to the GPU.
 * 
 * `image_id`      - Uniquely maps to a single image, as determined by you.
 * `buffer`        - Pointer to the memory where you need to fill in pixel data.
 * `bytes_to_fill` - Number of bytes to write to `buffer`.
 * `udata`         - The `udata` pointer that was originally passed to `batch_make` as `get_pixels_udata`.
 */
typedef void (get_pixels_fn)(uint64_t image_id, void* buffer, int bytes_to_fill, void* udata);

CUTE_API batch_t* CUTE_CALL batch_make(get_pixels_fn* get_pixels, void* get_pixels_udata, void* mem_ctx = NULL);
CUTE_API void CUTE_CALL batch_destroy(batch_t* b);

/**
 * Pushes sprite quad onto an internal buffer. Does no other logic.
 * 
 * To get your quad rendered, see `batch_flush`.
 * Don't forget to call `batch_update` at the beginning of each game loop.
 */
CUTE_API void CUTE_CALL batch_push(batch_t* b, batch_sprite_t sprite);

/**
 * All quads currently pushed onto the batch (see `batch_push`) will be converted to an internal draw call.
 */
CUTE_API error_t CUTE_CALL batch_flush(batch_t* b);

/**
 * Call this once at the beginning of each game loop.
 */
CUTE_API void CUTE_CALL batch_update(batch_t* b);

CUTE_API void CUTE_CALL batch_set_texture_wrap_mode(batch_t* b, sg_wrap wrap_mode);
CUTE_API void CUTE_CALL batch_set_texture_filter(batch_t* b, sg_filter filter);
CUTE_API void CUTE_CALL batch_set_projection(batch_t* b, matrix_t projection);
CUTE_API void CUTE_CALL batch_outlines(batch_t* b, bool use_outlines);
CUTE_API void CUTE_CALL batch_outlines_use_corners(batch_t* b, bool use_corners);
CUTE_API void CUTE_CALL batch_outlines_color(batch_t* b, color_t c);

CUTE_API void CUTE_CALL batch_push_m3x2(batch_t* b, m3x2 m);
CUTE_API void CUTE_CALL batch_pop_m3x2(batch_t* b);
CUTE_API void CUTE_CALL batch_push_scissor_box(batch_t* b, int x, int y, int w, int h);
CUTE_API void CUTE_CALL batch_pop_scissor_box(batch_t* b);
CUTE_API void CUTE_CALL batch_push_depth_state(batch_t* b, const sg_depth_state& depth_state);
CUTE_API void CUTE_CALL batch_push_depth_defaults(batch_t* b);
CUTE_API void CUTE_CALL batch_pop_depth_state(batch_t* b);
CUTE_API void CUTE_CALL batch_push_stencil_state(batch_t* b, const sg_stencil_state& depth_stencil_state);
CUTE_API void CUTE_CALL batch_push_stencil_defaults(batch_t* b);
CUTE_API void CUTE_CALL batch_pop_stencil_state(batch_t* b);
CUTE_API void CUTE_CALL batch_push_blend_state(batch_t* b, const sg_blend_state& blend_state);
CUTE_API void CUTE_CALL batch_push_blend_defaults(batch_t* b);
CUTE_API void CUTE_CALL batch_pop_blend_state(batch_t* b);
CUTE_API void CUTE_CALL batch_push_tint(batch_t* b, color_t c);
CUTE_API void CUTE_CALL batch_pop_tint(batch_t* b);

CUTE_API void CUTE_CALL batch_quad(batch_t* b, aabb_t bb, color_t c);
CUTE_API void CUTE_CALL batch_quad(batch_t* b, v2 p0, v2 p1, v2 p2, v2 p3, color_t c);
CUTE_API void CUTE_CALL batch_quad(batch_t* b, v2 p0, v2 p1, v2 p2, v2 p3, color_t c0, color_t c1, color_t c2, color_t c3);
CUTE_API void CUTE_CALL batch_quad_line(batch_t* b, aabb_t bb, float thickness, color_t c);
CUTE_API void CUTE_CALL batch_quad_line(batch_t* b, v2 p0, v2 p1, v2 p2, v2 p3, float thickness, color_t c);
CUTE_API void CUTE_CALL batch_quad_line(batch_t* b, v2 p0, v2 p1, v2 p2, v2 p3, float thickness, color_t c0, color_t c1, color_t c2, color_t c3);

CUTE_API void CUTE_CALL batch_circle(batch_t* b, v2 p, float r, int iters, color_t c);
CUTE_API void CUTE_CALL batch_circle_line(batch_t* b, v2 p, float r, int iters, float thickness, color_t c);
CUTE_API void CUTE_CALL batch_circle_arc(batch_t* b, v2 p, v2 center_of_arc, float range, int iters, color_t color);
CUTE_API void CUTE_CALL batch_circle_arc_line(batch_t* b, v2 p, v2 center_of_arc, float range, int iters, float thickness, color_t color);

CUTE_API void CUTE_CALL batch_capsule(batch_t* b, v2 p0, v2 p1, float r, int iters, color_t c);
CUTE_API void CUTE_CALL batch_capsule_line(batch_t* b, v2 p0, v2 p1, float r, int iters, float thickness, color_t c);

CUTE_API void CUTE_CALL batch_tri(batch_t* b, v2 p0, v2 p1, v2 p2, color_t c);
CUTE_API void CUTE_CALL batch_tri(batch_t* b, v2 p0, v2 p1, v2 p2, color_t c0, color_t c1, color_t c2);
CUTE_API void CUTE_CALL batch_tri_line(batch_t* b, v2 p0, v2 p1, v2 p2, float thickness, color_t c);
CUTE_API void CUTE_CALL batch_tri_line(batch_t* b, v2 p0, v2 p1, v2 p2, float thickness, color_t c0, color_t c1, color_t c2);

CUTE_API void CUTE_CALL batch_line(batch_t* b, v2 p0, v2 p1, float thickness, color_t c, bool antialias = false);
CUTE_API void CUTE_CALL batch_line(batch_t* b, v2 p0, v2 p1, float thickness, color_t c0, color_t c1, bool antialias = false);
CUTE_API void CUTE_CALL batch_polyline(batch_t* b, v2* points, int count, float thickness, color_t c, bool loop = false, bool antialias = false, int bevel_count = 0);


/**
 * Temporal texture information for a sprite. Is valid until the next call to `batch_flush`
 * is issued. Useful to render a sprite in an external system, e.g. Dear ImGui.
 */
struct temporary_image_t
{
	texture_t texture_id; // A handle representing the texture for this image.
	int w; // Width in pixels of the image.
	int h; // Height in pixels of the image.
	v2 u; // u coordinate of the image in the texture.
	v2 v; // v coordinate of the image in the texture.
};

CUTE_API temporary_image_t CUTE_CALL batch_fetch(batch_t* b, batch_sprite_t sprite);

}

#endif // CUTE_BATCH_H
