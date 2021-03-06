#define _GNU_SOURCE

#include "autoconf.h"

#include <fcntl.h>
#include <stdbool.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include "perf.h"

static void inline IGNORE() {} // Ignore unused values explicitly

#define SETERR(err, event) {				\
		if (err) {				\
			(event).error_status = ERROR;	\
		}					\
	}

#define ERROR(err, message, count) {					\
		if (err) {						\
			IGNORE(write(perf_config.error_stream_fd,	\
				     message "\n",			\
				     (count) + 1));			\
			return -1;					\
		}							\
	}

#define SETERR_RET(err, event, message, count) {	\
		SETERR(err, event);			\
		ERROR(err, message, count);		\
	}

#define DEBUG(message, length) {					\
		if (perf_config.debug) {				\
			IGNORE(write(perf_config.error_stream_fd,	\
				     message "\n",			\
				     (length) + 1));			\
		}							\
	}

static struct perf_sampling_config perf_config = {0};

static void perf_sampling_sig_handler(int signo, siginfo_t * info, void * context);

static int set_signal_handler(void (*sig_handler)(int, siginfo_t *, void *),
			      bool check)
{
	int err;

	if (check) {
		// TODO check if signal is blocked.
	}

	struct sigaction oldact, act = {
		.sa_sigaction = sig_handler,
		.sa_flags = SA_SIGINFO,
	};

	// default signal is SIGIO
	if (!perf_config.signo) {
		perf_config.signo = SIGIO;
	}

	err = sigaction(perf_config.signo, &act, &oldact);
	ERROR(err, "sigaction() call failed", 23);

	if (check) {
		err = oldact.sa_handler != SIG_DFL &&
			oldact.sa_handler != SIG_IGN;
		ERROR(err, "old signal handler has been overwritten for perf",
		      48);
	}

	DEBUG("Signal handler installed", 24);

	return 0;
}

static long sys_perf_event_open(struct perf_event_attr *attr,
				pid_t pid, int cpu, int group_fd,
				unsigned long flags)
{
	return syscall(__NR_perf_event_open, attr, pid, cpu,
		       group_fd, flags);
}

static pid_t sys_gettid(void)
{
	return (pid_t) syscall(__NR_gettid);
}

static int perf_close_event(struct perf_event* event)
{
	int err;
	if (event->fd_status == CLOSED)
		return 0;

	err = close(event->fd);
	SETERR_RET(err, *event, "Error closing event", 19);

	event->error_status = OK;
	event->fd_status = CLOSED;
	event->fd = -1;

	return 0;
}

static int perf_open_event(struct perf_event* event)
{
	int fd, err;

	DEBUG("Open event file descriptor", 26);

	if (event->fd_status == OPEN) {
		err = perf_close_event(event);
		SETERR(err, *event);
		if (err)
			return -1;
	}

	// Register event and get a file descriptor
	fd = sys_perf_event_open(&(event->attr), 0, -1, -1, 0);
	SETERR_RET(fd < 0, *event, "Error starting sampling with sys_perf_event_open()", 50);

	event->fd_status = OPEN;
	event->fd = fd;

	// Get tid for fd setup
	pid_t tid = sys_gettid();

	// Setup fd
	//  Set fd owner to this thread - TODO check if this means
	//  that in a multi-threaded application, we always unwind
	//  this thread's stack (instead of sample thread)
	struct f_owner_ex fown = {
		.type = F_OWNER_TID,
		.pid = tid,
	};
	err = fcntl(fd, F_SETOWN_EX, &fown);
	SETERR_RET(err, *event, "Error setting file descriptor owner thread", 42);
	//  Set the signal to use
	fcntl(fd, F_SETSIG, perf_config.signo);
	SETERR_RET(err, *event, "Error setting async fd IO signal", 32);
	//  Setup async reading on file descriptor
	int flags = fcntl(fd, F_GETFL);
	err = fcntl(fd, F_SETFL, flags | O_ASYNC);
	SETERR_RET(err, *event, "Error setting async flag on file", 32);

	event->error_status = OK;
	return 0;
}

static inline int perf_trigger_one_sample_fd(int fd)
{
	int err = ioctl(fd, PERF_EVENT_IOC_REFRESH, 1);
	ERROR(err, "Error triggering perf_event sample", 34);

	return 0;
}

static inline int perf_trigger_one_sample_event(struct perf_event* event)
{
	int fd = event->fd;

	return perf_trigger_one_sample_fd(fd);
}

int perf_set_config(struct perf_sampling_config* config)
{
	int err;
	// Check for perf support : (from perf_event_open manpage)
	//
	//   "The official way of knowing if perf_event_open() support
	// is enabled is checking for the existence of the file
	// /proc/sys/kernel/perf_event_paranoid."
	err = access("/proc/sys/kernel/perf_event_paranoid", F_OK);
	ERROR(err, "perf_events not supported by this kernel", 40);

	// Set static config
	perf_config = *config;

	// Setup signal handler
	err = set_signal_handler(*perf_sampling_sig_handler, true);
	ERROR(err, "error in signal handler setup", 29);

	return 0;
}

void perf_event_init(struct perf_event* event)
{
	struct perf_event pe = {
		.attr = {
			.size = sizeof(struct perf_event_attr),

			// Start disabled, enable later
			.disabled = 1,

			// Default to a dummy event that counts nothing
			.type = PERF_TYPE_SOFTWARE,
			.config = PERF_COUNT_SW_DUMMY,
			.sample_period = 1000,
			.sample_type = PERF_SAMPLE_IP,

			// Count only in userspace of this process by default
			.inherit = 0,
			.exclude_kernel = 1,
			.exclude_hv = 1,

			// Stop and notify (and unwind) after one sample
			.wakeup_events = 1,
		},
		.fd = -1,
		.fd_status = CLOSED,
		.next = (struct perf_event *) 0,
	};

	*event = pe;
}

void perf_config_add_event(struct perf_sampling_config* config,
			   struct perf_event* event)
{
	// Get head
	struct perf_event* head = config->events;
	// If no head, set as first
	if (!head) {
		config->events = event;
		return;
	}
	// Consume list to get last
	for (;head->next; head = head->next);
	// Append event
	head->next = event;
}

int perf_start_one_sample_all_events(void)
{
	int err;
	// Get head
	struct perf_event* head = perf_config.events;

	// Start all events for one sample
	for (; head; head = head->next) {
		err = perf_open_event(head);
		err |= perf_trigger_one_sample_event(head);
		ERROR(err, "Error starting event", 20);
	}
	return 0;
}

int perf_stop(void)
{
	int err;
	// Get head
	struct perf_event* head = perf_config.events;
	// Close all events
	for (; head; head = head->next) {
		err = perf_close_event(head);
		ERROR(err, "Error closing event", 19);
	}
	return 0;
}

void perf_sampling_sig_handler(int signo, siginfo_t * info, void * context)
{
	int fd = info->si_fd;

	DEBUG("In signal handler", 17);
	if (perf_config.event_sample_cb)
		perf_config.event_sample_cb();

	perf_trigger_one_sample_fd(fd);
}
