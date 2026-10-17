#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_WIDTH 16
#define TEST_HEIGHT 16
#define PIXEL_BYTES 4
#ifndef STRAND_COVERAGE
#define STRAND_COVERAGE 0.25f
#endif
#ifndef STRAND_NOISE
#define STRAND_NOISE 6
#endif

static float temporal_strength = 0.72f;
static const uint8_t *raw_history = NULL;

struct test_result {
	float strand_alpha;
	uint8_t noise_alpha;
	uint8_t block_noise_alpha;
	uint8_t dense_hole_alpha;
	uint8_t dense_hole_r;
	uint8_t dense_hole_g;
	uint8_t dense_hole_b;
	uint8_t temporal_alpha;
	uint8_t temporal_trail_alpha;
	uint8_t stale_semantic_alpha;
	uint8_t family_max_alpha;
	uint8_t recolored_alpha;
	uint8_t recolored_r;
	uint8_t recolored_g;
	uint8_t recolored_b;
	uint8_t protected_alpha;
	uint8_t protected_r;
	uint8_t protected_g;
	uint8_t protected_b;
};

static void set_float(gs_effect_t *effect, const char *name, float value)
{
	gs_effect_set_float(gs_effect_get_param_by_name(effect, name), value);
}

static size_t pixel_offset(int x, int y)
{
	return ((size_t)y * TEST_WIDTH + (size_t)x) * PIXEL_BYTES;
}

static void set_pixel(uint8_t *pixels, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	size_t offset = pixel_offset(x, y);
	pixels[offset] = r;
	pixels[offset + 1] = g;
	pixels[offset + 2] = b;
	pixels[offset + 3] = a;
}

static void fill_screen(uint8_t *pixels, bool blue, uint8_t alpha)
{
	for (int y = 0; y < TEST_HEIGHT; y++) {
		for (int x = 0; x < TEST_WIDTH; x++)
			set_pixel(pixels, x, y, 20, blue ? 20 : 180, blue ? 180 : 20, alpha);
	}
}

static void fill_color(uint8_t *pixels, uint8_t r, uint8_t g, uint8_t b)
{
	for (int y = 0; y < TEST_HEIGHT; y++) {
		for (int x = 0; x < TEST_WIDTH; x++)
			set_pixel(pixels, x, y, r, g, b, 255);
	}
}

static bool render_pixels(gs_effect_t *effect, const uint8_t *pixels, const uint8_t *previous,
			  const uint8_t *semantic_pixels, bool blue, bool history_valid, bool semantic_valid,
			  bool face_valid, uint8_t *result)
{
	uint8_t empty_semantic[TEST_WIDTH * TEST_HEIGHT] = {0};
	const uint8_t *pixel_data = pixels;
	const uint8_t *previous_data = previous;
	const uint8_t *raw_data = raw_history ? raw_history : pixels;
	const uint8_t *semantic_data = semantic_pixels ? semantic_pixels : empty_semantic;
	gs_texture_t *input = gs_texture_create(TEST_WIDTH, TEST_HEIGHT, GS_RGBA, 1, &pixel_data, 0);
	gs_texture_t *history = gs_texture_create(TEST_WIDTH, TEST_HEIGHT, GS_RGBA, 1, &previous_data, 0);
	gs_texture_t *raw = gs_texture_create(TEST_WIDTH, TEST_HEIGHT, GS_RGBA, 1, &raw_data, 0);
	gs_texture_t *semantic = gs_texture_create(TEST_WIDTH, TEST_HEIGHT, GS_R8, 1, &semantic_data, 0);
	gs_texrender_t *output = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_stagesurf_t *stage = gs_stagesurface_create(TEST_WIDTH, TEST_HEIGHT, GS_RGBA);
	bool success = input && history && raw && semantic && output && stage;

	if (success) {
		struct vec2 texel;
		texel.x = 1.0f / TEST_WIDTH;
		texel.y = 1.0f / TEST_HEIGHT;
		struct vec4 key;
		struct vec4 face;
		vec4_set(&face, 0.25f, 0.25f, 0.75f, 0.75f);
		key.x = 20.0f / 255.0f;
		key.y = (blue ? 20.0f : 180.0f) / 255.0f;
		key.z = (blue ? 180.0f : 20.0f) / 255.0f;
		key.w = 1.0f;
		gs_effect_set_vec2(gs_effect_get_param_by_name(effect, "texel_size"), &texel);
		gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "KeyColor"), &key);
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), input);
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "previous_image"), history);
		gs_eparam_t *raw_param = gs_effect_get_param_by_name(effect, "previous_input");
		if (raw_param)
			gs_effect_set_texture(raw_param, raw);
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "semantic_mask"), semantic);
		gs_effect_set_vec4(gs_effect_get_param_by_name(effect, "FaceRect"), &face);
		gs_effect_set_int(gs_effect_get_param_by_name(effect, "ScreenType"), blue ? 1 : 0);
		set_float(effect, "HistoryValid", history_valid ? 1.0f : 0.0f);
		set_float(effect, "SemanticMaskValid", semantic_valid ? 1.0f : 0.0f);
		set_float(effect, "FaceValid", face_valid ? 1.0f : 0.0f);
		set_float(effect, "TemporalStability", temporal_strength);
		set_float(effect, "BaseKey", 0.018f);
		set_float(effect, "EdgeSoftness", 0.075f);
		set_float(effect, "BackgroundClean", 0.04f);
		set_float(effect, "HairDetail", 0.85f);
		set_float(effect, "HairSoftness", 0.35f);
		set_float(effect, "EdgeBlur", 0.65f);
		set_float(effect, "EdgeShrink", 0.0f);
		set_float(effect, "EdgeGrow", 0.0f);
		set_float(effect, "SpillReduction", 1.15f);
		set_float(effect, "DespillLight", 1.0f);
		set_float(effect, "DespillDark", 0.9f);
		set_float(effect, "ColorInjectStrength", 0.0f);
		set_float(effect, "Brightness", 0.0f);
		set_float(effect, "Contrast", 1.0f);
		set_float(effect, "Saturation", 1.0f);
		set_float(effect, "Gamma", 1.0f);
		set_float(effect, "Temperature", 0.0f);
		set_float(effect, "Tint", 0.0f);
		set_float(effect, "KeySatBoost", 1.25f);
		set_float(effect, "Opacity", 1.0f);

		gs_texrender_reset(output);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		if (!gs_texrender_begin(output, TEST_WIDTH, TEST_HEIGHT)) {
			success = false;
		} else {
			struct vec4 clear = {0};
			gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
			gs_ortho(0.0f, TEST_WIDTH, 0.0f, TEST_HEIGHT, -100.0f, 100.0f);
			gs_set_cull_mode(GS_NEITHER);
			gs_enable_depth_test(false);
			while (gs_effect_loop(effect, "Draw"))
				gs_draw_sprite(input, 0, TEST_WIDTH, TEST_HEIGHT);
			gs_texrender_end(output);
		}
		gs_blend_state_pop();
	}

	if (success) {
		gs_stage_texture(stage, gs_texrender_get_texture(output));
		gs_flush();
		uint8_t *mapped = NULL;
		uint32_t linesize = 0;
		if (!gs_stagesurface_map(stage, &mapped, &linesize)) {
			success = false;
		} else {
			for (int y = 0; y < TEST_HEIGHT; y++)
				memcpy(result + (size_t)y * TEST_WIDTH * PIXEL_BYTES, mapped + (size_t)y * linesize,
				       TEST_WIDTH * PIXEL_BYTES);
			gs_stagesurface_unmap(stage);
		}
	}

	if (stage)
		gs_stagesurface_destroy(stage);
	if (output)
		gs_texrender_destroy(output);
	if (history)
		gs_texture_destroy(history);
	if (raw)
		gs_texture_destroy(raw);
	if (semantic)
		gs_texture_destroy(semantic);
	if (input)
		gs_texture_destroy(input);
	return success;
}

static bool run_tests(gs_effect_t *effect, bool blue, struct test_result *test_result)
{
	uint8_t pixels[TEST_WIDTH * TEST_HEIGHT * PIXEL_BYTES];
	uint8_t previous[TEST_WIDTH * TEST_HEIGHT * PIXEL_BYTES];
	uint8_t result[TEST_WIDTH * TEST_HEIGHT * PIXEL_BYTES];
	uint8_t semantic[TEST_WIDTH * TEST_HEIGHT] = {0};

	fill_screen(pixels, blue, 255);
	fill_screen(previous, blue, 0);
	for (int y = 3; y <= 12; y++)
		set_pixel(pixels, 8, y, 15, blue ? 15 : 135, blue ? 135 : 15, 255);
	set_pixel(pixels, 13, 13, 15, blue ? 15 : 135, blue ? 135 : 15, 255);
	if (!render_pixels(effect, pixels, previous, NULL, blue, false, false, false, result))
		return false;

	float strand_sum = 0.0f;
	for (int y = 5; y <= 10; y++)
		strand_sum += result[pixel_offset(8, y) + 3];
	test_result->strand_alpha = strand_sum / 6.0f;
	test_result->noise_alpha = result[pixel_offset(13, 13) + 3];

	fill_screen(pixels, blue, 255);
	for (int y = 6; y <= 8; y++)
		for (int x = 6; x <= 8; x++)
			set_pixel(pixels, x, y, 16, blue ? 16 : 144, blue ? 144 : 16, 255);
	if (!render_pixels(effect, pixels, previous, NULL, blue, false, false, false, result))
		return false;
	for (int y = 5; y <= 9; y++)
		for (int x = 5; x <= 9; x++)
			if (result[pixel_offset(x, y) + 3] > test_result->block_noise_alpha)
				test_result->block_noise_alpha = result[pixel_offset(x, y) + 3];
	printf("%s block-noise=%u\n", blue ? "blue" : "green", test_result->block_noise_alpha);

	fill_screen(pixels, blue, 255);
	memset(semantic, 0, sizeof(semantic));
	for (int y = 4; y <= 11; y++) {
		for (int x = 4; x <= 11; x++) {
			set_pixel(pixels, x, y, 80, 40, 20, 255);
			semantic[(size_t)y * TEST_WIDTH + x] = 255;
		}
	}
	set_pixel(pixels, 8, 8, 41, blue ? 27 : 131, blue ? 124 : 20, 255);
	if (!render_pixels(effect, pixels, previous, semantic, blue, false, true, true, result))
		return false;
	test_result->dense_hole_r = result[pixel_offset(8, 8)];
	test_result->dense_hole_g = result[pixel_offset(8, 8) + 1];
	test_result->dense_hole_b = result[pixel_offset(8, 8) + 2];
	test_result->dense_hole_alpha = result[pixel_offset(8, 8) + 3];

	fill_screen(pixels, blue, 255);
	fill_screen(previous, blue, 0);
	for (int y = 3; y <= 12; y++) {
		set_pixel(pixels, 8, y, 15, blue ? 15 : 135, blue ? 135 : 15, 255);
		set_pixel(previous, 8, y, 55, 35, 20, 82);
	}
	if (!render_pixels(effect, pixels, previous, NULL, blue, true, false, false, result))
		return false;
	test_result->temporal_alpha = result[pixel_offset(8, 8) + 3];

	/* An opaque subject pixel from history must not survive where the current
	 * frame is plain screen. This is the visible slow-moving trail regression. */
	fill_screen(pixels, blue, 255);
	fill_screen(previous, blue, 0);
	set_pixel(previous, 8, 8, 80, 40, 20, 255);
	if (!render_pixels(effect, pixels, previous, NULL, blue, true, false, false, result))
		return false;
	test_result->temporal_trail_alpha = result[pixel_offset(8, 8) + 3];

	/* Vision is asynchronous. A stale person mask over the sampled screen must
	 * not create a detached silhouette after the real subject has moved away. */
	fill_screen(pixels, blue, 255);
	memset(semantic, 0, sizeof(semantic));
	for (int y = 4; y <= 11; y++) {
		for (int x = 4; x <= 11; x++)
			semantic[(size_t)y * TEST_WIDTH + x] = 255;
	}
	if (!render_pixels(effect, pixels, previous, semantic, blue, false, true, true, result))
		return false;
	test_result->stale_semantic_alpha = result[pixel_offset(8, 8) + 3];

	const uint8_t green_shades[][3] = {
		{30, 90, 40}, {110, 220, 130}, {75, 95, 20}, {15, 160, 120}, {185, 225, 195},
	};
	const uint8_t blue_shades[][3] = {
		{30, 40, 90}, {110, 130, 220}, {75, 20, 95}, {120, 40, 160}, {195, 205, 225},
	};
	const uint8_t(*screen_shades)[3] = blue ? blue_shades : green_shades;
	memset(semantic, 0, sizeof(semantic));
	test_result->family_max_alpha = 0;
	for (size_t shade = 0; shade < sizeof(green_shades) / sizeof(green_shades[0]); shade++) {
		fill_color(pixels, screen_shades[shade][0], screen_shades[shade][1], screen_shades[shade][2]);
		if (!render_pixels(effect, pixels, previous, semantic, blue, false, true, false, result))
			return false;
		uint8_t shade_alpha = result[pixel_offset(8, 8) + 3];
		if (shade_alpha > test_result->family_max_alpha)
			test_result->family_max_alpha = shade_alpha;
	}

	fill_screen(pixels, blue, 255);
	memset(semantic, 0, sizeof(semantic));
	for (int y = 4; y <= 11; y++) {
		for (int x = 4; x <= 11; x++) {
			set_pixel(pixels, x, y, 80, 35, 15, 255);
			semantic[(size_t)y * TEST_WIDTH + x] = 255;
		}
	}
	set_pixel(pixels, 8, 8, 80, blue ? 35 : 70, blue ? 70 : 15, 255);
	if (!render_pixels(effect, pixels, previous, semantic, blue, false, true, true, result))
		return false;
	test_result->recolored_r = result[pixel_offset(8, 8)];
	test_result->recolored_g = result[pixel_offset(8, 8) + 1];
	test_result->recolored_b = result[pixel_offset(8, 8) + 2];
	test_result->recolored_alpha = result[pixel_offset(8, 8) + 3];

	/* A deliberately screen-colored garment inside the person mask must remain
	 * opaque and retain its intentional color. It is outside the face/hair box. */
	fill_screen(pixels, blue, 255);
	memset(semantic, 0, sizeof(semantic));
	for (int y = 7; y <= 14; y++) {
		for (int x = 3; x <= 12; x++) {
			set_pixel(pixels, x, y, blue ? 45 : 35, blue ? 65 : 155, blue ? 155 : 55, 255);
			semantic[(size_t)y * TEST_WIDTH + x] = 255;
		}
	}
	if (!render_pixels(effect, pixels, previous, semantic, blue, false, true, true, result))
		return false;
	test_result->protected_r = result[pixel_offset(8, 12)];
	test_result->protected_g = result[pixel_offset(8, 12) + 1];
	test_result->protected_b = result[pixel_offset(8, 12) + 2];
	test_result->protected_alpha = result[pixel_offset(8, 12) + 3];

	return test_result->strand_alpha >= 45.0f && test_result->noise_alpha <= 12 &&
	       test_result->block_noise_alpha <= 12 && test_result->dense_hole_alpha >= 150 &&
	       test_result->temporal_alpha >= 36 && test_result->temporal_trail_alpha <= 12 &&
	       test_result->stale_semantic_alpha <= 12 && test_result->dense_hole_r > test_result->dense_hole_g &&
	       test_result->dense_hole_g > test_result->dense_hole_b && test_result->family_max_alpha <= 12 &&
	       test_result->recolored_alpha >= 220 && test_result->recolored_r > test_result->recolored_g &&
	       test_result->recolored_g > test_result->recolored_b && test_result->protected_alpha >= 245 &&
	       abs((int)test_result->protected_r - (blue ? 45 : 35)) <= 3 &&
	       abs((int)test_result->protected_g - (blue ? 65 : 155)) <= 3 &&
	       abs((int)test_result->protected_b - (blue ? 155 : 55)) <= 3;
}

/* Known foreground/screen mixtures with two-pixel correlated sensor noise.
 * Compare whole sequences, not a hand-picked previous alpha. */
static bool sequence_tests(gs_effect_t *effect, bool blue)
{
	uint8_t pixels[TEST_WIDTH * TEST_HEIGHT * PIXEL_BYTES];
	uint8_t previous_raw[sizeof(pixels)];
	uint8_t history[sizeof(pixels)];
	uint8_t result[sizeof(pixels)];
	const uint8_t colors[][3] = {{70, 36, 18}, {195, 170, 110}, {8, 7, 6}, {175, 105, 80}};
	bool ok = true;
	for (size_t kind = 0; kind < sizeof(colors) / sizeof(colors[0]); kind++) {
		double flicker[2] = {0}, alpha_flicker[2] = {0}, error[2] = {0};
		for (int stabilized = 0; stabilized < 2; stabilized++) {
			memset(history, 0, sizeof(history));
			fill_screen(previous_raw, blue, 255);
			temporal_strength = stabilized ? 0.85f : 0.0f;
			for (int frame = 0; frame < 32; frame++) {
				fill_screen(pixels, blue, 255);
				for (int y = 0; y < TEST_HEIGHT; y++) {
					for (int x = 0; x <= 7; x++) {
						float coverage = x < 7 ? 1.0f : 0.4f;
						int noise = x == 7 ? (((frame + y / 2) % 2) ? 4 : -4) : 0;
						uint8_t rgb[3];
						for (int c = 0; c < 3; c++) {
							int screen = c == (blue ? 2 : 1) ? 180 : 20;
							int value = (int)(colors[kind][c] * coverage +
									  screen * (1.0f - coverage)) +
								    noise;
							rgb[c] = (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
						}
						set_pixel(pixels, x, y, rgb[0], rgb[1], rgb[2], 255);
					}
				}
				raw_history = previous_raw;
				if (!render_pixels(effect, pixels, history, NULL, blue, frame > 0, false, false,
						   result))
					return false;
				if (frame >= 6) {
					for (int y = 4; y < 12; y++) {
						size_t i = pixel_offset(7, y);
						float a = result[i + 3] / 255.0f;
						float pa = history[i + 3] / 255.0f;
						alpha_flicker[stabilized] += abs((int)result[i + 3] - history[i + 3]);
						for (int c = 0; c < 3; c++) {
							/* Both white and black composites expose colored
                             * fringes, dark rims and coverage instability. */
							for (int background = 0; background <= 255; background += 255) {
								float composite =
									result[i + c] * a + background * (1.0f - a);
								float prev =
									history[i + c] * pa + background * (1.0f - pa);
								float truth =
									colors[kind][c] * 0.4f + background * 0.6f;
								flicker[stabilized] += fabsf(composite - prev);
								error[stabilized] += fabsf(composite - truth);
							}
						}
					}
				}
				memcpy(previous_raw, pixels, sizeof(pixels));
				memcpy(history, result, sizeof(result));
			}
		}
		double samples = 26.0 * 8.0 * 6.0;
		if (getenv("SCK_TEST_DEBUG")) {
			size_t i = pixel_offset(7, 8);
			printf("edge rgba=%u,%u,%u,%u\n", result[i], result[i + 1], result[i + 2], result[i + 3]);
		}
		printf("%s color=%zu composite-flicker=%.3f->%.3f alpha-flicker=%.3f->%.3f composite-error=%.3f->%.3f\n",
		       blue ? "blue" : "green", kind, flicker[0] / samples, flicker[1] / samples,
		       alpha_flicker[0] / 208.0, alpha_flicker[1] / 208.0, error[0] / samples, error[1] / samples);
		ok &= flicker[1] < flicker[0] * 0.75 && error[1] / samples < 3.0;
	}
	/* Actual input changes must bypass history, including a cut to a different
     * opaque subject at the same coordinates. */
	raw_history = previous_raw;
	temporal_strength = 0.85f;
	fill_screen(pixels, blue, 255);
	if (!render_pixels(effect, pixels, history, NULL, blue, true, false, false, result))
		return false;
	uint8_t trail = 0;
	for (size_t i = 3; i < sizeof(result); i += 4)
		if (result[i] > trail)
			trail = result[i];
	ok &= trail <= 12;
	fill_color(pixels, 160, 55, 35);
	uint8_t without_history[sizeof(result)];
	if (!render_pixels(effect, pixels, history, NULL, blue, true, false, false, result) ||
	    !render_pixels(effect, pixels, history, NULL, blue, false, false, false, without_history))
		return false;
	ok &= memcmp(result, without_history, sizeof(result)) == 0;
	printf("%s moving-edge trail=%u scene-cut=%s\n", blue ? "blue" : "green", trail,
	       memcmp(result, without_history, sizeof(result)) == 0 ? "clean" : "FAILED");
	temporal_strength = 0.72f;
	raw_history = NULL;
	return ok;
}

/* A thin, half-covered strand must keep fractional alpha with the hue test
 * active. Classifying each pixel as 0 or 1 under sensor
 * noise produced a checkerboard along hair boundaries. */
static bool mixed_edge_tests(gs_effect_t *effect, bool blue)
{
	uint8_t pixels[TEST_WIDTH * TEST_HEIGHT * PIXEL_BYTES];
	uint8_t previous[TEST_WIDTH * TEST_HEIGHT * PIXEL_BYTES];
	uint8_t result[TEST_WIDTH * TEST_HEIGHT * PIXEL_BYTES];
	uint8_t semantic[TEST_WIDTH * TEST_HEIGHT] = {0};
	bool ok = true;

	fill_screen(previous, blue, 0);
	{
		const int ai = 0;
		fill_screen(pixels, blue, 255);
		for (int y = 0; y < TEST_HEIGHT; y++) {
			/* 50% mixture of brown hair and the screen, with deterministic
			 * sensor-like noise. */
			int noise = (y * 37) % 9 - 4;
			for (int x = 7; x <= 8; x++)
				set_pixel(pixels, x, y, 50 + noise, (blue ? 30 : 110) - noise,
					  (blue ? 100 : 20) + noise, 255);
		}
		if (!render_pixels(effect, pixels, previous, semantic, blue, false, ai, false, result))
			return false;
		float sum = 0.0f;
		float variation = 0.0f;
		for (int y = 4; y <= 11; y++) {
			sum += result[pixel_offset(8, y) + 3];
			if (y < 11)
				variation += fabsf((float)result[pixel_offset(8, y) + 3] -
						   (float)result[pixel_offset(8, y + 1) + 3]);
		}
		float mean = sum / 8.0f;
		variation /= 7.0f;
		printf("%s mixed-strand ai=%d alpha=%.1f row-variation=%.1f\n", blue ? "blue" : "green", ai, mean,
		       variation);
		ok &= mean > 70.0f && mean < 200.0f && variation < 30.0f;
	}
	return ok;
}

/* A detached, one-pixel diagonal strand over the screen. Dark strands keep
 * the screen hue, so hue and whole-family tests classify them as background;
 * colour-difference coverage must keep them (Ultra Key behaviour). */
static bool flyaway_strand_tests(gs_effect_t *effect, bool blue)
{
	uint8_t pixels[TEST_WIDTH * TEST_HEIGHT * PIXEL_BYTES];
	uint8_t previous[TEST_WIDTH * TEST_HEIGHT * PIXEL_BYTES];
	uint8_t result[TEST_WIDTH * TEST_HEIGHT * PIXEL_BYTES];
	uint8_t semantic[TEST_WIDTH * TEST_HEIGHT] = {0};
	const float strand_coverage = 0.4f;
	const uint8_t hair[3] = {40, 25, 15};
	bool ok = true;

	fill_screen(previous, blue, 0);
	for (int ai = 0; ai <= 1; ai++) {
		fill_screen(pixels, blue, 255);
		for (int i = 2; i <= 13; i++) {
			uint8_t rgb[3];
			for (int c = 0; c < 3; c++) {
				int screen = c == (blue ? 2 : 1) ? 180 : 20;
				rgb[c] = (uint8_t)lroundf(hair[c] * strand_coverage + screen * (1.0f - strand_coverage));
			}
			set_pixel(pixels, i, i, rgb[0], rgb[1], rgb[2], 255);
		}
		if (!render_pixels(effect, pixels, previous, semantic, blue, false, ai, false, result))
			return false;
		float sum = 0.0f;
		uint8_t background = 0;
		for (int i = 4; i <= 11; i++)
			sum += result[pixel_offset(i, i) + 3];
		for (int i = 4; i <= 11; i++)
			if (result[pixel_offset(i, 15 - i) + 3] > background && abs(i - (15 - i)) > 2)
				background = result[pixel_offset(i, 15 - i) + 3];
		float mean = sum / 8.0f;
		printf("%s flyaway-strand ai=%d alpha=%.1f (truth %.0f) background=%u\n", blue ? "blue" : "green", ai,
		       mean, strand_coverage * 255.0f, background);
		ok &= mean > 70.0f && mean < 135.0f && background <= 12;
	}
	return ok;
}

/* Replace chroma with its 2x2 average, as 4:2:0 camera video does. Thin
 * strands then survive only in luma. */
static void subsample_chroma(uint8_t *pixels)
{
	for (int y = 0; y < TEST_HEIGHT; y += 2) {
		for (int x = 0; x < TEST_WIDTH; x += 2) {
			float luma[4], mean[3] = {0};
			for (int i = 0; i < 4; i++) {
				size_t o = pixel_offset(x + (i & 1), y + (i >> 1));
				luma[i] = 0.2126f * pixels[o] + 0.7152f * pixels[o + 1] + 0.0722f * pixels[o + 2];
				for (int c = 0; c < 3; c++)
					mean[c] += pixels[o + c] * 0.25f;
			}
			float mean_luma = 0.2126f * mean[0] + 0.7152f * mean[1] + 0.0722f * mean[2];
			for (int i = 0; i < 4; i++) {
				size_t o = pixel_offset(x + (i & 1), y + (i >> 1));
				for (int c = 0; c < 3; c++) {
					int value = (int)lroundf(mean[c] + luma[i] - mean_luma);
					pixels[o + c] = (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
				}
			}
		}
	}
}

/* A faint flyaway strand in live 4:2:0 video: per-frame sensor noise must not
 * make its pixels switch on and off (twinkling "black snow") once temporal
 * stabilization has history. Green screen only: dark hair on blue has too
 * little luma contrast for luma-guided recovery. */
static bool live_strand_flicker_tests(gs_effect_t *effect, bool blue)
{
	uint8_t pixels[TEST_WIDTH * TEST_HEIGHT * PIXEL_BYTES];
	uint8_t history[sizeof(pixels)], previous_raw[sizeof(pixels)], result[sizeof(pixels)];
	uint8_t semantic[TEST_WIDTH * TEST_HEIGHT] = {0};
	const float strand_coverage = STRAND_COVERAGE;
	const uint8_t hair[3] = {30, 22, 14};
	bool ok = true;
	for (int ai = 0; ai <= 1; ai++) {
		memset(history, 0, sizeof(history));
		fill_screen(previous_raw, blue, 255);
		uint32_t rng = 7;
		double flicker = 0.0;
		int toggles = 0;
		int samples = 0;
		for (int frame = 0; frame < 24; frame++) {
			for (int y = 0; y < TEST_HEIGHT; y++) {
				for (int x = 0; x < TEST_WIDTH; x++) {
					/* Shallow anti-aliased strand: y = 3 + x / 2, so its
					 * coverage alternates between one and two pixels. */
					float distance = fabsf((float)y - (3.0f + x * 0.5f));
					float coverage = (x >= 1 && x <= 14 && distance < 1.0f)
								 ? strand_coverage * (1.0f - distance)
								 : 0.0f;
					uint8_t rgb[3];
					for (int c = 0; c < 3; c++) {
						rng = rng * 1664525u + 1013904223u;
						int noise = (int)((rng >> 16) % (2 * STRAND_NOISE + 1)) - STRAND_NOISE;
						int screen = c == (blue ? 2 : 1) ? 180 : 20;
						int value = (int)lroundf(hair[c] * coverage + screen * (1.0f - coverage)) + noise;
						rgb[c] = (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
					}
					set_pixel(pixels, x, y, rgb[0], rgb[1], rgb[2], 255);
				}
			}
			subsample_chroma(pixels);
			raw_history = previous_raw;
			if (!render_pixels(effect, pixels, history, semantic, blue, frame > 0, ai, false, result))
				return false;
			if (frame >= 6) {
				for (int i = 4; i <= 11; i++) {
					size_t offset = pixel_offset(i, 3 + i / 2) + 3;
					float change = fabsf((float)result[offset] - (float)history[offset]);
					flicker += change;
					toggles += change > 30.0f;
					samples++;
				}
			}
			memcpy(history, result, sizeof(result));
			memcpy(previous_raw, pixels, sizeof(pixels));
		}
		raw_history = NULL;
		float mean_flicker = (float)(flicker / samples);
		printf("%s live-strand ai=%d flicker=%.2f toggles=%d\n", blue ? "blue" : "green", ai, mean_flicker,
		       toggles);
		ok &= mean_flicker < 3.2f && toggles <= 2;
	}
	return ok;
}

/* With AI protection, the person mask covers a half-transparent hair fringe
 * whose colour keeps the screen hue. Sensor noise must not switch the mask
 * on and off per pixel and per frame (live "checkerboard" sparkle). */
static bool ai_fringe_flicker_tests(gs_effect_t *effect, bool blue)
{
	uint8_t pixels[TEST_WIDTH * TEST_HEIGHT * PIXEL_BYTES];
	uint8_t history[sizeof(pixels)], previous_raw[sizeof(pixels)], result[sizeof(pixels)];
	uint8_t semantic[TEST_WIDTH * TEST_HEIGHT];
	const uint8_t hair[3] = {25, 20, 15};
	memset(history, 0, sizeof(history));
	fill_screen(previous_raw, blue, 255);
	for (int y = 0; y < TEST_HEIGHT; y++)
		for (int x = 0; x < TEST_WIDTH; x++)
			semantic[y * TEST_WIDTH + x] = x <= 10 ? 255 : 0;
	uint32_t rng = 99;
	double flicker = 0.0;
	int toggles = 0, samples = 0;
	int column_toggles[4] = {0};
	for (int frame = 0; frame < 24; frame++) {
		for (int y = 0; y < TEST_HEIGHT; y++) {
			for (int x = 0; x < TEST_WIDTH; x++) {
				float coverage = x <= 5 ? 1.0f : x >= 11 ? 0.0f : (11 - x) / 6.0f;
				uint8_t rgb[3];
				for (int c = 0; c < 3; c++) {
					rng = rng * 1664525u + 1013904223u;
					int noise = (int)((rng >> 16) % 9) - 4;
					int screen = c == (blue ? 2 : 1) ? 180 : 20;
					int value = (int)lroundf(hair[c] * coverage + screen * (1.0f - coverage)) + noise;
					rgb[c] = (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
				}
				set_pixel(pixels, x, y, rgb[0], rgb[1], rgb[2], 255);
			}
		}
		subsample_chroma(pixels);
		raw_history = previous_raw;
		if (!render_pixels(effect, pixels, history, semantic, blue, frame > 0, true, true, result))
			return false;
		if (frame >= 6) {
			for (int y = 3; y <= 12; y++) {
				for (int x = 7; x <= 10; x++) {
					size_t offset = pixel_offset(x, y) + 3;
					float change = fabsf((float)result[offset] - (float)history[offset]);
					flicker += change;
					toggles += change > 40.0f;
					column_toggles[x - 7] += change > 40.0f;
					samples++;
				}
			}
		}
		memcpy(history, result, sizeof(result));
		memcpy(previous_raw, pixels, sizeof(pixels));
	}
	raw_history = NULL;
	if (getenv("SCK_DEBUG_FRINGE"))
		printf("ai-fringe columns 7..10 toggles=%d,%d,%d,%d alpha=%u,%u,%u,%u\n", column_toggles[0],
		       column_toggles[1], column_toggles[2], column_toggles[3], result[pixel_offset(7, 8) + 3],
		       result[pixel_offset(8, 8) + 3], result[pixel_offset(9, 8) + 3], result[pixel_offset(10, 8) + 3]);
	float mean_flicker = (float)(flicker / samples);
	printf("%s ai-fringe flicker=%.2f toggles=%d\n", blue ? "blue" : "green", mean_flicker, toggles);
	return mean_flicker < 9.0f && toggles <= 2;
}

/* Antialiased body silhouettes, outside the face/hair box, on white and
 * black backdrops. Broad edges model camera softness/motion blur. */
static bool body_edge_tests(gs_effect_t *effect, bool blue)
{
	uint8_t pixels[TEST_WIDTH * TEST_HEIGHT * 4];
	uint8_t history[sizeof(pixels)] = {0};
	uint8_t result[sizeof(pixels)];
	uint8_t semantic[TEST_WIDTH * TEST_HEIGHT];
	const uint8_t foreground[][3] = {{160, 160, 160}, {190, 125, 95}, {235, 230, 220}, {30, 30, 30}};
	bool ok = true;
	for (int ai = 0; ai <= 1; ai++) {
		for (int width = 1; width <= 5; width += 2) {
			double error = 0.0, dark_rim = 0.0;
			int count = 0;
			for (size_t kind = 0; kind < 4; kind++) {
				for (int y = 0; y < TEST_HEIGHT; y++) {
					for (int x = 0; x < TEST_WIDTH; x++) {
						float coverage = fminf(fmaxf((8.0f - x) / width + 0.5f, 0.0f), 1.0f);
						uint8_t rgb[3];
						for (int c = 0; c < 3; c++) {
							float bg = c == (blue ? 2 : 1) ? 180.0f : 20.0f;
							rgb[c] = (uint8_t)lroundf(foreground[kind][c] * coverage +
										  bg * (1.0f - coverage));
						}
						set_pixel(pixels, x, y, rgb[0], rgb[1], rgb[2], 255);
						// Coarse semantic edges can be more opaque than the
						// camera's actual antialiased boundary.
						semantic[y * TEST_WIDTH + x] =
							(uint8_t)lroundf(fminf(coverage * 1.35f, 1.0f) * 255.0f);
					}
				}
				if (!render_pixels(effect, pixels, history, semantic, blue, false, ai, ai, result))
					return false;
				for (int x = 6; x <= 10; x++) {
					float coverage = fminf(fmaxf((8.0f - x) / width + 0.5f, 0.0f), 1.0f);
					if (coverage <= 0 || coverage >= 1)
						continue;
					size_t i = pixel_offset(x, 12);
					float alpha = result[i + 3] / 255.0f;
					for (int c = 0; c < 3; c++)
						for (int bg = 0; bg <= 255; bg += 255) {
							float actual = result[i + c] * alpha + bg * (1.0f - alpha);
							float expected =
								foreground[kind][c] * coverage + bg * (1.0f - coverage);
							error += fabsf(actual - expected);
							if (bg == 255)
								dark_rim = fmaxf(dark_rim, expected - actual);
							count++;
						}
					if (getenv("SCK_TEST_DEBUG"))
						printf("body ai=%d width=%d color=%zu x=%d rgba=%u,%u,%u,%u\n", ai,
						       width, kind, x, result[i], result[i + 1], result[i + 2],
						       result[i + 3]);
				}
			}
			printf("%s body ai=%d width=%d mean-error=%.2f worst-dark-rim=%.2f\n", blue ? "blue" : "green",
			       ai, width, error / count, dark_rim);
			ok &= error / count < 2.0 && dark_rim < 10.0;
		}
	}
	// A real opaque dark seam on a light garment must remain dark: correcting
	// a black fringe must not bleach intentional black subject detail.
	fill_screen(pixels, blue, 255);
	for (int y = 0; y < TEST_HEIGHT; y++) {
		for (int x = 0; x < 8; x++)
			set_pixel(pixels, x, y, 160, 160, 160, 255);
		set_pixel(pixels, 7, y, 30, 30, 30, 255);
	}
	if (!render_pixels(effect, pixels, history, NULL, blue, false, false, false, result))
		return false;
	size_t seam = pixel_offset(7, 12);
	bool seam_ok = result[seam + 3] >= 250;
	for (int c = 0; c < 3; c++)
		seam_ok &= abs((int)result[seam + c] - 30) <= 3;
	printf("%s opaque-dark-seam=%u,%u,%u,%u\n", blue ? "blue" : "green", result[seam], result[seam + 1],
	       result[seam + 2], result[seam + 3]);
	return ok && seam_ok;
}

/* Broad hair fringes with independent RGB noise and a coarse person mask.
 * The old body-only color fit skipped exactly this face-guided region. */
static bool noisy_hair_tests(gs_effect_t *effect, bool blue)
{
	uint8_t pixels[TEST_WIDTH * TEST_HEIGHT * PIXEL_BYTES];
	uint8_t history[sizeof(pixels)], previous_raw[sizeof(pixels)], result[sizeof(pixels)];
	uint8_t semantic[TEST_WIDTH * TEST_HEIGHT];
	const uint8_t colors[][3] = {{70, 36, 18}, {195, 170, 110}, {8, 7, 6}};
	bool ok = true;
	for (int ai = 0; ai < 2; ai++) {
		for (size_t kind = 0; kind < sizeof(colors) / sizeof(colors[0]); kind++) {
			memset(history, 0, sizeof(history));
			fill_screen(previous_raw, blue, 255);
			uint32_t rng = 41;
			double error = 0, alpha_error = 0, flicker = 0;
			int samples = 0;
			for (int frame = 0; frame < 32; frame++) {
				for (int y = 0; y < TEST_HEIGHT; y++) {
					for (int x = 0; x < TEST_WIDTH; x++) {
						float coverage = x <= 4 ? 1.0f : x >= 10 ? 0.0f : (10 - x) / 6.0f;
						uint8_t rgb[3];
						for (int c = 0; c < 3; c++) {
							rng = rng * 1664525u + 1013904223u;
							int noise = (int)((rng >> 16) % 9) - 4;
							int screen = c == (blue ? 2 : 1) ? 180 : 20;
							int value = (int)lroundf(colors[kind][c] * coverage + screen * (1 - coverage)) + noise;
							rgb[c] = (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
						}
						set_pixel(pixels, x, y, rgb[0], rgb[1], rgb[2], 255);
						semantic[y * TEST_WIDTH + x] = x <= 8 ? 255 : x == 9 ? 128 : 0;
					}
				}
				raw_history = previous_raw;
				if (!render_pixels(effect, pixels, history, semantic, blue, frame > 0, ai, ai, result))
					return false;
				if (frame >= 6) {
					for (int y = 2; y <= 5; y++) {
						for (int x = 5; x <= 9; x++) {
							size_t i = pixel_offset(x, y);
							float coverage = (10 - x) / 6.0f;
							float a = result[i + 3] / 255.0f, pa = history[i + 3] / 255.0f;
							alpha_error += fabsf(a - coverage);
							for (int c = 0; c < 3; c++) {
								for (int bg = 0; bg <= 255; bg += 255) {
									float composite = result[i + c] * a + bg * (1 - a);
									float truth = colors[kind][c] * coverage + bg * (1 - coverage);
									error += fabsf(composite - truth);
									flicker += fabsf(composite - (history[i + c] * pa + bg * (1 - pa)));
								}
							}
							samples++;
						}
					}
				}
				memcpy(history, result, sizeof(result));
				memcpy(previous_raw, pixels, sizeof(pixels));
			}
			printf("%s noisy-hair ai=%d color=%zu error=%.3f alpha-error=%.3f flicker=%.3f\n",
			       blue ? "blue" : "green", ai, kind, error / (samples * 6),
			       alpha_error / samples, flicker / (samples * 6));
			const char *dump_dir = getenv("SCK_TEST_DUMP_DIR");
			if (dump_dir) {
				char path[1024];
				snprintf(path, sizeof(path), "%s/%s-ai%d-color%zu.rgba", dump_dir,
					 blue ? "blue" : "green", ai, kind);
				FILE *dump = fopen(path, "wb");
				if (!dump)
					return false;
				bool written = fwrite(result, 1, sizeof(result), dump) == sizeof(result);
				ok &= fclose(dump) == 0 && written;
			}
			ok &= error / (samples * 6) < 5.0 && alpha_error / samples < 0.04 && flicker / (samples * 6) < 2.0;
		}
	}
	raw_history = NULL;
	return ok;
}

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s <libobs-opengl.dylib> <effect-file>\n", argv[0]);
		return 2;
	}

	graphics_t *graphics = NULL;
	if (gs_create(&graphics, argv[1], 0) != GS_SUCCESS)
		return 3;
	gs_enter_context(graphics);

	char *errors = NULL;
	gs_effect_t *effect = gs_effect_create_from_file(argv[2], &errors);
	if (errors && *errors)
		fprintf(stderr, "%s\n", errors);
	bfree(errors);
	if (!effect) {
		gs_leave_context();
		gs_destroy(graphics);
		return 4;
	}

	struct test_result green = {0};
	struct test_result blue = {0};
	bool green_ok = run_tests(effect, false, &green);
	bool blue_ok = run_tests(effect, true, &blue);
	printf("green strand=%.1f noise=%u dense-hole=%u,%u,%u,%u temporal=%u trail=%u stale-ai=%u family=%u "
	       "recolored=%u,%u,%u,%u protected=%u,%u,%u,%u\n",
	       green.strand_alpha, green.noise_alpha, green.dense_hole_r, green.dense_hole_g, green.dense_hole_b,
	       green.dense_hole_alpha, green.temporal_alpha, green.temporal_trail_alpha, green.stale_semantic_alpha,
	       green.family_max_alpha, green.recolored_r, green.recolored_g, green.recolored_b, green.recolored_alpha,
	       green.protected_r, green.protected_g, green.protected_b, green.protected_alpha);
	printf("blue  strand=%.1f noise=%u dense-hole=%u,%u,%u,%u temporal=%u trail=%u stale-ai=%u family=%u "
	       "recolored=%u,%u,%u,%u protected=%u,%u,%u,%u\n",
	       blue.strand_alpha, blue.noise_alpha, blue.dense_hole_r, blue.dense_hole_g, blue.dense_hole_b,
	       blue.dense_hole_alpha, blue.temporal_alpha, blue.temporal_trail_alpha, blue.stale_semantic_alpha,
	       blue.family_max_alpha, blue.recolored_r, blue.recolored_g, blue.recolored_b, blue.recolored_alpha,
	       blue.protected_r, blue.protected_g, blue.protected_b, blue.protected_alpha);

	bool green_sequence_ok = sequence_tests(effect, false);
	bool blue_sequence_ok = sequence_tests(effect, true);
	bool sequences_ok = green_sequence_ok && blue_sequence_ok;
	bool body_green_ok = body_edge_tests(effect, false);
	bool body_blue_ok = body_edge_tests(effect, true);
	bool mixed_ok = mixed_edge_tests(effect, false) && mixed_edge_tests(effect, true);
	bool flyaway_ok = flyaway_strand_tests(effect, false) && flyaway_strand_tests(effect, true);
	bool live_ok = live_strand_flicker_tests(effect, false) && ai_fringe_flicker_tests(effect, false) &&
		       ai_fringe_flicker_tests(effect, true);
	bool hair_green_ok = noisy_hair_tests(effect, false);
	bool hair_blue_ok = noisy_hair_tests(effect, true);
	gs_effect_destroy(effect);
	gs_leave_context();
	gs_destroy(graphics);
	return green_ok && blue_ok && sequences_ok && body_green_ok && body_blue_ok && mixed_ok && flyaway_ok && live_ok && hair_green_ok && hair_blue_ok ? 0 : 5;
}
