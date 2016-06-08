#include <signal.h>

#include <linux/perf_event.h>

#ifndef PERF_H_
#define PERF_H_

typedef enum {CLOSED, OPEN} event_fd_status;
typedef enum {OK, ERROR} event_error_status;

struct perf_event {
	struct perf_event_attr attr;
	int fd;
	event_fd_status fd_status;
	event_error_status error_status;
	struct perf_event* next;
};

struct perf_sampling_config {
	int signo;
	struct perf_event* events;
	int error_stream_fd;
	int debug;
	void (*event_sample_cb)(void);
};

int perf_set_config(struct perf_sampling_config* config);

void perf_event_init(struct perf_event* event);

void perf_config_add_event(struct perf_sampling_config* config,
			   struct perf_event* event);

int perf_start_one_sample_all_events(void);

int perf_stop(void);

#endif /* PERF_H_ */
