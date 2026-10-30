#pragma once
#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct obs_source_info *sc_classic_engine(void);
const struct obs_source_info *sc_processing_filter(void);
#ifdef SCK_RVM_EMBEDDED
const struct obs_source_info *sc_rvm_engine(void);
#endif
#ifdef __cplusplus
}
#endif
