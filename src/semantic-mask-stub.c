#include "semantic-mask.h"

#include <stdlib.h>
#include <string.h>

struct sc_semantic_segmenter {
	int unused;
};

struct sc_semantic_segmenter *sc_semantic_segmenter_create(void)
{
	return NULL;
}

void sc_semantic_segmenter_destroy(struct sc_semantic_segmenter *segmenter)
{
	free(segmenter);
}

bool sc_semantic_segmenter_submit_rgba(struct sc_semantic_segmenter *segmenter, const uint8_t *data, uint32_t width,
				       uint32_t height, uint32_t linesize)
{
	(void)segmenter;
	(void)data;
	(void)width;
	(void)height;
	(void)linesize;
	return false;
}

bool sc_semantic_segmenter_is_busy(struct sc_semantic_segmenter *segmenter)
{
	(void)segmenter;
	return false;
}

bool sc_semantic_segmenter_take_result(struct sc_semantic_segmenter *segmenter, struct sc_semantic_result *result)
{
	(void)segmenter;
	if (result)
		memset(result, 0, sizeof(*result));
	return false;
}

void sc_semantic_result_release(struct sc_semantic_result *result)
{
	if (!result)
		return;
	free(result->mask);
	memset(result, 0, sizeof(*result));
}
