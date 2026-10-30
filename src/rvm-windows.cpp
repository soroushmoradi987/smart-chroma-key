/* Windows RVM: bounded asynchronous CPU inference, paired source/RGB/alpha.
 * The private runtime is loaded only when AI is selected; Classic has no ONNX
 * dependency. No camera frames leave the machine. */
#define NOMINMAX
#include <windows.h>
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#include "processing-method.h"
#include <graphics/vec4.h>
#include <util/platform.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
std::wstring wide(const char *text)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (!n) throw std::runtime_error("Invalid model/runtime path");
    std::wstring result(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result.data(), n);
    result.pop_back();
    return result;
}
std::wstring resource(const char *name)
{
    char *path = obs_module_file(name);
    if (!path) throw std::runtime_error(std::string("Missing bundled file: ") + name);
    std::string copy(path);
    bfree(path);
    return wide(copy.c_str());
}
void init_runtime()
{
    static std::once_flag once;
    std::call_once(once, [] {
        // Keep the DLL loaded for the plugin lifetime: ORT's process-wide API
        // table may still be referenced by other filter instances.
        HMODULE library = LoadLibraryExW(resource("runtime/sck-onnxruntime.dll").c_str(), nullptr,
                                        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!library) throw std::runtime_error("Cannot load bundled ONNX Runtime; reinstall the complete package");
        using GetApi = const OrtApiBase *(ORT_API_CALL *)();
        auto symbol = GetProcAddress(library, "OrtGetApiBase");
        GetApi get_api = nullptr;
        static_assert(sizeof(get_api) == sizeof(symbol));
        std::memcpy(&get_api, &symbol, sizeof(get_api));
        const OrtApi *api = get_api ? get_api()->GetApi(ORT_API_VERSION) : nullptr;
        if (!api) { FreeLibrary(library); throw std::runtime_error("ONNX Runtime API mismatch"); }
        Ort::InitApi(api);
    });
}
struct Frame {
    uint32_t width = 0, height = 0;
    uint64_t epoch = 0;
    std::vector<uint8_t> source, rgba;
};
class Worker {
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false, queued = false, busy = false, completed = false;
    Frame input, output;
    std::string status = "AI model is loading...";
    std::thread thread;
    void run() noexcept
    {
        try {
            init_runtime();
            Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "SmartChromaKey-RVM");
            Ort::SessionOptions options;
            options.SetIntraOpNumThreads(2);
            options.SetInterOpNumThreads(1);
            options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            Ort::Session session(env, resource("rvm.onnx").c_str(), options);
            Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            std::array<Ort::Value, 4> recurrent = {Ort::Value{nullptr}, Ort::Value{nullptr}, Ort::Value{nullptr}, Ort::Value{nullptr}};
            uint64_t epoch = 0, count = 0;
            {
                std::lock_guard<std::mutex> lock(mutex);
                status = "AI ready: RVM / ONNX Runtime (CPU). Processing stays on this computer.";
            }
            blog(LOG_INFO, "[Smart Chroma Key] Windows AI ready: RVM / ONNX Runtime CPU");
            for (;;) {
                Frame frame;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    wake.wait(lock, [&] { return stopping || queued; });
                    if (stopping) break;
                    frame = std::move(input); queued = false; busy = true;
                }
                uint64_t start = os_gettime_ns();
                const size_t pixels = static_cast<size_t>(frame.width) * frame.height;
                std::vector<float> rgb(pixels * 3);
                for (size_t i = 0; i < pixels; ++i)
                    for (size_t c = 0; c < 3; ++c)
                        rgb[c * pixels + i] = frame.source[4 * i + c] / 255.0f;
                int64_t shape[] = {1, 3, frame.height, frame.width};
                int64_t zero_shape[] = {1, 1, 1, 1}, ratio_shape[] = {1};
                float zero = 0.0f;
                float ratio = std::min(1.0f, 256.0f / static_cast<float>(std::max(frame.width, frame.height)));
                if (epoch != frame.epoch) {
                    for (auto &state : recurrent) state = Ort::Value{nullptr};
                    epoch = frame.epoch;
                }
                std::vector<Ort::Value> inputs;
                inputs.emplace_back(Ort::Value::CreateTensor<float>(memory, rgb.data(), rgb.size(), shape, 4));
                for (auto &state : recurrent) {
                    if (!state) state = Ort::Value::CreateTensor<float>(memory, &zero, 1, zero_shape, 4);
                    inputs.emplace_back(std::move(state));
                }
                inputs.emplace_back(Ort::Value::CreateTensor<float>(memory, &ratio, 1, ratio_shape, 1));
                const char *in_names[] = {"src", "r1i", "r2i", "r3i", "r4i", "downsample_ratio"};
                const char *out_names[] = {"fgr", "pha", "r1o", "r2o", "r3o", "r4o"};
                auto outputs = session.Run(Ort::RunOptions{nullptr}, in_names, inputs.data(), inputs.size(), out_names, 6);
                if (outputs[0].GetTensorTypeAndShapeInfo().GetElementCount() != pixels * 3 ||
                    outputs[1].GetTensorTypeAndShapeInfo().GetElementCount() != pixels)
                    throw std::runtime_error("Unexpected RVM output dimensions");
                const float *fgr = outputs[0].GetTensorData<float>(), *alpha = outputs[1].GetTensorData<float>();
                frame.rgba.resize(pixels * 4);
                auto byte = [](float v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
                for (size_t i = 0; i < pixels; ++i) {
                    for (size_t c = 0; c < 3; ++c) frame.rgba[4 * i + c] = byte(fgr[c * pixels + i]);
                    frame.rgba[4 * i + 3] = byte(alpha[i]);
                }
                for (size_t i = 0; i < 4; ++i) recurrent[i] = std::move(outputs[i + 2]);
                if (++count == 1 || count % 300 == 0)
                    blog(LOG_INFO, "[Smart Chroma Key] Windows AI frames=%llu inference=%.2fms (%ux%u)",
                         static_cast<unsigned long long>(count), static_cast<double>(os_gettime_ns() - start) / 1e6,
                         frame.width, frame.height);
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    output = std::move(frame); completed = true; busy = false;
                }
            }
        } catch (const std::exception &e) {
            blog(LOG_ERROR, "[Smart Chroma Key] Windows AI failed: %s", e.what());
            std::lock_guard<std::mutex> lock(mutex);
            status = std::string("AI unavailable: ") + e.what() + ". Select Classic or reinstall the plugin.";
            failed = true; busy = false;
        }
    }
public:
    std::atomic<bool> failed{false};
    Worker() { thread = std::thread([this] { run(); }); }
    ~Worker() {
        { std::lock_guard<std::mutex> lock(mutex); stopping = true; }
        wake.notify_one();
        thread.join();
    }
    std::string message() { std::lock_guard<std::mutex> lock(mutex); return status; }
    bool available() { std::lock_guard<std::mutex> lock(mutex); return !failed && !busy && !queued; }
    void submit(const uint8_t *data, uint32_t stride, uint32_t width, uint32_t height, uint64_t epoch) {
        std::lock_guard<std::mutex> lock(mutex);
        if (failed || busy || queued) return;
        input.width = width; input.height = height; input.epoch = epoch;
        input.source.resize(static_cast<size_t>(width) * height * 4);
        for (uint32_t y = 0; y < height; ++y)
            std::memcpy(input.source.data() + static_cast<size_t>(y) * width * 4, data + static_cast<size_t>(y) * stride, width * 4);
        queued = true; wake.notify_one();
    }
    bool take(Frame &frame) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!completed) return false;
        frame = std::move(output); completed = false; return true;
    }
};
struct Filter {
    obs_source_t *context = nullptr;
    Worker *worker = nullptr;
    gs_effect_t *effect = nullptr;
    gs_texrender_t *capture = nullptr;
    gs_stagesurf_t *stage = nullptr;
    gs_texture_t *rgba = nullptr, *original = nullptr;
    uint32_t width = 0, height = 0, source_width = 0, source_height = 0;
    uint64_t epoch = 1, last_render = 0, last_result = 0;
    bool ticked = false, pending = false, valid = false;
    float halo = 0.15f, despill = 0.95f;
    int screen = 1, view = 0, resolution = 640;
};
void update(void *data, obs_data_t *s) {
    auto *f = static_cast<Filter *>(data);
    f->halo = std::clamp(static_cast<float>(obs_data_get_double(s, "rvm_halo")), 0.0f, 1.0f);
    f->despill = std::clamp(static_cast<float>(obs_data_get_double(s, "rvm_despill")), 0.0f, 1.0f);
    f->screen = static_cast<int>(obs_data_get_int(s, "rvm_screen"));
    f->view = static_cast<int>(obs_data_get_int(s, "rvm_view"));
    int resolution = static_cast<int>(obs_data_get_int(s, "rvm_resolution"));
    f->resolution = resolution == 320 || resolution == 960 || resolution == 1280 ? resolution : 640;
}
void destroy(void *data) {
    auto *f = static_cast<Filter *>(data);
    if (!f) return;
    delete f->worker;
    obs_enter_graphics();
    if (f->effect) gs_effect_destroy(f->effect);
    if (f->capture) gs_texrender_destroy(f->capture);
    if (f->stage) gs_stagesurface_destroy(f->stage);
    if (f->rgba) gs_texture_destroy(f->rgba);
    if (f->original) gs_texture_destroy(f->original);
    obs_leave_graphics();
    delete f;
}
void *create(obs_data_t *s, obs_source_t *context) {
    auto *f = new Filter;
    f->context = context;
    char *path = obs_module_file("rvm-refine.effect"), *errors = nullptr;
    obs_enter_graphics();
    f->effect = path ? gs_effect_create_from_file(path, &errors) : nullptr;
    f->capture = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    obs_leave_graphics();
    bfree(path);
    if (errors) { blog(LOG_ERROR, "[Smart Chroma Key] AI shader: %s", errors); bfree(errors); }
    if (!f->effect || !f->capture) { destroy(f); return nullptr; }
    try { f->worker = new Worker; }
    catch (const std::exception &e) { blog(LOG_ERROR, "[Smart Chroma Key] AI worker: %s", e.what()); destroy(f); return nullptr; }
    update(f, s);
    return f;
}
obs_properties_t *properties(void *data) {
    auto *f = static_cast<Filter *>(data);
    obs_properties_t *p = obs_properties_create();
    std::string status = f && f->worker ? f->worker->message() : "RVM AI uses your CPU. Select AI to start the bundled model.";
    obs_properties_add_text(p, "rvm_status", status.c_str(), OBS_TEXT_INFO);
    obs_property_t *list = obs_properties_add_list(p, "rvm_resolution", "AI processing resolution (higher uses more CPU)", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(list, "320 - fastest", 320);
    obs_property_list_add_int(list, "640 - balanced", 640);
    obs_property_list_add_int(list, "960 - detailed", 960);
    obs_property_list_add_int(list, "1280 - highest detail", 1280);
    list = obs_properties_add_list(p, "rvm_screen", "Background / spill color", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(list, "None", 0); obs_property_list_add_int(list, "Green", 1); obs_property_list_add_int(list, "Blue", 2);
    obs_properties_add_float_slider(p, "rvm_halo", "Halo cleanup (0 keeps original hair matte)", 0, 1, 0.01);
    obs_properties_add_float_slider(p, "rvm_despill", "Remove reflected screen color", 0, 1, 0.01);
    list = obs_properties_add_list(p, "rvm_view", "Preview background", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(list, "Transparent", 0); obs_property_list_add_int(list, "White", 1);
    obs_property_list_add_int(list, "Black", 2); obs_property_list_add_int(list, "Alpha matte", 3);
    return p;
}
void tick(void *data, float) { static_cast<Filter *>(data)->ticked = false; }
void render(void *data, gs_effect_t *) {
    auto *f = static_cast<Filter *>(data);
    auto *target = obs_filter_get_target(f->context);
    if (!target || f->worker->failed) { obs_source_skip_video_filter(f->context); return; }
    uint32_t sw = obs_source_get_base_width(target), sh = obs_source_get_base_height(target);
    if (!sw || !sh) { obs_source_skip_video_filter(f->context); return; }
    double scale = std::min(1.0, static_cast<double>(f->resolution) / std::max(sw, sh));
    uint32_t w = std::max(32U, static_cast<uint32_t>(std::lround(sw * scale / 32)) * 32);
    uint32_t h = std::max(32U, static_cast<uint32_t>(std::lround(sh * scale / 32)) * 32);
    uint64_t now = os_gettime_ns();
    if (!f->ticked) {
        f->ticked = true;
        if (w != f->width || h != f->height || sw != f->source_width || sh != f->source_height || now - f->last_render > 500000000ULL) {
            ++f->epoch; f->valid = false; f->pending = false;
            if (f->stage) gs_stagesurface_destroy(f->stage);
            if (f->rgba) gs_texture_destroy(f->rgba);
            if (f->original) gs_texture_destroy(f->original);
            f->stage = gs_stagesurface_create(w, h, GS_RGBA);
            f->rgba = gs_texture_create(w, h, GS_RGBA, 1, nullptr, GS_DYNAMIC);
            f->original = gs_texture_create(w, h, GS_RGBA, 1, nullptr, GS_DYNAMIC);
            f->width = w; f->height = h; f->source_width = sw; f->source_height = sh;
        }
        f->last_render = now;
        if (!f->stage || !f->rgba || !f->original) { obs_source_skip_video_filter(f->context); return; }
        Frame result;
        if (f->worker->take(result) && result.epoch == f->epoch) {
            gs_texture_set_image(f->rgba, result.rgba.data(), w * 4, false);
            gs_texture_set_image(f->original, result.source.data(), w * 4, false);
            f->valid = true; f->last_result = now;
        }
        if (f->pending && f->worker->available()) {
            uint8_t *mapped = nullptr; uint32_t stride = 0;
            if (gs_stagesurface_map(f->stage, &mapped, &stride)) {
                f->worker->submit(mapped, stride, w, h, f->epoch);
                gs_stagesurface_unmap(f->stage);
            }
            f->pending = false;
        }
        // One pending GPU frame and one inference job at most; no latency queue.
        gs_texrender_reset(f->capture);
        gs_blend_state_push(); gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
        if (gs_texrender_begin(f->capture, w, h)) {
            struct vec4 clear = {};
            gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
            gs_ortho(0, static_cast<float>(sw), 0, static_cast<float>(sh), -100, 100);
            obs_source_video_render(target);
            gs_texrender_end(f->capture);
            gs_stage_texture(f->stage, gs_texrender_get_texture(f->capture)); f->pending = true;
        }
        gs_blend_state_pop();
    }
    if (!f->valid || now - f->last_result > 2000000000ULL) { obs_source_skip_video_filter(f->context); return; }
    gs_effect_set_texture(gs_effect_get_param_by_name(f->effect, "image"), f->rgba);
    gs_effect_set_texture(gs_effect_get_param_by_name(f->effect, "original"), f->original);
    gs_effect_set_float(gs_effect_get_param_by_name(f->effect, "Halo"), f->halo);
    gs_effect_set_float(gs_effect_get_param_by_name(f->effect, "Despill"), f->despill);
    gs_effect_set_int(gs_effect_get_param_by_name(f->effect, "Screen"), f->screen);
    gs_effect_set_int(gs_effect_get_param_by_name(f->effect, "View"), f->view);
    while (gs_effect_loop(f->effect, "Draw")) gs_draw_sprite(f->rgba, 0, sw, sh);
}
} // namespace

extern "C" const struct obs_source_info *sc_rvm_engine(void)
{
    static const obs_source_info info = [] {
        obs_source_info i = {};
        i.id = "smart_rvm_windows_engine"; i.type = OBS_SOURCE_TYPE_FILTER; i.output_flags = OBS_SOURCE_VIDEO;
        i.create = create; i.destroy = destroy; i.update = update; i.get_properties = properties;
        i.video_tick = tick; i.video_render = render;
        return i;
    }();
    return &info;
}
