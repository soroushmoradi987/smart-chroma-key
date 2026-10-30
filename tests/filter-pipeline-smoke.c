#include <obs.h>
#include <graphics/vec4.h>
#include <util/platform.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
static gs_texture_t *input;
static uint8_t pixels[64 * 64 * 4];
static const char *name(void *unused)
{
	(void)unused;
	return "fixture";
}
static void *create(obs_data_t *settings, obs_source_t *source)
{
	(void)settings;
	return source;
}
static void destroy(void *data)
{
	(void)data;
}
static uint32_t dimension(void *data)
{
	(void)data;
	return 64;
}
static void render(void *data, gs_effect_t *unused)
{
	(void)data;
	(void)unused;
	gs_effect_t *e = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_effect_set_texture(gs_effect_get_param_by_name(e, "image"), input);
	while (gs_effect_loop(e, "Draw"))
		gs_draw_sprite(input, 0, 64, 64);
}
int main(int argc, char **argv)
{
	if (argc != 4 || !obs_startup("en-US", NULL, NULL))
		return 2;
	struct obs_video_info vi = {0};
	vi.graphics_module = argv[1];
	vi.fps_num = 30;
	vi.fps_den = 1;
	vi.base_width = vi.base_height = vi.output_width = vi.output_height = 64;
	vi.output_format = VIDEO_FORMAT_RGBA;
	vi.colorspace = VIDEO_CS_709;
	vi.range = VIDEO_RANGE_FULL;
	vi.scale_type = OBS_SCALE_BILINEAR;
	if (obs_reset_video(&vi) != OBS_VIDEO_SUCCESS)
		return 3;
	obs_module_t *module = NULL;
	if (obs_open_module(&module, argv[2], argv[3]) != MODULE_SUCCESS || !obs_init_module(module))
		return 4;
	struct obs_source_info info = {0};
	info.id = "sck_fixture";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.get_name = name;
	info.create = create;
	info.destroy = destroy;
	info.get_width = dimension;
	info.get_height = dimension;
	info.video_render = render;
	obs_register_source(&info);
	obs_source_t *source = obs_source_create_private("sck_fixture", "fixture", NULL);
	obs_data_t *settings = obs_data_create();
	obs_data_set_bool(settings, "semantic_protection", false);
	obs_data_set_int(settings, "screen_type", 0);
	obs_data_set_int(settings, "key_color", 0xff14b414);
	obs_data_set_double(settings, "base_key", 0.018);
	obs_data_set_double(settings, "edge_softness", 0.075);
	obs_source_t *filter = obs_source_create_private("smart_chroma_key_filter", "key", settings);
	obs_data_release(settings);
	if (!source || !filter)
		return 5;
	obs_source_filter_add(source, filter);
	obs_enter_graphics();
	input = gs_texture_create(64, 64, GS_RGBA, 1, NULL, GS_DYNAMIC);
	gs_texrender_t *target = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_stagesurf_t *stage = gs_stagesurface_create(64, 64, GS_RGBA);
	obs_leave_graphics();
	int ok = 1;
	for (int frame = 0; frame < 8; frame++) {
		if (frame == 2) {
			/* Inactive RVM settings must not change Classic output. */
			obs_data_t *change = obs_data_create();
			obs_data_set_double(change, "rvm_halo", 1.0);
			obs_data_set_int(change, "rvm_view", 3);
			obs_data_set_int(change, "rvm_screen", 2);
			obs_source_update(filter, change);
			obs_data_release(change);
		}
		for (int y = 0; y < 64; y++)
			for (int x = 0; x < 64; x++) {
				size_t i = (y * 64 + x) * 4;
				float coverage = frame < 7 ? fminf(fmaxf((30.0f - x) / 5.0f + 0.4f, 0.0f), 1.0f) : 0.0f;
				const int foreground[3] = {70, 36, 18};
				const int screen[3] = {20, 180, 20};
				for (int c = 0; c < 3; c++)
					pixels[i + c] = (uint8_t)lroundf(foreground[c] * coverage +
									 screen[c] * (1.0f - coverage));
				pixels[i + 3] = 255;
			}
		obs_enter_graphics();
		gs_texture_set_image(input, pixels, 64 * 4, false);
		gs_texrender_reset(target);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		if (!gs_texrender_begin(target, 64, 64))
			return 6;
		struct vec4 clear = {0};
		gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
		gs_ortho(0, 64, 0, 64, -100, 100);
		gs_set_cull_mode(GS_NEITHER);
		gs_enable_depth_test(false);
		obs_source_video_render(source);
		gs_texrender_end(target);
		gs_blend_state_pop();
		gs_stage_texture(stage, gs_texrender_get_texture(target));
		gs_flush();
		uint8_t *mapped;
		uint32_t stride;
		if (!gs_stagesurface_map(stage, &mapped, &stride))
			return 7;
		uint8_t *body = mapped + 32 * stride + 10 * 4, *edge = mapped + 32 * stride + 30 * 4,
			*bg = mapped + 32 * stride + 50 * 4;
		printf("pipeline frame=%d body=%u,%u,%u,%u edge=%u,%u,%u,%u screen=%u\n", frame, body[0], body[1],
		       body[2], body[3], edge[0], edge[1], edge[2], edge[3], bg[3]);
		if (frame < 7)
			ok &= body[3] > 250 && edge[3] > 80 && edge[3] < 125 && bg[3] < 4;
		else
			ok &= body[3] < 4 && edge[3] < 4 && bg[3] < 4;
		gs_stagesurface_unmap(stage);
		// Exercise the plugin's final draw under real alpha blending too;
		// raw texture checks alone cannot detect alpha applied twice.
		for (int backdrop = 0; backdrop <= 1; backdrop++) {
			gs_texrender_reset(target);
			gs_blend_state_push();
			gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE,
						   GS_BLEND_INVSRCALPHA);
			if (!gs_texrender_begin(target, 64, 64))
				return 6;
			vec4_set(&clear, (float)backdrop, (float)backdrop, (float)backdrop, 1.0f);
			gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
			gs_ortho(0, 64, 0, 64, -100, 100);
			obs_source_video_render(source);
			gs_texrender_end(target);
			gs_blend_state_pop();
			gs_stage_texture(stage, gs_texrender_get_texture(target));
			gs_flush();
			if (!gs_stagesurface_map(stage, &mapped, &stride))
				return 7;
			edge = mapped + 32 * stride + 30 * 4;
			const int fg_rgb[3] = {70, 36, 18};
			for (int c = 0; c < 3; c++) {
				float expected = frame < 7 ? fg_rgb[c] * 0.4f + backdrop * 255.0f * 0.6f
							   : backdrop * 255.0f;
				ok &= fabsf(edge[c] - expected) < 3.0f;
			}
			ok &= edge[3] >= 254;
			if (frame == 6 || frame == 7)
				printf("composite frame=%d background=%s edge=%u,%u,%u,%u\n", frame,
				       backdrop ? "white" : "black", edge[0], edge[1], edge[2], edge[3]);
			gs_stagesurface_unmap(stage);
		}
		obs_leave_graphics();
		os_sleep_ms(50);
	}
	obs_source_filter_remove(source, filter);
	obs_source_release(filter);
	obs_source_release(source);
	obs_enter_graphics();
	gs_texture_destroy(input);
	gs_texrender_destroy(target);
	gs_stagesurface_destroy(stage);
	obs_leave_graphics();
	obs_shutdown();
	return ok ? 0 : 8;
}
