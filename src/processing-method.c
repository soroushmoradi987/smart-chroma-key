/* Dispatch only: the two engines own their resources and settings separately. */
#include "processing-method.h"
#include <string.h>

struct processing_filter {
	obs_source_t *context;
	void *classic;
	void *rvm;
	obs_data_t *classic_settings;
	int method;
};

static const char *rvm_keys[] = {"rvm_halo", "rvm_despill", "rvm_screen", "rvm_view", "rvm_resolution"};

static const char *filter_name(void *unused)
{
	return sc_classic_engine()->get_name(unused);
}

static void defaults(obs_data_t *s)
{
	sc_classic_engine()->get_defaults(s);
	obs_data_set_default_int(s, "processing_method", 0);
	obs_data_set_default_double(s, "rvm_halo", 0.15);
	obs_data_set_default_double(s, "rvm_despill", 0.95);
	obs_data_set_default_int(s, "rvm_screen", 1);
	obs_data_set_default_int(s, "rvm_view", 0);
	obs_data_set_default_int(s, "rvm_resolution", 640);
}

static obs_data_t *classic_settings(obs_data_t *s)
{
	obs_data_t *copy = obs_data_create();
	obs_data_apply(copy, s);
	obs_data_erase(copy, "processing_method");
	for (size_t i = 0; i < sizeof(rvm_keys) / sizeof(rvm_keys[0]); i++)
		obs_data_erase(copy, rvm_keys[i]);
	sc_classic_engine()->get_defaults(copy);
	return copy;
}

static void update(void *data, obs_data_t *s)
{
	struct processing_filter *f = data;
	int method = obs_data_get_int(s, "processing_method") == 1 ? 1 : 0;
#ifndef SCK_RVM_EMBEDDED
	/* A scene saved on an AI-capable machine must remain usable here. */
	method = 0;
	obs_data_set_int(s, "processing_method", 0);
#endif
	bool switched = method != f->method;
	obs_data_t *classic = classic_settings(s);
	/* Inactive RVM controls must not even reset Classic's temporal history. */
	if (switched || strcmp(obs_data_get_json(classic), obs_data_get_json(f->classic_settings)) != 0)
		sc_classic_engine()->update(f->classic, classic);
	obs_data_release(f->classic_settings);
	f->classic_settings = classic;
#ifdef SCK_RVM_EMBEDDED
	if (method == 0 && f->rvm) {
		sc_rvm_engine()->destroy(f->rvm);
		f->rvm = NULL;
	}
	if (method == 1) {
		if (!f->rvm)
			f->rvm = sc_rvm_engine()->create(s, f->context);
		else
			sc_rvm_engine()->update(f->rvm, s);
	}
#endif
	f->method = method;
}

static void *create(obs_data_t *s, obs_source_t *context)
{
	struct processing_filter *f = bzalloc(sizeof(*f));
	f->context = context;
	f->classic_settings = classic_settings(s);
	f->classic = sc_classic_engine()->create(f->classic_settings, context);
	if (!f->classic) {
		obs_data_release(f->classic_settings);
		bfree(f);
		return NULL;
	}
	update(f, s);
	return f;
}

static void destroy(void *data)
{
	struct processing_filter *f = data;
	if (!f) return;
#ifdef SCK_RVM_EMBEDDED
	if (f->rvm) sc_rvm_engine()->destroy(f->rvm);
#endif
	sc_classic_engine()->destroy(f->classic);
	obs_data_release(f->classic_settings);
	bfree(f);
}

static bool method_changed(obs_properties_t *p, obs_property_t *property, obs_data_t *s)
{
	UNUSED_PARAMETER(property);
	bool rvm = obs_data_get_int(s, "processing_method") == 1;
	obs_property_set_visible(obs_properties_get(p, "classic_controls"), !rvm);
	obs_property_set_visible(obs_properties_get(p, "rvm_controls"), rvm);
	return true;
}

static obs_properties_t *properties(void *data)
{
	struct processing_filter *f = data;
	obs_properties_t *p = obs_properties_create();
	obs_property_t *method = obs_properties_add_list(p, "processing_method", "Processing method",
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(method, "Smart Chroma Key (Classic)", 0);
	obs_property_list_add_int(method, "AI - Robust Video Matting (RVM)", 1);
	obs_property_set_modified_callback(method, method_changed);
	obs_properties_add_group(p, "classic_controls", "Classic settings", OBS_GROUP_NORMAL,
		sc_classic_engine()->get_properties(f ? f->classic : NULL));
#ifdef SCK_RVM_EMBEDDED
	obs_properties_t *rvm = sc_rvm_engine()->get_properties(f ? f->rvm : NULL);
#else
	obs_property_list_item_disable(method, 1, true);
	obs_properties_t *rvm = obs_properties_create();
	obs_properties_add_text(rvm, "rvm_unavailable",
		"AI is not included in this build. Install a package with the RVM model.", OBS_TEXT_INFO);
#endif
	obs_properties_add_group(p, "rvm_controls", "RVM settings", OBS_GROUP_NORMAL, rvm);
	obs_data_t *s = f ? obs_source_get_settings(f->context) : obs_data_create();
	method_changed(p, method, s);
	obs_data_release(s);
	return p;
}

static void tick(void *data, float seconds)
{
	struct processing_filter *f = data;
#ifdef SCK_RVM_EMBEDDED
	if (f->method == 1 && f->rvm) sc_rvm_engine()->video_tick(f->rvm, seconds);
#else
	UNUSED_PARAMETER(f);
	UNUSED_PARAMETER(seconds);
#endif
}

static void render(void *data, gs_effect_t *effect)
{
	struct processing_filter *f = data;
	if (f->method == 0) {
		sc_classic_engine()->video_render(f->classic, effect);
		return;
	}
#ifdef SCK_RVM_EMBEDDED
	if (f->rvm) {
		sc_rvm_engine()->video_render(f->rvm, effect);
		return;
	}
#endif
	/* Missing RVM must not silently apply a different algorithm. */
	obs_source_skip_video_filter(f->context);
}

const struct obs_source_info *sc_processing_filter(void)
{
	static const struct obs_source_info info = {
		.id = "smart_chroma_key_filter", .type = OBS_SOURCE_TYPE_FILTER,
		.output_flags = OBS_SOURCE_VIDEO, .get_name = filter_name,
		.create = create, .destroy = destroy, .update = update,
		.get_defaults = defaults, .get_properties = properties,
		.video_tick = tick, .video_render = render,
	};
	return &info;
}
