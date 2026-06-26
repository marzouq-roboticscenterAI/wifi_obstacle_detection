# Top-level convenience build.
#   make processor   -> Jetson/host CSI processor (C, needs only libm)
#   make sim         -> synthetic collector for end-to-end testing (no hardware)
#   make cuda        -> GPU localizer self-test (needs nvcc)
#   make collector   -> Raspberry Pi collector (needs libpcap-dev; build ON the Pi)
#   make test        -> run processor + sim together for a few seconds
.PHONY: all processor sim cuda collector test clean

all: processor sim

processor:
	$(MAKE) -C jetson_processor

sim:
	cc -O2 -Wall -Wextra -std=c11 -o tools/sim_collector tools/sim_collector.c -lm

cuda:
	nvcc -O3 -arch=compute_75 -code=compute_75 -o cuda/localize_cuda cuda/localize_cuda.cu

collector:
	$(MAKE) -C pi_collector

test: processor sim
	@./jetson_processor/csi_processor -p 9999 >/tmp/track.jsonl 2>/dev/null & \
	 P=$$!; sleep 0.4; timeout 12 ./tools/sim_collector -H 127.0.0.1 -p 9999; \
	 kill -9 $$P 2>/dev/null; \
	 echo "wrote /tmp/track.jsonl ($$(wc -l </tmp/track.jsonl) lines)"

clean:
	$(MAKE) -C jetson_processor clean
	$(MAKE) -C pi_collector clean 2>/dev/null || true
	rm -f tools/sim_collector cuda/localize_cuda
