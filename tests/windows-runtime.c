/* Run against the shipped plugin/data using real libobs + Direct3D 11. */
#include <obs.h>
#include <graphics/vec4.h>
#include <util/platform.h>
#include <stdio.h>
#include <string.h>

static gs_texture_t *input;
static const char *fixture_name(void *unused) { (void)unused; return "fixture"; }
static void *fixture_create(obs_data_t *s, obs_source_t *source) { (void)s; return source; }
static void fixture_destroy(void *data) { (void)data; }
static uint32_t dimension(void *data) { (void)data; return 64; }
static void fixture_render(void *data, gs_effect_t *unused)
{
    (void)data; (void)unused;
    gs_effect_t *e = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_effect_set_texture(gs_effect_get_param_by_name(e, "image"), input);
    while (gs_effect_loop(e, "Draw")) gs_draw_sprite(input, 0, 64, 64);
}
static void setting(obs_source_t *filter, const char *key, double value, bool integer)
{
    obs_data_t *s = obs_data_create();
    if (integer) obs_data_set_int(s, key, (long long)value);
    else obs_data_set_double(s, key, value);
    obs_source_update(filter, s);
    obs_data_release(s);
    os_sleep_ms(100);
}
static bool sample(obs_source_t *source, gs_texrender_t *target, gs_stagesurf_t *stage, uint8_t out[4])
{
    obs_enter_graphics();
    gs_texrender_reset(target);
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
    bool ok = gs_texrender_begin(target, 64, 64);
    if (ok) {
        struct vec4 clear = {0};
        gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
        gs_ortho(0, 64, 0, 64, -100, 100);
        obs_source_video_render(source);
        gs_texrender_end(target);
        gs_stage_texture(stage, gs_texrender_get_texture(target));
        gs_flush();
        uint8_t *mapped = NULL; uint32_t stride = 0;
        ok = gs_stagesurface_map(stage, &mapped, &stride);
        if (ok) { memcpy(out, mapped + 32 * stride + 32 * 4, 4); gs_stagesurface_unmap(stage); }
    }
    gs_blend_state_pop();
    obs_leave_graphics();
    return ok;
}
int main(int argc, char **argv)
{
    if (argc != 4 || !obs_startup("en-US", NULL, NULL)) return 2;
    struct obs_video_info vi = {0};
    vi.graphics_module = "libobs-d3d11.dll";
    vi.fps_num = 30; vi.fps_den = 1;
    vi.base_width = vi.base_height = vi.output_width = vi.output_height = 64;
    vi.output_format = VIDEO_FORMAT_RGBA; vi.colorspace = VIDEO_CS_709;
    vi.range = VIDEO_RANGE_FULL; vi.scale_type = OBS_SCALE_BILINEAR;
    if (obs_reset_video(&vi) != OBS_VIDEO_SUCCESS) return 3;
    if (strcmp(argv[3], "shader-must-fail") == 0) {
        obs_enter_graphics();
        char *errors = NULL;
        gs_effect_t *e = gs_effect_create_from_file(argv[2], &errors);
        printf("Original Windows shader rejected: %s\n", e ? "NO" : "YES");
        if (errors) printf("%s\n", errors);
        bfree(errors);
        bool rejected = !e;
        if (e) gs_effect_destroy(e);
        obs_leave_graphics(); obs_shutdown();
        return rejected ? 0 : 20;
    }
    obs_module_t *module = NULL;
    if (obs_open_module(&module, argv[1], argv[2]) != MODULE_SUCCESS || !obs_init_module(module)) return 4;
    struct obs_source_info info = {0};
    info.id = "sck_windows_fixture"; info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
    info.get_name = fixture_name; info.create = fixture_create; info.destroy = fixture_destroy;
    info.get_width = dimension; info.get_height = dimension; info.video_render = fixture_render;
    obs_register_source(&info);
    obs_source_t *source = obs_source_create_private(info.id, "fixture", NULL);
    obs_source_t *filter = obs_source_create_private("smart_chroma_key_filter", "key", NULL);
    if (!source || !filter) return 5;
    obs_source_filter_add(source, filter);
    uint8_t pixels[64 * 64 * 4];
    for (int i = 0; i < 64 * 64; ++i) {
        pixels[4*i] = 160; pixels[4*i+1] = 70; pixels[4*i+2] = 40; pixels[4*i+3] = 255;
    }
    obs_enter_graphics();
    const uint8_t *ptr = pixels;
    input = gs_texture_create(64, 64, GS_RGBA, 1, &ptr, GS_DYNAMIC);
    gs_texrender_t *target = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    gs_stagesurf_t *stage = gs_stagesurface_create(64, 64, GS_RGBA);
    obs_leave_graphics();
    uint8_t rgba[4] = {0};
    bool ok = sample(source, target, stage, rgba) && rgba[3] > 250;
    printf("Classic opaque alpha=%u\n", rgba[3]);
    setting(filter, "opacity", 0.25, false);
    ok &= sample(source, target, stage, rgba) && rgba[3] > 60 && rgba[3] < 68;
    printf("Classic opacity slider alpha=%u (expected 64)\n", rgba[3]);
    setting(filter, "opacity", 1, false);
    setting(filter, "brightness", 0.2, false);
    ok &= sample(source, target, stage, rgba) && rgba[0] > 195;
    printf("Classic brightness slider red=%u (original 160)\n", rgba[0]);
    setting(filter, "brightness", 0, false);
    if (strcmp(argv[3], "classic-only") != 0) {
        obs_properties_t *p = obs_source_properties(filter);
        obs_property_t *method = obs_properties_get(p, "processing_method");
        ok &= method && obs_property_list_item_count(method) == 2 && !obs_property_list_item_disabled(method, 1);
        obs_properties_destroy(p);
        for (int i = 0; i < 64*64; ++i) { pixels[4*i] = 0; pixels[4*i+1] = 255; pixels[4*i+2] = 0; }
        obs_enter_graphics(); gs_texture_set_image(input, pixels, 64*4, false); obs_leave_graphics();
        setting(filter, "rvm_screen", 0, true);
        setting(filter, "rvm_view", 1, true);
        setting(filter, "processing_method", 1, true);
        bool white = false;
        for (int i = 0; i < 200 && !white; ++i) {
            if (!sample(source, target, stage, rgba)) return 6;
            white = rgba[0] > 240 && rgba[1] > 240 && rgba[2] > 240;
            os_sleep_ms(50);
        }
        printf("AI inference and white preview=%s RGB=%u,%u,%u\n", white ? "PASS" : "FAIL", rgba[0], rgba[1], rgba[2]);
        ok &= white;
        setting(filter, "rvm_view", 2, true);
        ok &= sample(source, target, stage, rgba) && rgba[0] < 15 && rgba[1] < 15 && rgba[2] < 15;
        printf("AI black preview RGB=%u,%u,%u\n", rgba[0], rgba[1], rgba[2]);
        setting(filter, "processing_method", 0, true);
        ok &= sample(source, target, stage, rgba) && rgba[3] < 4;
        printf("Return to Classic alpha=%u\n", rgba[3]);
        // A second AI session must load and release safely too.
        setting(filter, "processing_method", 1, true);
        for (int i = 0; i < 20; ++i) { sample(source, target, stage, rgba); os_sleep_ms(50); }
        setting(filter, "processing_method", 0, true);
    }
    obs_source_filter_remove(source, filter);
    obs_source_release(filter); obs_source_release(source);
    obs_enter_graphics(); gs_texture_destroy(input); gs_texrender_destroy(target); gs_stagesurface_destroy(stage); obs_leave_graphics();
    obs_shutdown();
    printf("Windows runtime checks: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 10;
}
