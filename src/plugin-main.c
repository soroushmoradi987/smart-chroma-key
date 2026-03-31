/*
Smart Chroma Key - Professional Green Screen Removal Plugin for OBS
Copyright (C) 2026 Soroush Moradi
*/

#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <plugin-support.h>
#include <math.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

struct smart_chroma_data {
	obs_source_t *context;
	gs_effect_t *effect;

	/* Chroma Key */
	struct vec4 key_color;
	float base_key;
	float edge_softness;
	float background_clean;

	/* Hair Detail */
	float hair_detail;
	float hair_softness;

	/* Edge Refinement */
	float edge_blur;
	float edge_shrink;
	float edge_grow;

	/* Despill & Hair */
	struct vec4 hair_color;
	float spill_reduction;
	float despill_light;
	float despill_dark;
	float color_inject_strength;

	/* Color Correction */
	float brightness;
	float contrast;
	float saturation;
	float gamma;
	float temperature;
	float tint;

	/* Key Processing */
	float key_sat_boost;

	/* Opacity */
	float opacity;

	/* Auto-tune */
	int auto_tune_phase; /* 0=idle, 1=render+stage, 2=map+read */
	gs_texrender_t *texrender;
	gs_stagesurf_t *stagesurf;
	uint32_t at_width;
	uint32_t at_height;
};

static const char *smart_chroma_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("SmartChromaKey");
}

static void smart_chroma_update(void *data, obs_data_t *settings)
{
	struct smart_chroma_data *f = data;

	uint32_t kc = (uint32_t)obs_data_get_int(settings, "key_color");
	vec4_from_rgba(&f->key_color, kc);
	f->base_key = (float)obs_data_get_double(settings, "base_key");
	f->edge_softness =
		(float)obs_data_get_double(settings, "edge_softness");
	f->background_clean =
		(float)obs_data_get_double(settings, "background_clean");

	f->hair_detail =
		(float)obs_data_get_double(settings, "hair_detail");
	f->hair_softness =
		(float)obs_data_get_double(settings, "hair_softness");

	f->edge_blur = (float)obs_data_get_double(settings, "edge_blur");
	f->edge_shrink = (float)obs_data_get_double(settings, "edge_shrink");
	f->edge_grow = (float)obs_data_get_double(settings, "edge_grow");

	uint32_t hc = (uint32_t)obs_data_get_int(settings, "hair_color");
	vec4_from_rgba(&f->hair_color, hc);
	f->spill_reduction =
		(float)obs_data_get_double(settings, "spill_reduction");
	f->despill_light =
		(float)obs_data_get_double(settings, "despill_light");
	f->despill_dark =
		(float)obs_data_get_double(settings, "despill_dark");
	f->color_inject_strength =
		(float)obs_data_get_double(settings, "color_inject_strength");

	f->brightness = (float)obs_data_get_double(settings, "brightness");
	f->contrast = (float)obs_data_get_double(settings, "contrast");
	f->saturation = (float)obs_data_get_double(settings, "saturation");
	f->gamma = (float)obs_data_get_double(settings, "gamma");
	f->temperature = (float)obs_data_get_double(settings, "temperature");
	f->tint = (float)obs_data_get_double(settings, "tint");

	f->key_sat_boost =
		(float)obs_data_get_double(settings, "key_sat_boost");

	f->opacity = (float)obs_data_get_double(settings, "opacity");
}

static void *smart_chroma_create(obs_data_t *settings, obs_source_t *context)
{
	struct smart_chroma_data *f =
		bzalloc(sizeof(struct smart_chroma_data));
	f->context = context;

	char *path = obs_module_file("smart-chroma-key.effect");
	obs_enter_graphics();
	f->effect = gs_effect_create_from_file(path, NULL);
	f->texrender = gs_texrender_create(GS_RGBA, 1);
	obs_leave_graphics();
	bfree(path);

	if (!f->effect) {
		obs_enter_graphics();
		gs_texrender_destroy(f->texrender);
		obs_leave_graphics();
		bfree(f);
		return NULL;
	}

	smart_chroma_update(f, settings);
	return f;
}

static void smart_chroma_destroy(void *data)
{
	struct smart_chroma_data *f = data;
	obs_enter_graphics();
	if (f->effect)
		gs_effect_destroy(f->effect);
	if (f->texrender)
		gs_texrender_destroy(f->texrender);
	if (f->stagesurf)
		gs_stagesurface_destroy(f->stagesurf);
	obs_leave_graphics();
	bfree(f);
}

/* Auto-tune phase 1: render source to texrender and stage to CPU */
static void auto_tune_phase1(struct smart_chroma_data *f)
{
	obs_source_t *target = obs_filter_get_target(f->context);
	if (!target) {
		f->auto_tune_phase = 0;
		return;
	}

	uint32_t w = obs_source_get_base_width(target);
	uint32_t h = obs_source_get_base_height(target);
	if (w == 0 || h == 0) {
		f->auto_tune_phase = 0;
		return;
	}

	f->at_width = w;
	f->at_height = h;

	/* Create or recreate stagesurface if needed */
	if (f->stagesurf) {
		uint32_t sw = gs_stagesurface_get_width(f->stagesurf);
		uint32_t sh = gs_stagesurface_get_height(f->stagesurf);
		if (sw != w || sh != h) {
			gs_stagesurface_destroy(f->stagesurf);
			f->stagesurf = NULL;
		}
	}
	if (!f->stagesurf)
		f->stagesurf = gs_stagesurface_create(w, h, GS_RGBA);

	if (!f->stagesurf) {
		f->auto_tune_phase = 0;
		return;
	}

	/* Render source to texrender */
	gs_texrender_reset(f->texrender);
	if (!gs_texrender_begin(f->texrender, w, h)) {
		f->auto_tune_phase = 0;
		return;
	}
	struct vec4 clear_color;
	vec4_zero(&clear_color);
	gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
	gs_ortho(0.0f, (float)w, 0.0f, (float)h, -1.0f, 1.0f);
	obs_source_video_render(target);
	gs_texrender_end(f->texrender);

	gs_texture_t *tex = gs_texrender_get_texture(f->texrender);
	if (!tex) {
		f->auto_tune_phase = 0;
		return;
	}

	/* Stage (async GPU->CPU copy) */
	gs_stage_texture(f->stagesurf, tex);

	/* Next frame will read it */
	f->auto_tune_phase = 2;
}

/* Auto-tune phase 2: map stagesurface and read pixel data */
static void auto_tune_phase2(struct smart_chroma_data *f)
{
	uint32_t w = f->at_width;
	uint32_t h = f->at_height;

	if (w == 0 || h == 0 || !f->stagesurf) {
		f->auto_tune_phase = 0;
		return;
	}

	uint8_t *video_data = NULL;
	uint32_t linesize = 0;

	if (!gs_stagesurface_map(f->stagesurf, &video_data, &linesize)) {
		f->auto_tune_phase = 0;
		return;
	}

	/* Sample 9 points around the borders (corners + edge midpoints) */
	float avg_r = 0.0f, avg_g = 0.0f, avg_b = 0.0f;
	int count = 0;
	uint32_t mx = w / 20; /* 5% margin */
	uint32_t my = h / 20;

	/* 9 sample positions: 4 corners + 4 edge midpoints + 1 top-center */
	uint32_t sx[9], sy[9];
	sx[0] = mx;       sy[0] = my;        /* top-left */
	sx[1] = w / 2;    sy[1] = my;        /* top-center */
	sx[2] = w - mx;   sy[2] = my;        /* top-right */
	sx[3] = mx;       sy[3] = h / 2;     /* mid-left */
	sx[4] = w - mx;   sy[4] = h / 2;     /* mid-right */
	sx[5] = mx;       sy[5] = h - my;    /* bottom-left */
	sx[6] = w / 2;    sy[6] = h - my;    /* bottom-center */
	sx[7] = w - mx;   sy[7] = h - my;    /* bottom-right */
	sx[8] = w / 4;    sy[8] = my;        /* quarter-top */

	for (int i = 0; i < 9; i++) {
		/* Sample 5x5 block around each point */
		for (int dy = -2; dy <= 2; dy++) {
			for (int dx = -2; dx <= 2; dx++) {
				uint32_t px = sx[i] + dx;
				uint32_t py = sy[i] + dy;
				if (px >= w || py >= h)
					continue;
				uint8_t *p =
					video_data + py * linesize + px * 4;
				avg_r += p[0] / 255.0f;
				avg_g += p[1] / 255.0f;
				avg_b += p[2] / 255.0f;
				count++;
			}
		}
	}

	gs_stagesurface_unmap(f->stagesurf);
	f->auto_tune_phase = 0;

	if (count == 0)
		return;

	avg_r /= count;
	avg_g /= count;
	avg_b /= count;

	/* Determine dominant channel */
	float green_diff = avg_g - fmaxf(avg_r, avg_b);

	/* Set key color (OBS color format: 0xAABBGGRR) */
	uint32_t key_color = 0xFF000000 |
			     ((uint32_t)(avg_b * 255.0f) << 16) |
			     ((uint32_t)(avg_g * 255.0f) << 8) |
			     ((uint32_t)(avg_r * 255.0f));

	obs_data_t *settings = obs_source_get_settings(f->context);

	obs_data_set_int(settings, "key_color", (long long)key_color);

	/* Auto-tune based on screen characteristics */
	float auto_base_key = -0.05f - green_diff * 0.2f;
	if (auto_base_key < -0.3f)
		auto_base_key = -0.3f;

	float auto_softness = 0.8f + green_diff * 0.4f;
	if (auto_softness > 1.0f)
		auto_softness = 1.0f;

	float auto_sat_boost = 1.0f + (1.0f - green_diff) * 0.8f;
	if (auto_sat_boost > 3.0f)
		auto_sat_boost = 3.0f;

	obs_data_set_double(settings, "base_key", (double)auto_base_key);
	obs_data_set_double(settings, "edge_softness", (double)auto_softness);
	obs_data_set_double(settings, "background_clean", 0.0);
	obs_data_set_double(settings, "spill_reduction", 0.8);
	obs_data_set_double(settings, "despill_light", 1.0);
	obs_data_set_double(settings, "despill_dark", 1.2);
	obs_data_set_double(settings, "key_sat_boost", (double)auto_sat_boost);
	obs_data_set_double(settings, "hair_detail", 0.4);
	obs_data_set_double(settings, "hair_softness", 0.1);
	obs_data_set_double(settings, "edge_blur", 0.1);
	obs_data_set_double(settings, "edge_shrink", 0.0);
	obs_data_set_double(settings, "edge_grow", 0.0);
	obs_data_set_double(settings, "color_inject_strength", 0.2);
	obs_data_set_double(settings, "brightness", -0.04);
	obs_data_set_double(settings, "contrast", 1.1);
	obs_data_set_double(settings, "saturation", 1.0);
	obs_data_set_double(settings, "gamma", 1.0);
	obs_data_set_double(settings, "temperature", 0.0);
	obs_data_set_double(settings, "tint", 0.0);
	obs_data_set_double(settings, "opacity", 1.0);

	obs_source_update(f->context, settings);
	obs_data_release(settings);
}

static bool auto_tune_clicked(obs_properties_t *props,
			       obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	struct smart_chroma_data *f = data;
	f->auto_tune_phase = 1;
	return true;
}

static obs_properties_t *smart_chroma_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();

	/* --- Auto Tune Button --- */
	obs_properties_add_button(p, "auto_tune",
				  obs_module_text("AutoTune"),
				  auto_tune_clicked);

	/* --- Chroma Key --- */
	obs_properties_add_color(p, "key_color",
				 obs_module_text("KeyColor"));
	obs_properties_add_float_slider(p, "base_key",
		obs_module_text("BaseKey"), -0.5, 1.0, 0.001);
	obs_properties_add_float_slider(p, "edge_softness",
		obs_module_text("EdgeSoftness"), 0.001, 1.0, 0.001);
	obs_properties_add_float_slider(p, "background_clean",
		obs_module_text("BackgroundClean"), 0.0, 1.0, 0.001);

	/* --- Hair Detail --- */
	obs_properties_add_float_slider(p, "hair_detail",
		obs_module_text("HairDetail"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(p, "hair_softness",
		obs_module_text("HairSoftness"), 0.0, 1.0, 0.01);

	/* --- Edge Refinement --- */
	obs_properties_add_float_slider(p, "edge_blur",
		obs_module_text("EdgeBlur"), 0.0, 5.0, 0.1);
	obs_properties_add_float_slider(p, "edge_shrink",
		obs_module_text("EdgeShrink"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(p, "edge_grow",
		obs_module_text("EdgeGrow"), 0.0, 1.0, 0.01);

	/* --- Despill & Hair --- */
	obs_properties_add_color(p, "hair_color",
				 obs_module_text("HairColor"));
	obs_properties_add_float_slider(p, "spill_reduction",
		obs_module_text("SpillReduction"), 0.0, 2.0, 0.01);
	obs_properties_add_float_slider(p, "despill_light",
		obs_module_text("DespillLight"), 0.0, 2.0, 0.01);
	obs_properties_add_float_slider(p, "despill_dark",
		obs_module_text("DespillDark"), 0.0, 2.0, 0.01);
	obs_properties_add_float_slider(p, "color_inject_strength",
		obs_module_text("ColorInjectStrength"), 0.0, 5.0, 0.01);

	/* --- Color Correction --- */
	obs_properties_add_float_slider(p, "brightness",
		obs_module_text("Brightness"), -1.0, 1.0, 0.01);
	obs_properties_add_float_slider(p, "contrast",
		obs_module_text("Contrast"), 0.0, 3.0, 0.01);
	obs_properties_add_float_slider(p, "saturation",
		obs_module_text("Saturation"), 0.0, 3.0, 0.01);
	obs_properties_add_float_slider(p, "gamma",
		obs_module_text("Gamma"), 0.1, 3.0, 0.01);
	obs_properties_add_float_slider(p, "temperature",
		obs_module_text("Temperature"), -1.0, 1.0, 0.01);
	obs_properties_add_float_slider(p, "tint",
		obs_module_text("Tint"), -1.0, 1.0, 0.01);

	/* --- Key Processing --- */
	obs_properties_add_float_slider(p, "key_sat_boost",
		obs_module_text("KeySatBoost"), 1.0, 5.0, 0.01);

	/* --- Opacity --- */
	obs_properties_add_float_slider(p, "opacity",
		obs_module_text("Opacity"), 0.0, 1.0, 0.01);

	return p;
}

static void smart_chroma_defaults(obs_data_t *s)
{
	obs_data_set_default_int(s, "key_color", 0xFF00FF00);
	obs_data_set_default_double(s, "base_key", 0.0);
	obs_data_set_default_double(s, "edge_softness", 0.05);
	obs_data_set_default_double(s, "background_clean", 0.0);

	obs_data_set_default_double(s, "hair_detail", 0.0);
	obs_data_set_default_double(s, "hair_softness", 0.5);

	obs_data_set_default_double(s, "edge_blur", 0.0);
	obs_data_set_default_double(s, "edge_shrink", 0.0);
	obs_data_set_default_double(s, "edge_grow", 0.0);

	obs_data_set_default_int(s, "hair_color", 0xFF05100D);
	obs_data_set_default_double(s, "spill_reduction", 1.0);
	obs_data_set_default_double(s, "despill_light", 1.0);
	obs_data_set_default_double(s, "despill_dark", 1.0);
	obs_data_set_default_double(s, "color_inject_strength", 0.0);

	obs_data_set_default_double(s, "brightness", 0.0);
	obs_data_set_default_double(s, "contrast", 1.0);
	obs_data_set_default_double(s, "saturation", 1.0);
	obs_data_set_default_double(s, "gamma", 1.0);
	obs_data_set_default_double(s, "temperature", 0.0);
	obs_data_set_default_double(s, "tint", 0.0);

	obs_data_set_default_double(s, "key_sat_boost", 1.0);

	obs_data_set_default_double(s, "opacity", 1.0);
}

static void smart_chroma_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct smart_chroma_data *f = data;

	if (!f->effect)
		return;

	/* Handle auto-tune request (two-phase: stage in frame 1, read in frame 2) */
	if (f->auto_tune_phase == 1) {
		auto_tune_phase1(f);
		/* phase1 sets phase to 2 on success, 0 on failure */
	} else if (f->auto_tune_phase == 2) {
		auto_tune_phase2(f);
	}

	if (!obs_source_process_filter_begin(f->context, GS_RGBA,
					     OBS_ALLOW_DIRECT_RENDERING))
		return;

	/* Compute texel size */
	obs_source_t *target = obs_filter_get_target(f->context);
	uint32_t tw = obs_source_get_base_width(target);
	uint32_t th = obs_source_get_base_height(target);
	struct vec2 ts;
	ts.x = (tw > 0) ? 1.0f / (float)tw : 0.0f;
	ts.y = (th > 0) ? 1.0f / (float)th : 0.0f;

	gs_effect_set_vec2(
		gs_effect_get_param_by_name(f->effect, "texel_size"), &ts);

	gs_effect_set_vec4(
		gs_effect_get_param_by_name(f->effect, "KeyColor"),
		&f->key_color);
	gs_effect_set_float(
		gs_effect_get_param_by_name(f->effect, "BaseKey"),
		f->base_key);
	gs_effect_set_float(
		gs_effect_get_param_by_name(f->effect, "EdgeSoftness"),
		f->edge_softness);
	gs_effect_set_float(
		gs_effect_get_param_by_name(f->effect, "BackgroundClean"),
		f->background_clean);

	gs_effect_set_float(
		gs_effect_get_param_by_name(f->effect, "HairDetail"),
		f->hair_detail);
	gs_effect_set_float(
		gs_effect_get_param_by_name(f->effect, "HairSoftness"),
		f->hair_softness);

	gs_effect_set_float(
		gs_effect_get_param_by_name(f->effect, "EdgeBlur"),
		f->edge_blur);
	gs_effect_set_float(
		gs_effect_get_param_by_name(f->effect, "EdgeShrink"),
		f->edge_shrink);
	gs_effect_set_float(
		gs_effect_get_param_by_name(f->effect, "EdgeGrow"),
		f->edge_grow);

	gs_effect_set_vec4(
		gs_effect_get_param_by_name(f->effect, "HairColor"),
		&f->hair_color);
	gs_effect_set_float(
		gs_effect_get_param_by_name(f->effect, "SpillReduction"),
		f->spill_reduction);
	gs_effect_set_float(
		gs_effect_get_param_by_name(f->effect, "DespillLight"),
		f->despill_light);
	gs_effect_set_float(
		gs_effect_get_param_by_name(f->effect, "DespillDark"),
		f->despill_dark);
	gs_effect_set_float(
		gs_effect_get_param_by_name(f->effect, "ColorInjectStrength"),
		f->color_inject_strength);

	gs_effect_set_float(
		gs_effect_get_param_by_name(f->effect, "Brightness"),
		f->brightness);
	gs_effect_set_float(
		gs_effect_get_param_by_name(f->effect, "Contrast"),
		f->contrast);
	gs_effect_set_float(
		gs_effect_get_param_by_name(f->effect, "Saturation"),
		f->saturation);
	gs_effect_set_float(
		gs_effect_get_param_by_name(f->effect, "Gamma"), f->gamma);
	gs_effect_set_float(
		gs_effect_get_param_by_name(f->effect, "Temperature"),
		f->temperature);
	gs_effect_set_float(
		gs_effect_get_param_by_name(f->effect, "Tint"), f->tint);

	gs_effect_set_float(
		gs_effect_get_param_by_name(f->effect, "KeySatBoost"),
		f->key_sat_boost);

	gs_effect_set_float(
		gs_effect_get_param_by_name(f->effect, "Opacity"),
		f->opacity);

	obs_source_process_filter_end(f->context, f->effect, 0, 0);
}

static struct obs_source_info smart_chroma_key_filter = {
	.id = "smart_chroma_key_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = smart_chroma_name,
	.create = smart_chroma_create,
	.destroy = smart_chroma_destroy,
	.update = smart_chroma_update,
	.get_properties = smart_chroma_properties,
	.get_defaults = smart_chroma_defaults,
	.video_render = smart_chroma_render,
};

bool obs_module_load(void)
{
	obs_register_source(&smart_chroma_key_filter);
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)",
		PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}
