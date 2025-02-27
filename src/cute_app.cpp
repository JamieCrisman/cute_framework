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

#include <cute_app.h>
#include <cute_alloc.h>
#include <cute_audio.h>
#include <cute_concurrency.h>
#include <cute_file_system.h>
#include <cute_file_system_utils.h>
#include <cute_net.h>
#include <cute_c_runtime.h>
#include <cute_kv.h>
#include <cute_font.h>

#include <internal/cute_app_internal.h>
#include <internal/cute_file_system_internal.h>
#include <internal/cute_net_internal.h>
#include <internal/cute_crypto_internal.h>
#include <internal/cute_audio_internal.h>
#include <internal/cute_input_internal.h>
#include <internal/cute_dx11.h>
#include <internal/cute_font_internal.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#ifdef CUTE_WINDOWS
#	include <SDL_syswm.h>
#endif

#define CUTE_SOUND_FORCE_SDL
#include <cute/cute_sound.h>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#define SOKOL_IMPL
#define SOKOL_TRACE_HOOKS
#ifdef SOKOL_D3D11
#	define D3D11_NO_HELPERS
#endif
#include <sokol/sokol_gfx.h>

#define SOKOL_IMGUI_IMPL
#define SOKOL_IMGUI_NO_SOKOL_APP
#include <internal/imgui/sokol_imgui.h>
#include <internal/imgui/imgui_impl_sdl.h>
#include <sokol/sokol_gfx_imgui.h>

#include <shaders/upscale_shader.h>

namespace cute
{

app_t* app;

// TODO: Refactor to use error_t reporting.

error_t app_make(const char* window_title, int x, int y, int w, int h, uint32_t options, const char* argv0, void* user_allocator_context)
{
	SDL_SetMainReady();

#ifdef CUTE_EMSCRIPTEN
	Uint32 sdl_options = SDL_INIT_EVENTS | SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER;
#else
	Uint32 sdl_options = SDL_INIT_EVENTS | SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC;
	bool needs_video = options & (CUTE_APP_OPTIONS_OPENGL_CONTEXT | CUTE_APP_OPTIONS_OPENGLES_CONTEXT | CUTE_APP_OPTIONS_D3D11_CONTEXT | CUTE_APP_OPTIONS_DEFAULT_GFX_CONTEXT);
	if (!needs_video) {
		sdl_options &= ~SDL_INIT_VIDEO;
	}
#endif
	if (SDL_Init(sdl_options)) {
		return error_failure("SDL_Init failed");
	}

	if (options & CUTE_APP_OPTIONS_DEFAULT_GFX_CONTEXT) {
#ifdef CUTE_WINDOWS
		options |= CUTE_APP_OPTIONS_D3D11_CONTEXT;
#elif CUTE_EMSCRIPTEN
		options |= CUTE_APP_OPTIONS_OPENGLES_CONTEXT;
#else
		options |= CUTE_APP_OPTIONS_OPENGL_CONTEXT;
#endif
	}

	if (options & (CUTE_APP_OPTIONS_D3D11_CONTEXT | CUTE_APP_OPTIONS_OPENGLES_CONTEXT | CUTE_APP_OPTIONS_OPENGL_CONTEXT)) {
		// D3D11 crashes if w/h are not positive.
		w = w <= 0 ? 1 : w;
		h = h <= 0 ? 1 : h;
	}

	Uint32 flags = 0;
	if (options & CUTE_APP_OPTIONS_OPENGL_CONTEXT) flags |= SDL_WINDOW_OPENGL;
	if (options & CUTE_APP_OPTIONS_OPENGLES_CONTEXT) flags |= SDL_WINDOW_OPENGL;
	if (options & CUTE_APP_OPTIONS_FULLSCREEN) flags |= SDL_WINDOW_FULLSCREEN;
	if (options & CUTE_APP_OPTIONS_RESIZABLE) flags |= SDL_WINDOW_RESIZABLE;
	if (options & CUTE_APP_OPTIONS_HIDDEN) flags |= (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED);

	if (options & CUTE_APP_OPTIONS_OPENGL_CONTEXT) {
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	}

	if (options & CUTE_APP_OPTIONS_OPENGLES_CONTEXT) {
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	}

	SDL_Window* window;
	if (options & CUTE_APP_OPTIONS_WINDOW_POS_CENTERED) {
		window = SDL_CreateWindow(window_title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, flags);
	} else {
		window = SDL_CreateWindow(window_title, x, y, w, h, flags);
	}
	app_t* app = (app_t*)CUTE_ALLOC(sizeof(app_t), user_allocator_context);
	CUTE_PLACEMENT_NEW(app) app_t;
	app->options = options;
	app->window = window;
	app->mem_ctx = user_allocator_context;
	app->w = w;
	app->h = h;
	app->x = x;
	app->y = y;
	app->offscreen_w = w;
	app->offscreen_h = h;
	cute::app = app;

#ifdef CUTE_WINDOWS
	SDL_SysWMinfo wmInfo;
	SDL_VERSION(&wmInfo.version);
	SDL_GetWindowWMInfo(window, &wmInfo);
	HWND hwnd = wmInfo.info.win.window;
	app->platform_handle = hwnd;
#else
	void* hwnd = NULL;
#endif

	if ((options & CUTE_APP_OPTIONS_OPENGL_CONTEXT) | (options & CUTE_APP_OPTIONS_OPENGLES_CONTEXT)) {
		SDL_GL_SetSwapInterval(0);
		SDL_GLContext ctx = SDL_GL_CreateContext(window);
		if (!ctx) {
			CUTE_FREE(app, user_allocator_context);
			return error_failure("Unable to create OpenGL context.");
		}
		CUTE_MEMSET(&app->gfx_ctx_params, 0, sizeof(app->gfx_ctx_params));
		app->gfx_ctx_params.color_format = SG_PIXELFORMAT_RGBA8;
		app->gfx_ctx_params.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
		sg_desc params = { 0 };
		params.context = app->gfx_ctx_params;
		sg_setup(params);
		app->gfx_enabled = true;
		font_init();
	}

	if (options & CUTE_APP_OPTIONS_D3D11_CONTEXT) {
		dx11_init(hwnd, w, h, 1);
		app->gfx_ctx_params = dx11_get_context();
		sg_desc params = { 0 };
		params.context = app->gfx_ctx_params;
		sg_setup(params);
		app->gfx_enabled = true;
		font_init();
	}

	int num_threads_to_spawn = core_count() - 1;
	if (num_threads_to_spawn) {
		app->threadpool = threadpool_create(num_threads_to_spawn, user_allocator_context);
	}

	error_t err = file_system_init(argv0);
	if (err.is_error()) {
		CUTE_ASSERT(0);
	} else if (!(options & CUTE_APP_OPTIONS_FILE_SYSTEM_DONT_DEFAULT_MOUNT)) {
		// Put the base directory (the path to the exe) onto the file system search path.
		file_system_mount(file_system_get_base_dir(), "");
	}

	app->strpool = make_strpool();

	return error_success();
}

void app_destroy()
{
	destroy_strpool(app->strpool);
	if (app->using_imgui) {
		simgui_shutdown();
		ImGui_ImplSDL2_Shutdown();
		sg_imgui_discard(&app->sg_imgui);
		app->using_imgui = false;
	}
	if (app->gfx_enabled) {
		sg_shutdown();
		dx11_shutdown();
	}
	if (app->cute_sound) cs_shutdown_context(app->cute_sound);
	SDL_DestroyWindow(app->window);
	SDL_Quit();
	cute_threadpool_destroy(app->threadpool);
	audio_system_destroy(app->audio_system);
	int schema_count = app->entity_parsed_schemas.count();
	kv_t** schemas = app->entity_parsed_schemas.items();
	for (int i = 0; i < schema_count; ++i) kv_destroy(schemas[i]);
	if (app->ase_cache) {
		aseprite_cache_destroy(app->ase_cache);
		batch_destroy(app->ase_batch);
	}
	if (app->png_cache) {
		png_cache_destroy(app->png_cache);
		batch_destroy(app->png_batch);
	}
	if (app->courier_new) {
		font_free((font_t*)app->courier_new);
	}
	app->~app_t();
	CUTE_FREE(app, app->mem_ctx);
	file_system_destroy();
}

bool app_is_running()
{
	return app->running;
}

void app_stop_running()
{
	app->running = 0;
}

void app_update(float dt)
{
	app->dt = dt;
	pump_input_msgs();
	if (app->audio_system) {
		audio_system_update(app->audio_system, dt);
#ifdef CUTE_EMSCRIPTEN
		app_do_mixing(app);
#endif // CUTE_EMSCRIPTEN
	}
	if (app->using_imgui) {
		simgui_new_frame(app->w, app->h, dt);
		ImGui_ImplSDL2_NewFrame(app->window);
	}

	if (app->gfx_enabled) {
		sg_pass_action pass_action = { 0 };
		pass_action.colors[0] = { SG_ACTION_CLEAR, { 0.4f, 0.65f, 0.7f, 1.0f } };
		if (app->offscreen_enabled) {
			sg_begin_pass(app->offscreen_pass, pass_action);
		} else {
			sg_begin_default_pass(pass_action, app->w, app->h);
		}
	}

	if (app->ase_batch) {
		batch_update(app->ase_batch);
	}
}

static void s_imgui_present()
{
	if (app->using_imgui) {
		ImGui::EndFrame();
		ImGui::Render();
		simgui_render();
	}
}

void app_present()
{
	if (app->offscreen_enabled) {
		sg_end_pass();

		sg_bindings bind = { 0 };
		bind.vertex_buffers[0] = app->quad;
		bind.fs_images[0] = app->offscreen_color_buffer;

		sg_pass_action clear_to_black = { 0 };
		clear_to_black.colors[0] = { SG_ACTION_CLEAR, { 0.0f, 0.0f, 0.0f, 1.0f } };
		sg_begin_default_pass(&clear_to_black, app->w, app->h);
		sg_apply_pipeline(app->offscreen_to_screen_pip);
		sg_apply_bindings(bind);
		upscale_vs_params_t vs_params = { app->upscale };
		sg_apply_uniforms(SG_SHADERSTAGE_VS, 0, SG_RANGE(vs_params));
		upscale_fs_params_t fs_params = { v2((float)app->offscreen_w, (float)app->offscreen_h) };
		sg_apply_uniforms(SG_SHADERSTAGE_FS, 0, SG_RANGE(fs_params));
		sg_draw(0, 6, 1);
		if (app->using_imgui) {
			sg_imgui_draw(&app->sg_imgui);
			s_imgui_present();
		}
		sg_end_pass();
	} else {
		if (app->using_imgui) {
			sg_imgui_draw(&app->sg_imgui);
			s_imgui_present();
		}
		sg_end_pass();
	}

	sg_commit();
	dx11_present();
	if (app->options & CUTE_APP_OPTIONS_OPENGL_CONTEXT) {
		SDL_GL_SwapWindow(app->window);
	}

	// Triple buffering on the font vertices.
	app->font_buffer.advance();
}

// TODO - Move these init functions into audio/net headers.

error_t app_init_net()
{
	error_t err = crypto_init();
	if (err.is_error()) return err;
	return net_init();
}

error_t app_init_audio(bool spawn_mix_thread, int max_simultaneous_sounds)
{
	int more_on_emscripten = 1;
#ifdef CUTE_EMSCRIPTEN
	more_on_emscripten = 4;
#endif
	app->cute_sound = cs_make_context(NULL, 44100, 1024 * more_on_emscripten, 0, app->mem_ctx);
	if (app->cute_sound) {
#ifndef CUTE_EMSCRIPTEN
		if (spawn_mix_thread) {
			cs_spawn_mix_thread(app->cute_sound);
			app->spawned_mix_thread = true;
		}
#endif // CUTE_EMSCRIPTEN
		app->audio_system = audio_system_make(max_simultaneous_sounds, app->mem_ctx);
		return error_success();
	} else {
		return error_failure(cs_error_reason);
	}
}

void app_do_mixing()
{
#ifdef CUTE_EMSCRIPTEN
	cs_mix(app->cute_sound);
#else
	if (app->spawned_mix_thread) {
		cs_mix(app->cute_sound);
	}
#endif // CUTE_EMSCRIPTEN
}

ImGuiContext* app_init_imgui(bool no_default_font)
{
	if (!app->gfx_enabled) return NULL;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	app->using_imgui = true;

	ImGui::StyleColorsDark();
	ImGui_SDL2_Init(app->window);
	simgui_desc_t imgui_params = { 0 };
	imgui_params.no_default_font = no_default_font;
	imgui_params.ini_filename = "imgui.ini";
	simgui_setup(imgui_params);
	sg_imgui_init(&app->sg_imgui);

	return ::ImGui::GetCurrentContext();
}

sg_imgui_t* app_get_sokol_imgui()
{
	if (!app->using_imgui) return NULL;
	return &app->sg_imgui;
}

strpool_t* app_get_strpool()
{
	return app->strpool;
}

static void s_quad(float x, float y, float sx, float sy, float* out)
{
	struct vertex_t
	{
		float x, y;
		float u, v;
	};

	vertex_t quad[6];

	quad[0].x = -0.5f; quad[0].y = 0.5f;  quad[0].u = 0; quad[0].v = 0;
	quad[1].x = 0.5f;  quad[1].y = -0.5f; quad[1].u = 1; quad[1].v = 1;
	quad[2].x = 0.5f;  quad[2].y = 0.5f;  quad[2].u = 1; quad[2].v = 0;

	quad[3].x = -0.5f; quad[3].y =  0.5f; quad[3].u = 0; quad[3].v = 0;
	quad[4].x = -0.5f; quad[4].y = -0.5f; quad[4].u = 0; quad[4].v = 1;
	quad[5].x = 0.5f;  quad[5].y = -0.5f; quad[5].u = 1; quad[5].v = 1;

	for (int i = 0; i < 6; ++i)
	{
		quad[i].x = quad[i].x * sx + x;
		quad[i].y = quad[i].y * sy + y;
	}

	CUTE_MEMCPY(out, quad, sizeof(quad));
}

error_t app_set_offscreen_buffer(int offscreen_w, int offscreen_h)
{
	if (app->offscreen_enabled) {
		CUTE_ASSERT(false); // Need to implement calling this to resize offscreen buffer at runtime.
		return error_success();
	}

	app->offscreen_enabled = true;
	app->offscreen_w = offscreen_w;
	app->offscreen_h = offscreen_h;

	// Create offscreen buffers.
	sg_image_desc buffer_params = { 0 };
	buffer_params.render_target = true;
	buffer_params.width = offscreen_w;
	buffer_params.height = offscreen_h;
	buffer_params.pixel_format = app->gfx_ctx_params.color_format;
	app->offscreen_color_buffer = sg_make_image(buffer_params);
	if (app->offscreen_color_buffer.id == SG_INVALID_ID) return error_failure("Unable to create offscreen color buffer.");
	buffer_params.pixel_format = app->gfx_ctx_params.depth_format;
	app->offscreen_depth_buffer = sg_make_image(buffer_params);
	if (app->offscreen_depth_buffer.id == SG_INVALID_ID) return error_failure("Unable to create offscreen depth buffer.");

	// Define pass to reference offscreen buffers.
	sg_pass_desc pass_params = { 0 };
	pass_params.color_attachments[0].image = app->offscreen_color_buffer;
	pass_params.depth_stencil_attachment.image = app->offscreen_depth_buffer;
	app->offscreen_pass = sg_make_pass(pass_params);
	if (app->offscreen_pass.id == SG_INVALID_ID) return error_failure("Unable to create offscreen pass.");

	// Initialize static geometry for the offscreen quad.
	float quad[4 * 6];
	s_quad(0, 0, 2, 2, quad);
	sg_buffer_desc quad_params = { 0 };
	quad_params.size = sizeof(quad);
	quad_params.data = SG_RANGE(quad);
	app->quad = sg_make_buffer(quad_params);
	if (app->quad.id == SG_INVALID_ID) return error_failure("Unable create static quad buffer.");

	// Setup upscaling shader, to draw the offscreen buffer onto the screen as a textured quad.
	app->offscreen_shader = sg_make_shader(upscale_shd_shader_desc(sg_query_backend()));
	if (app->offscreen_shader.id == SG_INVALID_ID) return error_failure("Unable create offscreen shader.");

	app->upscale = { (float)app->offscreen_w / (float)app->w, (float)app->offscreen_h / (float)app->h };

	// Setup offscreen rendering pipeline, to draw the offscreen buffer onto the screen.
	sg_pipeline_desc params = { 0 };
	params.layout.buffers[0].stride = sizeof(v2) * 2;
	params.layout.buffers[0].step_func = SG_VERTEXSTEP_PER_VERTEX;
	params.layout.buffers[0].step_rate = 1;
	params.layout.attrs[0].buffer_index = 0;
	params.layout.attrs[0].offset = 0;
	params.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
	params.layout.attrs[1].buffer_index = 0;
	params.layout.attrs[1].offset = sizeof(v2);
	params.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
	params.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
	params.shader = app->offscreen_shader;
	app->offscreen_to_screen_pip = sg_make_pipeline(params);
	if (app->offscreen_to_screen_pip.id == SG_INVALID_ID) return error_failure("Unable create offscreen pipeline.");

	return error_success();
}

void app_offscreen_size(int* offscreen_w, int* offscreen_h)
{
	*offscreen_w = app->offscreen_w;
	*offscreen_h = app->offscreen_h;
}

power_info_t app_power_info()
{
	power_info_t info;
	SDL_PowerState state = SDL_GetPowerInfo(&info.seconds_left, &info.percentage_left);
	switch (state) {
	case SDL_POWERSTATE_UNKNOWN: info.state = POWER_STATE_UNKNOWN;
	case SDL_POWERSTATE_ON_BATTERY: info.state = POWER_STATE_ON_BATTERY;
	case SDL_POWERSTATE_NO_BATTERY: info.state = POWER_STATE_NO_BATTERY;
	case SDL_POWERSTATE_CHARGING: info.state = POWER_STATE_CHARGING;
	case SDL_POWERSTATE_CHARGED: info.state = POWER_STATE_CHARGED;
	}
	return info;
}

void sleep(int milliseconds)
{
	SDL_Delay((Uint32)milliseconds);
}

}
