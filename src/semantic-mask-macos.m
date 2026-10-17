#import "semantic-mask.h"

#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <Vision/Vision.h>

@interface SCSemanticWorker : NSObject {
    dispatch_queue_t _queue;
    NSLock *_lock;
    VNGeneratePersonSegmentationRequest *_personRequest;
    BOOL _busy;
    BOOL _shuttingDown;
    NSData *_latestMask;
    uint32_t _latestWidth;
    uint32_t _latestHeight;
    uint64_t _generation;
    BOOL _faceValid;
    float _faceRect[4];
}
- (BOOL)submitRGBA:(const uint8_t *)data width:(uint32_t)width height:(uint32_t)height linesize:(uint32_t)linesize;
- (BOOL)isBusy;
- (BOOL)takeResult:(struct sc_semantic_result *)result;
- (void)shutdown;
@end

@implementation SCSemanticWorker

- (instancetype)init
{
    self = [super init];
    if (self) {
        _queue = dispatch_queue_create("com.dinejahani.smart-chroma-key.vision", DISPATCH_QUEUE_SERIAL);
        _lock = [[NSLock alloc] init];
        _personRequest = [[VNGeneratePersonSegmentationRequest alloc] init];
        /* Apple's Balanced path is intended for live video. Accurate performs an
		 * additional matting refinement that arrives several frames late in OBS,
		 * producing a visibly detached silhouette while the subject moves. */
        _personRequest.qualityLevel = VNGeneratePersonSegmentationRequestQualityLevelBalanced;
        _personRequest.outputPixelFormat = kCVPixelFormatType_OneComponent8;
    }
    return self;
}

- (BOOL)submitRGBA:(const uint8_t *)data width:(uint32_t)width height:(uint32_t)height linesize:(uint32_t)linesize
{
    if (!data || width == 0 || height == 0 || linesize < width * 4)
        return NO;

    [_lock lock];
    if (_busy || _shuttingDown) {
        [_lock unlock];
        return NO;
    }
    _busy = YES;
    [_lock unlock];

    NSMutableData *tightRGBA = [NSMutableData dataWithLength:(NSUInteger) width * height * 4];
    uint8_t *tightBytes = tightRGBA.mutableBytes;
    for (uint32_t y = 0; y < height; y++)
        memcpy(tightBytes + (size_t) y * width * 4, data + (size_t) y * linesize, (size_t) width * 4);

    __weak SCSemanticWorker *weakSelf = self;
    dispatch_async(_queue, ^{
        @autoreleasepool {
            SCSemanticWorker *strongSelf = weakSelf;
            if (!strongSelf)
                return;

            CVPixelBufferRef input = NULL;
            NSDictionary *attributes = @{
                (NSString *) kCVPixelBufferCGImageCompatibilityKey: @YES,
                (NSString *) kCVPixelBufferCGBitmapContextCompatibilityKey: @YES,
            };
            CVReturn cvResult = CVPixelBufferCreate(kCFAllocatorDefault, width, height, kCVPixelFormatType_32BGRA,
                                                    (__bridge CFDictionaryRef) attributes, &input);
            if (cvResult != kCVReturnSuccess || !input) {
                NSLog(@"[Smart Chroma Key] Vision input allocation failed: %d", cvResult);
                [strongSelf finishWithoutResult];
                return;
            }

            CVPixelBufferLockBaseAddress(input, 0);
            uint8_t *destination = CVPixelBufferGetBaseAddress(input);
            size_t destinationStride = CVPixelBufferGetBytesPerRow(input);
            const uint8_t *source = tightRGBA.bytes;
            for (uint32_t y = 0; y < height; y++) {
                uint8_t *destinationRow = destination + (size_t) y * destinationStride;
                const uint8_t *sourceRow = source + (size_t) y * width * 4;
                for (uint32_t x = 0; x < width; x++) {
                    destinationRow[x * 4 + 0] = sourceRow[x * 4 + 2];
                    destinationRow[x * 4 + 1] = sourceRow[x * 4 + 1];
                    destinationRow[x * 4 + 2] = sourceRow[x * 4 + 0];
                    destinationRow[x * 4 + 3] = sourceRow[x * 4 + 3];
                }
            }
            CVPixelBufferUnlockBaseAddress(input, 0);

            VNDetectFaceRectanglesRequest *faceRequest = [[VNDetectFaceRectanglesRequest alloc] init];
            faceRequest.revision = VNDetectFaceRectanglesRequestRevision3;
            VNImageRequestHandler *handler =
                [[VNImageRequestHandler alloc] initWithCVPixelBuffer:input orientation:kCGImagePropertyOrientationUp
                                                             options:@ {}];
            NSError *error = nil;
            BOOL succeeded = [handler performRequests:@[strongSelf->_personRequest, faceRequest] error:&error];
            CVPixelBufferRelease(input);

            VNPixelBufferObservation *maskObservation = strongSelf->_personRequest.results.firstObject;
            if (!succeeded || !maskObservation) {
                if (error)
                    NSLog(@"[Smart Chroma Key] Vision inference failed: %@", error.localizedDescription);
                [strongSelf finishWithoutResult];
                return;
            }

            CVPixelBufferRef maskBuffer = maskObservation.pixelBuffer;
            CVPixelBufferLockBaseAddress(maskBuffer, kCVPixelBufferLock_ReadOnly);
            uint32_t maskWidth = (uint32_t) CVPixelBufferGetWidth(maskBuffer);
            uint32_t maskHeight = (uint32_t) CVPixelBufferGetHeight(maskBuffer);
            size_t maskStride = CVPixelBufferGetBytesPerRow(maskBuffer);
            const uint8_t *maskBase = CVPixelBufferGetBaseAddress(maskBuffer);
            NSMutableData *mask = [NSMutableData dataWithLength:(NSUInteger) maskWidth * maskHeight];
            uint8_t *maskBytes = mask.mutableBytes;
            for (uint32_t y = 0; y < maskHeight; y++)
                memcpy(maskBytes + (size_t) y * maskWidth, maskBase + (size_t) y * maskStride, maskWidth);
            CVPixelBufferUnlockBaseAddress(maskBuffer, kCVPixelBufferLock_ReadOnly);

            BOOL faceValid = NO;
            CGRect largestFace = CGRectZero;
            CGFloat largestArea = 0.0;
            for (VNFaceObservation *observation in faceRequest.results) {
                CGRect candidate = observation.boundingBox;
                CGFloat area = candidate.size.width * candidate.size.height;
                if (area > largestArea) {
                    largestArea = area;
                    largestFace = candidate;
                    faceValid = YES;
                }
            }

            float faceRect[4] = {0};
            if (faceValid) {
                faceRect[0] = (float) CGRectGetMinX(largestFace);
                faceRect[1] = (float) (1.0 - CGRectGetMaxY(largestFace));
                faceRect[2] = (float) CGRectGetMaxX(largestFace);
                faceRect[3] = (float) (1.0 - CGRectGetMinY(largestFace));
            }

            [strongSelf->_lock lock];
            if (!strongSelf->_shuttingDown) {
                strongSelf->_latestMask = mask;
                strongSelf->_latestWidth = maskWidth;
                strongSelf->_latestHeight = maskHeight;
                strongSelf->_faceValid = faceValid;
                memcpy(strongSelf->_faceRect, faceRect, sizeof(faceRect));
                strongSelf->_generation++;
            }
            strongSelf->_busy = NO;
            [strongSelf->_lock unlock];
        }
    });

    return YES;
}

- (void)finishWithoutResult
{
    [_lock lock];
    _busy = NO;
    [_lock unlock];
}

- (BOOL)isBusy
{
    [_lock lock];
    BOOL busy = _busy;
    [_lock unlock];
    return busy;
}

- (BOOL)takeResult:(struct sc_semantic_result *)result
{
    if (!result)
        return NO;
    memset(result, 0, sizeof(*result));

    [_lock lock];
    if (!_latestMask) {
        [_lock unlock];
        return NO;
    }

    result->width = _latestWidth;
    result->height = _latestHeight;
    result->linesize = _latestWidth;
    result->generation = _generation;
    result->face_valid = _faceValid;
    memcpy(result->face_rect, _faceRect, sizeof(result->face_rect));
    result->mask = malloc(_latestMask.length);
    if (result->mask)
        memcpy(result->mask, _latestMask.bytes, _latestMask.length);
    _latestMask = nil;
    [_lock unlock];

    return result->mask != NULL;
}

- (void)shutdown
{
    [_lock lock];
    _shuttingDown = YES;
    [_lock unlock];
    dispatch_sync(_queue, ^ {
                  });
}

@end

struct sc_semantic_segmenter {
    void *worker;
};

struct sc_semantic_segmenter *sc_semantic_segmenter_create(void)
{
    if (@available(macOS 12.0, *)) {
        struct sc_semantic_segmenter *segmenter = calloc(1, sizeof(*segmenter));
        if (!segmenter)
            return NULL;
        SCSemanticWorker *worker = [[SCSemanticWorker alloc] init];
        segmenter->worker = (__bridge_retained void *) worker;
        return segmenter;
    }
    return NULL;
}

void sc_semantic_segmenter_destroy(struct sc_semantic_segmenter *segmenter)
{
    if (!segmenter)
        return;
    SCSemanticWorker *worker = (__bridge_transfer SCSemanticWorker *) segmenter->worker;
    [worker shutdown];
    worker = nil;
    free(segmenter);
}

bool sc_semantic_segmenter_submit_rgba(struct sc_semantic_segmenter *segmenter, const uint8_t *data, uint32_t width,
                                       uint32_t height, uint32_t linesize)
{
    if (!segmenter)
        return false;
    SCSemanticWorker *worker = (__bridge SCSemanticWorker *) segmenter->worker;
    return [worker submitRGBA:data width:width height:height linesize:linesize];
}

bool sc_semantic_segmenter_is_busy(struct sc_semantic_segmenter *segmenter)
{
    if (!segmenter)
        return false;
    SCSemanticWorker *worker = (__bridge SCSemanticWorker *) segmenter->worker;
    return [worker isBusy];
}

bool sc_semantic_segmenter_take_result(struct sc_semantic_segmenter *segmenter, struct sc_semantic_result *result)
{
    if (!segmenter)
        return false;
    SCSemanticWorker *worker = (__bridge SCSemanticWorker *) segmenter->worker;
    return [worker takeResult:result];
}

void sc_semantic_result_release(struct sc_semantic_result *result)
{
    if (!result)
        return;
    free(result->mask);
    memset(result, 0, sizeof(*result));
}
