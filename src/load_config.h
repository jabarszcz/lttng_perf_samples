#include "perf.h"

#ifndef LOAD_CONFIG_H_
#define LOAD_CONFIG_H_

int load_config(struct perf_sampling_config* config);

void set_default_config(struct perf_sampling_config* config);

#endif /* LOAD_CONFIG_H_ */
