//TODO parameterize tracing : config file read to setup struct perf_sampling_config
#define _GNU_SOURCE

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "perf.h"

static void init_lttng_perf_samples(void) __attribute__((constructor));


static int lttng_logger;

void event_sample_cb(void)
{
	write(lttng_logger, "In signal callback\n", 19);
	write(STDERR_FILENO, ".", 1); // Debug

	// TODO Unwind and trace here
}

void init_lttng_perf_samples(void)
{
	lttng_logger = open("/proc/lttng-logger", O_RDWR);

	if (!lttng_logger) {
		fprintf(stderr, "lttng-logger file could not be opened");
		return;
	}
	
	struct perf_sampling_config config = {
		.error_stream_fd = lttng_logger, // or stderr/file/etc
		.event_sample_cb = event_sample_cb,
	};

	struct perf_event* event = malloc(sizeof(struct perf_event));

	perf_event_init(event);
	perf_config_add_event(&config, event);

	// Parameterize perf events (hardcoded right now for debugging)
	event->attr.type = PERF_TYPE_HARDWARE;
	event->attr.config = PERF_COUNT_HW_CPU_CYCLES; 

	perf_set_config(&config);

	perf_start_one_sample_all_events();
}


