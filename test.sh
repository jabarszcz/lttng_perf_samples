lttng create;
lttng enable-event -k sched_switch,sched_wakeup;
lttng enable-event -k signal_generate,signal_deliver;
lttng enable-event -k lttng_logger;

make -B;

lttng start;

LD_PRELOAD=liblttng-ust-cyg-profile.so:./lttng_perf_samples.so bash -c "while true; do true; done;";

lttng stop;

lttng destroy -a;
