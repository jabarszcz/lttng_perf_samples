all: lttng_perf_samples.so

lttng_perf_samples.so : lttng_perf_samples.c perf.c
	gcc -shared -fPIC lttng_perf_samples.c perf.c -o lttng_perf_samples.so \
		-Wall
