#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct sc_semantic_segmenter;

struct sc_semantic_result {
	uint8_t *mask;
	uint32_t width;
	uint32_t height;
	uint32_t linesize;
	uint64_t generation;
	bool face_valid;
	/* Normalized texture coordinates: left, top, right, bottom. */
	float face_rect[4];
};

struct sc_semantic_segmenter *sc_semantic_segmenter_create(void);
void sc_semantic_segmenter_destroy(struct sc_semantic_segmenter *segmenter);

/* Copies the RGBA frame before returning and performs inference asynchronously. */
bool sc_semantic_segmenter_submit_rgba(struct sc_semantic_segmenter *segmenter, const uint8_t *data, uint32_t width,
				       uint32_t height, uint32_t linesize);
bool sc_semantic_segmenter_is_busy(struct sc_semantic_segmenter *segmenter);

/* Transfers ownership of result.mask to the caller. */
bool sc_semantic_segmenter_take_result(struct sc_semantic_segmenter *segmenter, struct sc_semantic_result *result);
void sc_semantic_result_release(struct sc_semantic_result *result);
