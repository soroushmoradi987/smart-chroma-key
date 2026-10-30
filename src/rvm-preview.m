/* Local experimental RVM filter. Independent of Smart Chroma Key 1.3.7. */
#import <Foundation/Foundation.h>
#import <CoreML/CoreML.h>
#import <CoreVideo/CoreVideo.h>
#include <obs-module.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <util/platform.h>

#ifdef SCK_RVM_EMBEDDED
#define RVMWorker SCKIntegratedRVMWorker
#define RVM_KEY(key) "rvm_" key
#else
OBS_DECLARE_MODULE()
#define RVM_KEY(key) key
#endif
#define RW 1920
#define RH 1080
#define FRAME_BYTES ((size_t)RW * RH * 4)

@interface RVMWorker : NSObject {
    dispatch_queue_t _queue;
    NSLock *_lock;
    MLModel *_model;
    NSMutableDictionary *_states;
    BOOL _busy, _closed, _ready;
    NSData *_rgba, *_source;
    uint64_t _resultEpoch, _lastEpoch, _lastInputTime, _count;
    double _totalMS;
}
- (instancetype)initWithPath:(NSString *)path;
- (BOOL)canSubmit;
- (void)submit:(const uint8_t *)bytes stride:(uint32_t)stride epoch:(uint64_t)epoch;
- (BOOL)take:(NSData **)rgba source:(NSData **)source epoch:(uint64_t *)epoch;
- (void)shutdown;
@end
@implementation RVMWorker
- (instancetype)initWithPath:(NSString *)path {
    self = [super init];
    if (!self) return nil;
    _lock = [NSLock new];
    _queue = dispatch_queue_create("com.dinejahani.rvm-preview", DISPATCH_QUEUE_SERIAL);
    _states = [NSMutableDictionary new];
    dispatch_async(_queue, ^{
        @autoreleasepool {
            NSError *error = nil;
            MLModelConfiguration *config = [MLModelConfiguration new];
            config.computeUnits = MLComputeUnitsAll;
            self->_model = [MLModel modelWithContentsOfURL:[NSURL fileURLWithPath:path] configuration:config error:&error];
            [self->_lock lock]; self->_ready = self->_model != nil; [self->_lock unlock];
            blog(self->_model ? LOG_INFO : LOG_ERROR, "[RVM Preview] Model load: %s", self->_model ? "ready (1080p FP16)" : error.localizedDescription.UTF8String);
        }
    });
    return self;
}
- (BOOL)canSubmit {
    [_lock lock]; BOOL ok = _ready && !_busy && !_closed; [_lock unlock]; return ok;
}
- (void)submit:(const uint8_t *)bytes stride:(uint32_t)stride epoch:(uint64_t)epoch {
    [_lock lock];
    if (!_ready || _busy || _closed || stride < RW * 4) { [_lock unlock]; return; }
    _busy = YES; [_lock unlock];
    NSMutableData *original = [NSMutableData dataWithLength:FRAME_BYTES];
    for (int y = 0; y < RH; y++) memcpy((uint8_t *)original.mutableBytes + (size_t)y * RW * 4, bytes + (size_t)y * stride, RW * 4);
    dispatch_async(_queue, ^{
        @autoreleasepool {
            NSError *error = nil;
            uint64_t now = os_gettime_ns();
            if (epoch != self->_lastEpoch || now - self->_lastInputTime > 500000000ULL) [self->_states removeAllObjects];
            self->_lastEpoch = epoch; self->_lastInputTime = now;
            CVPixelBufferRef input = NULL;
            NSDictionary *attrs = @{(NSString *)kCVPixelBufferIOSurfacePropertiesKey: @{}};
            if (CVPixelBufferCreate(kCFAllocatorDefault, RW, RH, kCVPixelFormatType_32BGRA, (__bridge CFDictionaryRef)attrs, &input) == kCVReturnSuccess) {
                CVPixelBufferLockBaseAddress(input, 0);
                uint8_t *dst = CVPixelBufferGetBaseAddress(input);
                size_t ds = CVPixelBufferGetBytesPerRow(input);
                const uint8_t *src = original.bytes;
                for (int y = 0; y < RH; y++) for (int x = 0; x < RW; x++) {
                    size_t s = ((size_t)y * RW + x) * 4, d = y * ds + x * 4;
                    dst[d] = src[s+2]; dst[d+1] = src[s+1]; dst[d+2] = src[s]; dst[d+3] = 255;
                }
                CVPixelBufferUnlockBaseAddress(input, 0);
                NSMutableDictionary *features = [self->_states mutableCopy];
                features[@"src"] = [MLFeatureValue featureValueWithPixelBuffer:input];
                MLDictionaryFeatureProvider *provider = [[MLDictionaryFeatureProvider alloc] initWithDictionary:features error:&error];
                uint64_t start = os_gettime_ns();
                id<MLFeatureProvider> result = provider ? [self->_model predictionFromFeatures:provider error:&error] : nil;
                double ms = (os_gettime_ns() - start) / 1e6;
                CVPixelBufferRelease(input);
                CVPixelBufferRef fg = [result featureValueForName:@"fgr"].imageBufferValue;
                CVPixelBufferRef alpha = [result featureValueForName:@"pha"].imageBufferValue;
                if (fg && alpha && CVPixelBufferGetPixelFormatType(fg) == kCVPixelFormatType_32BGRA && CVPixelBufferGetPixelFormatType(alpha) == kCVPixelFormatType_OneComponent8 && CVPixelBufferGetWidth(fg) == RW && CVPixelBufferGetHeight(fg) == RH && CVPixelBufferGetWidth(alpha) == RW && CVPixelBufferGetHeight(alpha) == RH) {
                    for (int n = 1; n <= 4; n++) {
                        MLFeatureValue *state = [result featureValueForName:[NSString stringWithFormat:@"r%do", n]];
                        if (state) self->_states[[NSString stringWithFormat:@"r%di", n]] = state;
                    }
                    NSMutableData *rgba = [NSMutableData dataWithLength:FRAME_BYTES];
                    uint8_t *out = rgba.mutableBytes;
                    CVPixelBufferLockBaseAddress(fg, kCVPixelBufferLock_ReadOnly);
                    CVPixelBufferLockBaseAddress(alpha, kCVPixelBufferLock_ReadOnly);
                    const uint8_t *f = CVPixelBufferGetBaseAddress(fg), *a = CVPixelBufferGetBaseAddress(alpha);
                    size_t fs = CVPixelBufferGetBytesPerRow(fg), as = CVPixelBufferGetBytesPerRow(alpha);
                    for (int y = 0; y < RH; y++) for (int x = 0; x < RW; x++) {
                        size_t s = y * fs + x * 4, d = ((size_t)y * RW + x) * 4;
                        out[d] = f[s+2]; out[d+1] = f[s+1]; out[d+2] = f[s]; out[d+3] = a[y * as+x];
                    }
                    CVPixelBufferUnlockBaseAddress(alpha, kCVPixelBufferLock_ReadOnly);
                    CVPixelBufferUnlockBaseAddress(fg, kCVPixelBufferLock_ReadOnly);
                    [self->_lock lock];
                    if (!self->_closed) { self->_rgba = rgba; self->_source = original; self->_resultEpoch = epoch; }
                    [self->_lock unlock];
                    self->_count++; self->_totalMS += ms;
                    if (self->_count == 1 || self->_count % 120 == 0)
                        blog(LOG_INFO, "[RVM Preview] frames=%llu inference=%.2fms mean=%.2fms paired RGB/alpha", (unsigned long long)self->_count, ms, self->_totalMS/self->_count);
                } else {
                    [self->_states removeAllObjects];
                    blog(LOG_ERROR, "[RVM Preview] Prediction failed: %s", error ? error.localizedDescription.UTF8String : "unexpected output format");
                }
            } else { blog(LOG_ERROR, "[RVM Preview] Input buffer allocation failed"); }
            [self->_lock lock]; self->_busy = NO; [self->_lock unlock];
        }
    });
}
- (BOOL)take:(NSData **)rgba source:(NSData **)source epoch:(uint64_t *)epoch {
    [_lock lock];
    BOOL available = _rgba != nil;
    if (available) { *rgba = _rgba; *source = _source; *epoch = _resultEpoch; _rgba = nil; _source = nil; }
    [_lock unlock]; return available;
}
- (void)shutdown {
    [_lock lock]; _closed = YES; [_lock unlock];
    dispatch_sync(_queue, ^{});
    [_lock lock]; _rgba = nil; _source = nil; [_lock unlock];
}
@end

struct rvm_filter {
    obs_source_t *context;
    void *worker;
    gs_texrender_t *capture;
    gs_stagesurf_t *stage;
    gs_texture_t *rgba, *original;
    gs_effect_t *effect;
    bool pending, ticked, valid;
    uint32_t sw, sh;
    uint64_t epoch, last_render, last_result;
    float halo, despill;
    int screen, view;
};
static const char *rvm_name(void *unused) { (void)unused; return "RVM Hair Preview (macOS)"; }
static void rvm_update(void *data, obs_data_t *s) {
    struct rvm_filter *f = data;
    f->halo = (float)obs_data_get_double(s, RVM_KEY("halo"));
    f->despill = (float)obs_data_get_double(s, RVM_KEY("despill"));
    f->screen = (int)obs_data_get_int(s, RVM_KEY("screen"));
    f->view = (int)obs_data_get_int(s, RVM_KEY("view"));
}
static void rvm_defaults(obs_data_t *s) {
    obs_data_set_default_double(s, RVM_KEY("halo"), 0.15);
    obs_data_set_default_double(s, RVM_KEY("despill"), 0.95);
    obs_data_set_default_int(s, RVM_KEY("screen"), 1);
    obs_data_set_default_int(s, RVM_KEY("view"), 0);
}
static obs_properties_t *rvm_properties(void *data) {
    (void)data;
    obs_properties_t *p = obs_properties_create();
    obs_property_t *s = obs_properties_add_list(p, RVM_KEY("screen"), "Background / spill color", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(s, "Natural background (no despill)", 0);
    obs_property_list_add_int(s, "Green screen", 1);
    obs_property_list_add_int(s, "Blue screen", 2);
    obs_properties_add_float_slider(p, RVM_KEY("halo"), "Halo cleanup (0 keeps original hair matte)", 0, 1, 0.01);
    obs_properties_add_float_slider(p, RVM_KEY("despill"), "Remove reflected screen color", 0, 1, 0.01);
    s = obs_properties_add_list(p, RVM_KEY("view"), "Preview background", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(s, "Transparent / scene", 0);
    obs_property_list_add_int(s, "White", 1);
    obs_property_list_add_int(s, "Black", 2);
    obs_property_list_add_int(s, "Alpha mask", 3);
    return p;
}
static void rvm_destroy(void *data) {
    struct rvm_filter *f = data;
    if (!f) return;
    RVMWorker *worker = CFBridgingRelease(f->worker);
    [worker shutdown];
    obs_enter_graphics();
    if (f->capture) gs_texrender_destroy(f->capture);
    if (f->stage) gs_stagesurface_destroy(f->stage);
    if (f->rgba) gs_texture_destroy(f->rgba);
    if (f->original) gs_texture_destroy(f->original);
    if (f->effect) gs_effect_destroy(f->effect);
    obs_leave_graphics(); bfree(f);
}
static void *rvm_create(obs_data_t *s, obs_source_t *context) {
    struct rvm_filter *f = bzalloc(sizeof(*f)); f->context = context; f->epoch = 1;
    char *shader = obs_module_file("rvm-refine.effect"), *model = obs_module_file("rvm.mlmodelc");
    obs_enter_graphics();
    f->effect = shader ? gs_effect_create_from_file(shader, NULL) : NULL;
    f->capture = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    f->stage = gs_stagesurface_create(RW, RH, GS_RGBA);
    f->rgba = gs_texture_create(RW, RH, GS_RGBA, 1, NULL, GS_DYNAMIC);
    f->original = gs_texture_create(RW, RH, GS_RGBA, 1, NULL, GS_DYNAMIC);
    obs_leave_graphics();
    if (model) f->worker = (void *)CFBridgingRetain([[RVMWorker alloc] initWithPath:@(model)]);
    bfree(shader); bfree(model);
    if (!f->effect || !f->capture || !f->stage || !f->rgba || !f->original || !f->worker) { rvm_destroy(f); return NULL; }
    rvm_update(f, s); return f;
}
static void rvm_tick(void *data, float seconds) { (void)seconds; ((struct rvm_filter *)data)->ticked = false; }
static void rvm_render(void *data, gs_effect_t *unused) {
    (void)unused;
    struct rvm_filter *f = data;
    obs_source_t *target = obs_filter_get_target(f->context);
    if (!target) return;
    uint32_t sw = obs_source_get_base_width(target), sh = obs_source_get_base_height(target);
    if (!sw || !sh) { obs_source_skip_video_filter(f->context); return; }
    uint64_t now = os_gettime_ns();
    RVMWorker *worker = (__bridge RVMWorker *)f->worker;
    if (!f->ticked) {
        f->ticked = true;
        if (sw != f->sw || sh != f->sh || now - f->last_render > 500000000ULL) {
            f->epoch++; f->sw = sw; f->sh = sh; f->valid = false; f->pending = false;
        }
        f->last_render = now;
        @autoreleasepool {
            NSData *rgba = nil, *source = nil; uint64_t epoch = 0;
            if ([worker take:&rgba source:&source epoch:&epoch] && epoch == f->epoch) {
                gs_texture_set_image(f->rgba, rgba.bytes, RW * 4, false);
                gs_texture_set_image(f->original, source.bytes, RW * 4, false);
                f->valid = true; f->last_result = now;
            }
            if (f->pending && [worker canSubmit]) {
                uint8_t *mapped = NULL; uint32_t stride = 0;
                if (gs_stagesurface_map(f->stage, &mapped, &stride)) {
                    [worker submit:mapped stride:stride epoch:f->epoch];
                    gs_stagesurface_unmap(f->stage);
                }
                f->pending = false;
            }
        }
        gs_texrender_reset(f->capture);
        gs_blend_state_push(); gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
        if (gs_texrender_begin(f->capture, RW, RH)) {
            struct vec4 clear = {0}; gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
            gs_ortho(0, (float)sw, 0, (float)sh, -100, 100);
            obs_source_video_render(target);
            gs_texrender_end(f->capture);
            gs_stage_texture(f->stage, gs_texrender_get_texture(f->capture)); f->pending = true;
        }
        gs_blend_state_pop();
    }
    if (!f->valid || now - f->last_result > 1000000000ULL) { obs_source_skip_video_filter(f->context); return; }
    gs_effect_set_texture(gs_effect_get_param_by_name(f->effect, "image"), f->rgba);
    gs_effect_set_texture(gs_effect_get_param_by_name(f->effect, "original"), f->original);
    gs_effect_set_float(gs_effect_get_param_by_name(f->effect, "Halo"), f->halo);
    gs_effect_set_float(gs_effect_get_param_by_name(f->effect, "Despill"), f->despill);
    gs_effect_set_int(gs_effect_get_param_by_name(f->effect, "Screen"), f->screen);
    gs_effect_set_int(gs_effect_get_param_by_name(f->effect, "View"), f->view);
    while (gs_effect_loop(f->effect, "Draw")) gs_draw_sprite(f->rgba, 0, sw, sh);
}
static struct obs_source_info rvm_info = {
    .id = "smart_rvm_preview_filter", .type = OBS_SOURCE_TYPE_FILTER, .output_flags = OBS_SOURCE_VIDEO,
    .get_name = rvm_name, .create = rvm_create, .destroy = rvm_destroy, .update = rvm_update,
    .get_defaults = rvm_defaults, .get_properties = rvm_properties, .video_render = rvm_render, .video_tick = rvm_tick,
};
#ifdef SCK_RVM_EMBEDDED
const struct obs_source_info *sc_rvm_engine(void) { return &rvm_info; }
#else
bool obs_module_load(void) { obs_register_source(&rvm_info); blog(LOG_INFO, "[RVM Preview] Independent filter loaded 0.1.0"); return true; }

#endif
