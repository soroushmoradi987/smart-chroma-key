#include <obs.h>
#include <graphics/vec4.h>
#include <util/platform.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
static gs_texture_t *input;
static uint8_t pixels[1920 * 1080 * 4];
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
static uint32_t width(void *data)
{
	(void)data;
	return 1920;
}
static uint32_t height(void *data) { (void)data; return 1080; }
static void render(void *data, gs_effect_t *unused)
{
	(void)data;
	(void)unused;
	gs_effect_t *e = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_effect_set_texture(gs_effect_get_param_by_name(e, "image"), input);
	while (gs_effect_loop(e, "Draw"))
		gs_draw_sprite(input, 0, 1920, 1080);
}
int main(int argc, char **argv)
{
	if (argc != 7 || !obs_startup("en-US", NULL, NULL))
		return 2;
	struct obs_video_info vi = {0};
	vi.graphics_module = argv[1];
	vi.fps_num = 30;
	vi.fps_den = 1;
	vi.base_width = vi.output_width = 1920;
	vi.base_height = vi.output_height = 1080;
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
	info.get_width = width;
	info.get_height = height;
	info.video_render = render;
	obs_register_source(&info);
	obs_source_t *source = obs_source_create_private("sck_fixture", "fixture", NULL);
	obs_data_t *settings = obs_data_create_from_json_file(argv[6]);
	if (!settings) return 9;
	#ifdef SCK_TEST_INTEGRATED
    obs_data_set_int(settings, "processing_method", 1);
    obs_data_set_double(settings, "rvm_halo", 0.35);
    obs_data_set_double(settings, "base_key", 0.123);
    obs_data_set_int(settings, "screen_type", 0);
    obs_data_set_bool(settings, "semantic_protection", false);
    obs_source_t *filter = obs_source_create_private("smart_chroma_key_filter", "key", settings);
#else
    obs_source_t *filter = obs_source_create_private("smart_rvm_preview_filter", "key", settings);
#endif
	obs_data_release(settings);
	if (!source || !filter)
		return 5;
	obs_source_filter_add(source, filter);
	obs_enter_graphics();
	input = gs_texture_create(1920, 1080, GS_RGBA, 1, NULL, GS_DYNAMIC);
	gs_texrender_t *target = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_stagesurf_t *stage = gs_stagesurface_create(1920, 1080, GS_RGBA);
	obs_leave_graphics();
	FILE *in = fopen(argv[4], "rb"), *out = fopen(argv[5], "wb");
	if (!in || !out) return 10;
	int frame = 0; int verified = 0;
	while (fread(pixels, 1, sizeof(pixels), in) == sizeof(pixels)) {
		#ifdef SCK_TEST_INTEGRATED
        // Toggle both ways, then return to RVM. Settings must survive the switch
        // and changing Classic's opacity must never affect RVM's output.
        if (frame == 40 || frame == 55 || frame == 70) {
            obs_data_t *change = obs_data_create();
            obs_data_set_int(change, "processing_method", frame == 40 ? 0 : 1);
            if (frame == 70) obs_data_set_double(change, "opacity", 0.0);
            obs_source_update(filter, change);
            obs_data_release(change);
            os_sleep_ms(100);
            obs_data_t *saved = obs_source_get_settings(filter);
            if (fabs(obs_data_get_double(saved, "base_key") - 0.123) > 1e-6 ||
                fabs(obs_data_get_double(saved, "rvm_halo") - 0.35) > 1e-6) return 12;
            obs_data_release(saved);
            obs_properties_t *props = obs_source_properties(filter);
            bool classic_visible = obs_property_visible(obs_properties_get(props, "classic_controls"));
            bool rvm_visible = obs_property_visible(obs_properties_get(props, "rvm_controls"));
            if (classic_visible != (frame == 40) || rvm_visible == classic_visible) return 13;
            obs_properties_destroy(props);
        }
#endif
        obs_enter_graphics();
        gs_texture_set_image(input, pixels, 1920 * 4, false);
		gs_texrender_reset(target);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		if (!gs_texrender_begin(target, 1920, 1080))
			return 6;
		struct vec4 clear = {0};
		gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
		gs_ortho(0, 1920, 0, 1080, -100, 100);
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
		if (frame >= 75) {
            const uint8_t *body = mapped + 900 * stride + 960 * 4;
            const uint8_t *background = mapped + 20 * stride + 20 * 4;
            if (body[3] > 230 && background[3] < 10) verified++;
        }
        for (int y = 0; y < 1080; y++)
			if (fwrite(mapped + y * stride, 1, 1920 * 4, out) != 1920 * 4) return 11;
		gs_stagesurface_unmap(stage);
		obs_leave_graphics();
		if (frame++ % 30 == 0) printf("baseline frame=%d\n", frame);
		os_sleep_ms(34);
	}
	fclose(in); fclose(out);
	obs_source_filter_remove(source, filter);
	obs_source_release(filter);
	obs_source_release(source);
	obs_enter_graphics();
	gs_texture_destroy(input);
	gs_texrender_destroy(target);
	gs_stagesurface_destroy(stage);
	obs_leave_graphics();
	obs_shutdown();
	printf("Verified RVM frames: %d\n", verified);
	return verified >= 20 ? 0 : 8;
}
