#define _GNU_SOURCE

#include "autoconf.h"

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "load_config.h"
#include "perf.h"

static inline void IGNORE(){}

static void init_lttng_perf_samples(void) __attribute__((constructor));

static int lttng_logger;

void event_sample_cb(void)
{
	IGNORE(write(lttng_logger, "In signal callback\n", 19));
	IGNORE(write(STDERR_FILENO, ".", 1)); // Debug

	// TODO Unwind and trace here
}

void init_lttng_perf_samples(void)
{
	struct perf_sampling_config config;

	load_config(&config);

	config.event_sample_cb = event_sample_cb;

	perf_set_config(&config);

	perf_start_one_sample_all_events();
}


