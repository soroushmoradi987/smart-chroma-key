/*
Smart Chroma Key - Professional Green and Blue Screen Removal Plugin for OBS
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

enum smart_chroma_screen_type {
	SMART_CHROMA_SCREEN_GREEN = 0,
	SMART_CHROMA_SCREEN_BLUE = 1,
	SMART_CHROMA_SCREEN_AUTO = 2,
};

struct smart_chroma_effect_params {
	gs_eparam_t *texel_size;
	gs_eparam_t *previous_image;
	gs_eparam_t *history_valid;
	gs_eparam_t *temporal_stability;
	gs_eparam_t *screen_type;
	gs_eparam_t *key_color;
	gs_eparam_t *base_key;
	gs_eparam_t *edge_softness;
	gs_eparam_t *background_clean;
	gs_eparam_t *hair_detail;
	gs_eparam_t *hair_softness;
	gs_eparam_t *edge_blur;
	gs_eparam_t *edge_shrink;
	gs_eparam_t *edge_grow;
	gs_eparam_t *hair_color;
	gs_eparam_t *spill_reduction;
	gs_eparam_t *despill_light;
	gs_eparam_t *despill_dark;
	gs_eparam_t *color_inject_strength;
	gs_eparam_t *brightness;
	gs_eparam_t *contrast;
	gs_eparam_t *saturation;
	gs_eparam_t *gamma;
	gs_eparam_t *temperature;
	gs_eparam_t *tint;
	gs_eparam_t *key_sat_boost;
	gs_eparam_t *opacity;
};

struct smart_chroma_data {
	obs_source_t *context;
	gs_effect_t *effect;
	struct smart_chroma_effect_params params;

	/* Chroma Key */
	int screen_type;
	int detected_screen_type;
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
	float temporal_stability;

	/* Opacity */
	float opacity;

	/* Auto-tune */
	int auto_tune_phase; /* 0=idle, 1=render+stage, 2=map+read */
	gs_texrender_t *texrender;
	gs_stagesurf_t *stagesurf;
	uint32_t at_width;
	uint32_t at_height;

	/* Motion-aware temporal stabilization uses ping-pong output history. */
	gs_texrender_t *history[2];
	uint32_t history_width;
	uint32_t history_height;
	uint32_t history_index;
	bool history_valid;
};

static bool smart_chroma_cache_effect_params(struct smart_chroma_data *f)
{
	bool valid = true;

#define CACHE_EFFECT_PARAM(field, name)                                      \
	do {                                                                   \
		f->params.field =                                              \
			gs_effect_get_param_by_name(f->effect, name);             \
		if (!f->params.field) {                                         \
			obs_log(LOG_ERROR, "Effect parameter '%s' is missing",    \
				name);                                             \
			valid = false;                                              \
		}                                                              \
	} while (false)

	CACHE_EFFECT_PARAM(texel_size, "texel_size");
	CACHE_EFFECT_PARAM(previous_image, "previous_image");
	CACHE_EFFECT_PARAM(history_valid, "HistoryValid");
	CACHE_EFFECT_PARAM(temporal_stability, "TemporalStability");
	CACHE_EFFECT_PARAM(screen_type, "ScreenType");
	CACHE_EFFECT_PARAM(key_color, "KeyColor");
	CACHE_EFFECT_PARAM(base_key, "BaseKey");
	CACHE_EFFECT_PARAM(edge_softness, "EdgeSoftness");
	CACHE_EFFECT_PARAM(background_clean, "BackgroundClean");
	CACHE_EFFECT_PARAM(hair_detail, "HairDetail");
	CACHE_EFFECT_PARAM(hair_softness, "HairSoftness");
	CACHE_EFFECT_PARAM(edge_blur, "EdgeBlur");
	CACHE_EFFECT_PARAM(edge_shrink, "EdgeShrink");
	CACHE_EFFECT_PARAM(edge_grow, "EdgeGrow");
	CACHE_EFFECT_PARAM(hair_color, "HairColor");
	CACHE_EFFECT_PARAM(spill_reduction, "SpillReduction");
	CACHE_EFFECT_PARAM(despill_light, "DespillLight");
	CACHE_EFFECT_PARAM(despill_dark, "DespillDark");
	CACHE_EFFECT_PARAM(color_inject_strength, "ColorInjectStrength");
	CACHE_EFFECT_PARAM(brightness, "Brightness");
	CACHE_EFFECT_PARAM(contrast, "Contrast");
	CACHE_EFFECT_PARAM(saturation, "Saturation");
	CACHE_EFFECT_PARAM(gamma, "Gamma");
	CACHE_EFFECT_PARAM(temperature, "Temperature");
	CACHE_EFFECT_PARAM(tint, "Tint");
	CACHE_EFFECT_PARAM(key_sat_boost, "KeySatBoost");
	CACHE_EFFECT_PARAM(opacity, "Opacity");

#undef CACHE_EFFECT_PARAM

	return valid;
}

static float screen_difference(float r, float g, float b, int screen_type)
{
	return screen_type == SMART_CHROMA_SCREEN_BLUE ? b - fmaxf(r, g) : g - fmaxf(r, b);
}

static float clamp_float(float value, float minimum, float maximum)
{
	return fminf(fmaxf(value, minimum), maximum);
}

static const char *smart_chroma_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("SmartChromaKey");
}

static void smart_chroma_update(void *data, obs_data_t *settings)
{
	struct smart_chroma_data *f = data;
	int previous_screen_type = f->screen_type;
	int screen_type = (int)obs_data_get_int(settings, "screen_type");
	if (screen_type == SMART_CHROMA_SCREEN_BLUE || screen_type == SMART_CHROMA_SCREEN_AUTO)
		f->screen_type = screen_type;
	else
		f->screen_type = SMART_CHROMA_SCREEN_GREEN;

	if (f->screen_type != SMART_CHROMA_SCREEN_AUTO)
		f->detected_screen_type = f->screen_type;
	else if (previous_screen_type != SMART_CHROMA_SCREEN_AUTO && f->auto_tune_phase == 0)
		f->auto_tune_phase = 1;

	uint32_t kc = (uint32_t)obs_data_get_int(settings, "key_color");
	vec4_from_rgba(&f->key_color, kc);
	f->base_key = (float)obs_data_get_double(settings, "base_key");
	f->edge_softness = fmaxf((float)obs_data_get_double(settings, "edge_softness"), 0.001f);
	f->background_clean = fminf(fmaxf((float)obs_data_get_double(settings, "background_clean"), 0.0f), 0.999f);

	f->hair_detail = (float)obs_data_get_double(settings, "hair_detail");
	f->hair_softness = (float)obs_data_get_double(settings, "hair_softness");

	f->edge_blur = (float)obs_data_get_double(settings, "edge_blur");
	f->edge_shrink = fminf(fmaxf((float)obs_data_get_double(settings, "edge_shrink"), 0.0f), 0.999f);
	f->edge_grow = fminf(fmaxf((float)obs_data_get_double(settings, "edge_grow"), 0.0f), 0.999f);

	uint32_t hc = (uint32_t)obs_data_get_int(settings, "hair_color");
	vec4_from_rgba(&f->hair_color, hc);
	f->spill_reduction = (float)obs_data_get_double(settings, "spill_reduction");
	f->despill_light = (float)obs_data_get_double(settings, "despill_light");
	f->despill_dark = (float)obs_data_get_double(settings, "despill_dark");
	f->color_inject_strength = (float)obs_data_get_double(settings, "color_inject_strength");

	f->brightness = (float)obs_data_get_double(settings, "brightness");
	f->contrast = (float)obs_data_get_double(settings, "contrast");
	f->saturation = (float)obs_data_get_double(settings, "saturation");
	f->gamma = fmaxf((float)obs_data_get_double(settings, "gamma"), 0.1f);
	f->temperature = (float)obs_data_get_double(settings, "temperature");
	f->tint = (float)obs_data_get_double(settings, "tint");

	f->key_sat_boost = (float)obs_data_get_double(settings, "key_sat_boost");
	f->temporal_stability = clamp_float((float)obs_data_get_double(settings, "temporal_stability"), 0.0f, 1.0f);

	f->opacity = fminf(fmaxf((float)obs_data_get_double(settings, "opacity"), 0.0f), 1.0f);
	f->history_valid = false;
}

static void *smart_chroma_create(obs_data_t *settings, obs_source_t *context)
{
	struct smart_chroma_data *f = bzalloc(sizeof(struct smart_chroma_data));
	f->context = context;

	char *path = obs_module_file("smart-chroma-key.effect");
	if (!path) {
		obs_log(LOG_ERROR, "Could not locate 'smart-chroma-key.effect' in plugin data directory");
	} else {
		obs_log(LOG_INFO, "Loading effect from: %s", path);
	}

	obs_enter_graphics();
	f->effect = path ? gs_effect_create_from_file(path, NULL) : NULL;
	f->texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	f->history[0] = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	f->history[1] = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	if (f->effect && !smart_chroma_cache_effect_params(f)) {
		gs_effect_destroy(f->effect);
		f->effect = NULL;
	}
	obs_leave_graphics();
	bfree(path);

	if (!f->effect) {
		obs_log(LOG_ERROR,
			"Failed to load smart-chroma-key.effect; filter will pass through video, but properties remain available.");
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
	if (f->history[0])
		gs_texrender_destroy(f->history[0]);
	if (f->history[1])
		gs_texrender_destroy(f->history[1]);
	obs_leave_graphics();
	bfree(f);
}

/* Auto-tune phase 1: render source to texrender and stage to CPU */
static void auto_tune_phase1(struct smart_chroma_data *f)
{
	obs_source_t *target = obs_filter_get_target(f->context);
	if (!target || !f->texrender) {
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

	/* Analyze a dense border grid. The border is much more likely to contain
	 * clean screen than the frame center, while hundreds of samples make the
	 * estimate resistant to a person, props, wrinkles, and uneven lighting. */
	const uint32_t grid_width = 64;
	const uint32_t grid_height = 36;
	const uint32_t border_x = 8;
	const uint32_t border_y = 5;
	float green_score = 0.0f;
	float blue_score = 0.0f;
	uint32_t valid_samples = 0;

	for (uint32_t gy = 0; gy < grid_height; gy++) {
		for (uint32_t gx = 0; gx < grid_width; gx++) {
			bool is_border = gx < border_x || gx >= grid_width - border_x || gy < border_y ||
					 gy >= grid_height - border_y;
			if (!is_border)
				continue;

			uint32_t px = gx * (w - 1) / (grid_width - 1);
			uint32_t py = gy * (h - 1) / (grid_height - 1);
			uint8_t *p = video_data + py * linesize + px * 4;
			if (p[3] < 32)
				continue;

			float r = p[0] / 255.0f;
			float g = p[1] / 255.0f;
			float b = p[2] / 255.0f;
			float maximum = fmaxf(r, fmaxf(g, b));
			float minimum = fminf(r, fminf(g, b));
			float saturation = maximum - minimum;
			if (maximum < 0.025f || saturation < 0.02f)
				continue;

			float green_difference = fmaxf(screen_difference(r, g, b, SMART_CHROMA_SCREEN_GREEN), 0.0f);
			float blue_difference = fmaxf(screen_difference(r, g, b, SMART_CHROMA_SCREEN_BLUE), 0.0f);
			green_score += green_difference * green_difference * (0.25f + saturation);
			blue_score += blue_difference * blue_difference * (0.25f + saturation);
			valid_samples++;
		}
	}

	int detected_type = f->screen_type;
	if (detected_type == SMART_CHROMA_SCREEN_AUTO)
		detected_type = blue_score > green_score ? SMART_CHROMA_SCREEN_BLUE : SMART_CHROMA_SCREEN_GREEN;

	/* First robust pass: estimate the chromaticity centroid of plausible
	 * screen pixels. Selected-channel dominance is squared so foreground
	 * pixels touching the frame contribute very little. */
	float rough_r = 0.0f, rough_g = 0.0f, rough_b = 0.0f;
	float rough_weight = 0.0f;
	uint32_t candidate_samples = 0;
	for (uint32_t gy = 0; gy < grid_height; gy++) {
		for (uint32_t gx = 0; gx < grid_width; gx++) {
			bool is_border = gx < border_x || gx >= grid_width - border_x || gy < border_y ||
					 gy >= grid_height - border_y;
			if (!is_border)
				continue;

			uint32_t px = gx * (w - 1) / (grid_width - 1);
			uint32_t py = gy * (h - 1) / (grid_height - 1);
			uint8_t *p = video_data + py * linesize + px * 4;
			if (p[3] < 32)
				continue;

			float r = p[0] / 255.0f;
			float g = p[1] / 255.0f;
			float b = p[2] / 255.0f;
			float difference = screen_difference(r, g, b, detected_type);
			if (difference <= 0.015f)
				continue;

			float weight = difference * difference;
			rough_r += r * weight;
			rough_g += g * weight;
			rough_b += b * weight;
			rough_weight += weight;
			candidate_samples++;
		}
	}

	if (rough_weight <= 0.001f || valid_samples == 0) {
		gs_stagesurface_unmap(f->stagesurf);
		f->auto_tune_phase = 0;
		obs_log(LOG_WARNING,
			"Auto Tune could not find a sufficiently saturated green or blue screen near the frame borders");
		return;
	}

	rough_r /= rough_weight;
	rough_g /= rough_weight;
	rough_b /= rough_weight;
	float rough_sum = fmaxf(rough_r + rough_g + rough_b, 0.001f);
	float rough_cr = rough_r / rough_sum;
	float rough_cg = rough_g / rough_sum;
	float rough_cb = rough_b / rough_sum;

	/* Second pass: reject chromaticity outliers and calculate screen variance.
	 * The variance drives the matte tolerance, so a wrinkled/noisy screen gets
	 * enough latitude without erasing similarly colored foreground detail. */
	float avg_r = 0.0f, avg_g = 0.0f, avg_b = 0.0f;
	float keyed_weight = 0.0f;
	float distance_squared_sum = 0.0f;
	uint32_t accepted_samples = 0;
	for (uint32_t gy = 0; gy < grid_height; gy++) {
		for (uint32_t gx = 0; gx < grid_width; gx++) {
			bool is_border = gx < border_x || gx >= grid_width - border_x || gy < border_y ||
					 gy >= grid_height - border_y;
			if (!is_border)
				continue;

			uint32_t px = gx * (w - 1) / (grid_width - 1);
			uint32_t py = gy * (h - 1) / (grid_height - 1);
			uint8_t *p = video_data + py * linesize + px * 4;
			if (p[3] < 32)
				continue;

			float r = p[0] / 255.0f;
			float g = p[1] / 255.0f;
			float b = p[2] / 255.0f;
			float difference = screen_difference(r, g, b, detected_type);
			float sum = r + g + b;
			if (difference <= 0.015f || sum <= 0.025f)
				continue;

			float dr = r / sum - rough_cr;
			float dg = g / sum - rough_cg;
			float db = b / sum - rough_cb;
			float distance_squared = dr * dr + dg * dg + db * db;
			if (distance_squared > 0.0324f) /* 0.18 chromaticity radius */
				continue;

			float similarity = 1.0f - sqrtf(distance_squared) / 0.18f;
			float weight = difference * difference * similarity * similarity;
			avg_r += r * weight;
			avg_g += g * weight;
			avg_b += b * weight;
			distance_squared_sum += distance_squared * weight;
			keyed_weight += weight;
			accepted_samples++;
		}
	}

	gs_stagesurface_unmap(f->stagesurf);
	f->auto_tune_phase = 0;

	if (keyed_weight <= 0.001f || accepted_samples < 12)
		return;

	avg_r /= keyed_weight;
	avg_g /= keyed_weight;
	avg_b /= keyed_weight;
	float chroma_deviation = sqrtf(distance_squared_sum / keyed_weight);
	float key_difference = screen_difference(avg_r, avg_g, avg_b, detected_type);
	if (key_difference <= 0.01f) {
		obs_log(LOG_WARNING, "Auto Tune could not find enough %s screen color near the frame borders",
			detected_type == SMART_CHROMA_SCREEN_BLUE ? "blue" : "green");
		return;
	}
	f->detected_screen_type = detected_type;

	/* Set key color (OBS color format: 0xAABBGGRR) */
	uint32_t key_color = 0xFF000000 | ((uint32_t)(avg_b * 255.0f) << 16) | ((uint32_t)(avg_g * 255.0f) << 8) |
			     ((uint32_t)(avg_r * 255.0f));

	obs_data_t *settings = obs_source_get_settings(f->context);

	obs_data_set_int(settings, "key_color", (long long)key_color);

	/* Auto-tune based on screen characteristics */
	float coverage = (float)candidate_samples / (float)valid_samples;
	float auto_base_key = clamp_float(0.018f + chroma_deviation * 2.0f, 0.018f, 0.095f);
	float auto_softness = clamp_float(0.075f + chroma_deviation * 1.5f, 0.065f, 0.18f);
	float auto_sat_boost = clamp_float(1.15f + (1.0f - key_difference) * 0.45f, 1.15f, 1.75f);

	obs_data_set_double(settings, "base_key", (double)auto_base_key);
	obs_data_set_double(settings, "edge_softness", (double)auto_softness);
	obs_data_set_double(settings, "background_clean", 0.04);
	obs_data_set_double(settings, "spill_reduction", 1.15);
	obs_data_set_double(settings, "despill_light", 1.0);
	obs_data_set_double(settings, "despill_dark", 0.9);
	obs_data_set_double(settings, "key_sat_boost", (double)auto_sat_boost);
	obs_data_set_double(settings, "temporal_stability", 0.72);
	obs_data_set_double(settings, "hair_detail", 0.85);
	obs_data_set_double(settings, "hair_softness", 0.35);
	obs_data_set_double(settings, "edge_blur", 0.65);
	obs_data_set_double(settings, "edge_shrink", 0.0);
	obs_data_set_double(settings, "edge_grow", 0.0);
	obs_data_set_double(settings, "color_inject_strength", 0.0);
	obs_data_set_double(settings, "brightness", 0.0);
	obs_data_set_double(settings, "contrast", 1.0);
	obs_data_set_double(settings, "saturation", 1.0);
	obs_data_set_double(settings, "gamma", 1.0);
	obs_data_set_double(settings, "temperature", 0.0);
	obs_data_set_double(settings, "tint", 0.0);
	obs_data_set_double(settings, "opacity", 1.0);

	obs_source_update(f->context, settings);
	obs_data_release(settings);

	obs_log(LOG_INFO,
		"Auto Tune detected a %s screen (coverage %.1f%%, chroma deviation %.4f, key RGB %.3f/%.3f/%.3f)",
		detected_type == SMART_CHROMA_SCREEN_BLUE ? "blue" : "green", coverage * 100.0f, chroma_deviation,
		avg_r, avg_g, avg_b);
}

static bool auto_tune_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	struct smart_chroma_data *f = data;
	f->auto_tune_phase = 1;
	return true;
}

static bool screen_type_modified(obs_properties_t *props, obs_property_t *property, obs_data_t *settings)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);

	int screen_type = (int)obs_data_get_int(settings, "screen_type");
	if (screen_type != SMART_CHROMA_SCREEN_AUTO) {
		uint32_t key_color = screen_type == SMART_CHROMA_SCREEN_BLUE ? 0xFFFF0000 : 0xFF00FF00;
		obs_data_set_int(settings, "key_color", (long long)key_color);
	}
	return true;
}

static obs_properties_t *smart_chroma_properties(void *data)
{
	obs_properties_t *p = obs_properties_create();

	/* --- Auto Tune Button --- */
	obs_properties_add_button2(p, "auto_tune", obs_module_text("AutoTune"), auto_tune_clicked, data);

	/* --- Chroma Key --- */
	obs_property_t *screen_type = obs_properties_add_list(p, "screen_type", obs_module_text("ScreenType"),
							      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(screen_type, obs_module_text("AutoScreen"), SMART_CHROMA_SCREEN_AUTO);
	obs_property_list_add_int(screen_type, obs_module_text("GreenScreen"), SMART_CHROMA_SCREEN_GREEN);
	obs_property_list_add_int(screen_type, obs_module_text("BlueScreen"), SMART_CHROMA_SCREEN_BLUE);
	obs_property_set_modified_callback(screen_type, screen_type_modified);
	obs_properties_add_color(p, "key_color", obs_module_text("KeyColor"));
	obs_properties_add_float_slider(p, "base_key", obs_module_text("BaseKey"), -0.5, 1.0, 0.001);
	obs_properties_add_float_slider(p, "edge_softness", obs_module_text("EdgeSoftness"), 0.001, 1.0, 0.001);
	obs_properties_add_float_slider(p, "background_clean", obs_module_text("BackgroundClean"), 0.0, 1.0, 0.001);

	/* --- Hair Detail --- */
	obs_properties_add_float_slider(p, "hair_detail", obs_module_text("HairDetail"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(p, "hair_softness", obs_module_text("HairSoftness"), 0.0, 1.0, 0.01);

	/* --- Edge Refinement --- */
	obs_properties_add_float_slider(p, "edge_blur", obs_module_text("EdgeBlur"), 0.0, 5.0, 0.1);
	obs_properties_add_float_slider(p, "edge_shrink", obs_module_text("EdgeShrink"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(p, "edge_grow", obs_module_text("EdgeGrow"), 0.0, 1.0, 0.01);

	/* --- Despill & Hair --- */
	obs_properties_add_color(p, "hair_color", obs_module_text("HairColor"));
	obs_properties_add_float_slider(p, "spill_reduction", obs_module_text("SpillReduction"), 0.0, 2.0, 0.01);
	obs_properties_add_float_slider(p, "despill_light", obs_module_text("DespillLight"), 0.0, 2.0, 0.01);
	obs_properties_add_float_slider(p, "despill_dark", obs_module_text("DespillDark"), 0.0, 2.0, 0.01);
	obs_properties_add_float_slider(p, "color_inject_strength", obs_module_text("ColorInjectStrength"), 0.0, 5.0,
					0.01);

	/* --- Color Correction --- */
	obs_properties_add_float_slider(p, "brightness", obs_module_text("Brightness"), -1.0, 1.0, 0.01);
	obs_properties_add_float_slider(p, "contrast", obs_module_text("Contrast"), 0.0, 3.0, 0.01);
	obs_properties_add_float_slider(p, "saturation", obs_module_text("Saturation"), 0.0, 3.0, 0.01);
	obs_properties_add_float_slider(p, "gamma", obs_module_text("Gamma"), 0.1, 3.0, 0.01);
	obs_properties_add_float_slider(p, "temperature", obs_module_text("Temperature"), -1.0, 1.0, 0.01);
	obs_properties_add_float_slider(p, "tint", obs_module_text("Tint"), -1.0, 1.0, 0.01);

	/* --- Key Processing --- */
	obs_properties_add_float_slider(p, "key_sat_boost", obs_module_text("KeySatBoost"), 1.0, 5.0, 0.01);
	obs_properties_add_float_slider(p, "temporal_stability", obs_module_text("TemporalStability"), 0.0, 1.0, 0.01);

	/* --- Opacity --- */
	obs_properties_add_float_slider(p, "opacity", obs_module_text("Opacity"), 0.0, 1.0, 0.01);

	return p;
}

static void smart_chroma_defaults(obs_data_t *s)
{
	obs_data_set_default_int(s, "screen_type", SMART_CHROMA_SCREEN_AUTO);
	obs_data_set_default_int(s, "key_color", 0xFF00FF00);
	obs_data_set_default_double(s, "base_key", 0.035);
	obs_data_set_default_double(s, "edge_softness", 0.1);
	obs_data_set_default_double(s, "background_clean", 0.04);

	obs_data_set_default_double(s, "hair_detail", 0.85);
	obs_data_set_default_double(s, "hair_softness", 0.35);

	obs_data_set_default_double(s, "edge_blur", 0.65);
	obs_data_set_default_double(s, "edge_shrink", 0.0);
	obs_data_set_default_double(s, "edge_grow", 0.0);

	obs_data_set_default_int(s, "hair_color", 0xFF05100D);
	obs_data_set_default_double(s, "spill_reduction", 1.15);
	obs_data_set_default_double(s, "despill_light", 1.0);
	obs_data_set_default_double(s, "despill_dark", 1.0);
	obs_data_set_default_double(s, "color_inject_strength", 0.0);

	obs_data_set_default_double(s, "brightness", 0.0);
	obs_data_set_default_double(s, "contrast", 1.0);
	obs_data_set_default_double(s, "saturation", 1.0);
	obs_data_set_default_double(s, "gamma", 1.0);
	obs_data_set_default_double(s, "temperature", 0.0);
	obs_data_set_default_double(s, "tint", 0.0);

	obs_data_set_default_double(s, "key_sat_boost", 1.25);
	obs_data_set_default_double(s, "temporal_stability", 0.72);

	obs_data_set_default_double(s, "opacity", 1.0);
}

static void smart_chroma_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct smart_chroma_data *f = data;

	if (!f->effect) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	/* Handle auto-tune request (two-phase: stage in frame 1, read in frame 2) */
	if (f->auto_tune_phase == 1) {
		auto_tune_phase1(f);
		/* phase1 sets phase to 2 on success, 0 on failure */
	} else if (f->auto_tune_phase == 2) {
		auto_tune_phase2(f);
	}

	obs_source_t *target = obs_filter_get_target(f->context);
	if (!target) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	/* Temporal stabilization needs the filtered frame in a history texture, so
	 * direct rendering must be disabled for this filter. */
	if (!obs_source_process_filter_begin(f->context, GS_RGBA, OBS_NO_DIRECT_RENDERING))
		return;

	/* Compute texel size */
	uint32_t tw = obs_source_get_base_width(target);
	uint32_t th = obs_source_get_base_height(target);
	if (tw == 0 || th == 0) {
		obs_source_skip_video_filter(f->context);
		f->history_valid = false;
		return;
	}
	if (tw != f->history_width || th != f->history_height) {
		f->history_width = tw;
		f->history_height = th;
		f->history_index = 0;
		f->history_valid = false;
	}

	struct vec2 ts;
	ts.x = 1.0f / (float)tw;
	ts.y = 1.0f / (float)th;

	gs_effect_set_vec2(f->params.texel_size, &ts);
	gs_texture_t *previous_texture = f->history_valid ? gs_texrender_get_texture(f->history[f->history_index])
							  : NULL;
	if (previous_texture)
		gs_effect_set_texture(f->params.previous_image, previous_texture);
	gs_effect_set_float(f->params.history_valid, previous_texture ? 1.0f : 0.0f);
	gs_effect_set_float(f->params.temporal_stability, f->temporal_stability);
	gs_effect_set_int(f->params.screen_type, f->detected_screen_type);
	gs_effect_set_vec4(f->params.key_color, &f->key_color);
	gs_effect_set_float(f->params.base_key, f->base_key);
	gs_effect_set_float(f->params.edge_softness, f->edge_softness);
	gs_effect_set_float(f->params.background_clean, f->background_clean);
	gs_effect_set_float(f->params.hair_detail, f->hair_detail);
	gs_effect_set_float(f->params.hair_softness, f->hair_softness);
	gs_effect_set_float(f->params.edge_blur, f->edge_blur);
	gs_effect_set_float(f->params.edge_shrink, f->edge_shrink);
	gs_effect_set_float(f->params.edge_grow, f->edge_grow);
	gs_effect_set_vec4(f->params.hair_color, &f->hair_color);
	gs_effect_set_float(f->params.spill_reduction, f->spill_reduction);
	gs_effect_set_float(f->params.despill_light, f->despill_light);
	gs_effect_set_float(f->params.despill_dark, f->despill_dark);
	gs_effect_set_float(f->params.color_inject_strength, f->color_inject_strength);
	gs_effect_set_float(f->params.brightness, f->brightness);
	gs_effect_set_float(f->params.contrast, f->contrast);
	gs_effect_set_float(f->params.saturation, f->saturation);
	gs_effect_set_float(f->params.gamma, f->gamma);
	gs_effect_set_float(f->params.temperature, f->temperature);
	gs_effect_set_float(f->params.tint, f->tint);
	gs_effect_set_float(f->params.key_sat_boost, f->key_sat_boost);
	gs_effect_set_float(f->params.opacity, f->opacity);

	uint32_t next_history_index = f->history_index ^ 1U;
	gs_texrender_t *next_history = f->history[next_history_index];
	bool history_rendered = false;

	if (next_history) {
		gs_texrender_reset(next_history);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		if (gs_texrender_begin(next_history, tw, th)) {
			struct vec4 clear_color;
			vec4_zero(&clear_color);
			gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
			gs_ortho(0.0f, (float)tw, 0.0f, (float)th, -100.0f, 100.0f);
			obs_source_process_filter_end(f->context, f->effect, tw, th);
			gs_texrender_end(next_history);
			history_rendered = true;
		}
		gs_blend_state_pop();
	}

	if (!history_rendered) {
		f->history_valid = false;
		obs_source_process_filter_end(f->context, f->effect, 0, 0);
		return;
	}

	gs_texture_t *output_texture = gs_texrender_get_texture(next_history);
	gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	if (!output_texture || !default_effect) {
		f->history_valid = false;
		return;
	}

	gs_eparam_t *image = gs_effect_get_param_by_name(default_effect, "image");
	gs_effect_set_texture(image, output_texture);
	while (gs_effect_loop(default_effect, "Draw"))
		gs_draw_sprite(output_texture, 0, tw, th);

	f->history_index = next_history_index;
	f->history_valid = true;
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
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}
