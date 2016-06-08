#include "perf.h"

#ifndef LOAD_CONFIG_H_
#define LOAD_CONFIG_H_

void set_default_config(struct perf_sampling_config* config);

int load_config(struct perf_sampling_config* config);

int load_config_from_file(struct perf_sampling_config* config,
			  char* filename);

#endif /* LOAD_CONFIG_H_ */
