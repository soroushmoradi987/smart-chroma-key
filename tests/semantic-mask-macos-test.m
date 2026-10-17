#import "semantic-mask.h"

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    @autoreleasepool {
        const uint32_t width = 320;
        const uint32_t height = 180;
        const uint32_t linesize = width * 4;
        uint8_t *frame = calloc(height, linesize);
        if (!frame)
            return 2;

        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                uint8_t *pixel = frame + (size_t) y * linesize + (size_t) x * 4;
                pixel[0] = 20;
                pixel[1] = 180;
                pixel[2] = 20;
                pixel[3] = 255;
            }
        }

        struct sc_semantic_segmenter *segmenter = sc_semantic_segmenter_create();
        if (!segmenter) {
            free(frame);
            return 3;
        }
        if (!sc_semantic_segmenter_submit_rgba(segmenter, frame, width, height, linesize)) {
            sc_semantic_segmenter_destroy(segmenter);
            free(frame);
            return 4;
        }
        free(frame);

        for (int attempt = 0; attempt < 500 && sc_semantic_segmenter_is_busy(segmenter); attempt++)
            usleep(20000);

        struct sc_semantic_result result;
        bool received = sc_semantic_segmenter_take_result(segmenter, &result);
        if (!received || !result.mask || result.width == 0 || result.height == 0) {
            sc_semantic_segmenter_destroy(segmenter);
            return 5;
        }

        uint8_t maximum = 0;
        for (uint32_t y = 0; y < result.height; y++) {
            for (uint32_t x = 0; x < result.width; x++) {
                uint8_t value = result.mask[(size_t) y * result.linesize + x];
                if (value > maximum)
                    maximum = value;
            }
        }
        printf("Vision semantic mask: %ux%u, generation=%llu, max=%u, face=%s\n", result.width, result.height,
               (unsigned long long) result.generation, maximum, result.face_valid ? "yes" : "no");
        sc_semantic_result_release(&result);
        sc_semantic_segmenter_destroy(segmenter);
        return 0;
    }
}
