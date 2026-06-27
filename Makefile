# Top-level convenience build.
#   make processor   -> Jetson/host CSI processor (C, needs only libm)
#   make viz         -> 3D OpenGL visualizer (needs freeglut3-dev + a display)
#   make sim         -> synthetic collector for end-to-end testing (no hardware)
#   make cuda        -> GPU localizer self-test (needs nvcc)
#   make collector   -> Raspberry Pi collector (needs libpcap-dev; build ON the Pi)
#   make test        -> run processor + sim together for a few seconds
#
# Setup:
#   make configure   -> interactively set room geometry + anchors in config.h
# Dependencies (run once per device):
#   make deps-jetson -> apt-install everything the Jetson needs
#   make deps-pi     -> apt-install everything the Pi needs
# Turn-key launch (auto-builds what it needs):
#   make run-jetson  -> static IP + processor + live 3D view   (run ON the Jetson)
#   make run-pi      -> static IP + Nexmon CSI + collector      (run ON the Pi)
#   make share-net   -> (Jetson, optional) give the Pi internet over the link
.PHONY: all processor viz sim cuda collector test configure deps-jetson deps-pi \
        nexmon-part1 nexmon-part2 backup-firmware restore-firmware \
        net-jetson net-pi run-jetson run-pi share-net clean

all: processor sim

processor:
	$(MAKE) -C jetson_processor

viz:
	$(MAKE) -C viz

sim:
	cc -O2 -Wall -Wextra -std=c11 -o tools/sim_collector tools/sim_collector.c -lm

cuda:
	nvcc -O3 -arch=compute_75 -code=compute_75 -o cuda/localize_cuda cuda/localize_cuda.cu

collector:
	$(MAKE) -C pi_collector

configure:
	./scripts/configure.sh

deps-jetson:
	./scripts/install_jetson.sh

deps-pi:
	./scripts/install_pi.sh

# Nexmon CSI firmware install on the Pi (run with sudo; part1 reboots, then part2)
nexmon-part1:
	./scripts/install_nexmon_part1.sh

nexmon-part2:
	./scripts/install_nexmon_part2.sh

# Stock WiFi firmware backup/restore (offline). part2 auto-backs-up before flashing.
backup-firmware:
	./scripts/backup_firmware.sh

restore-firmware:
	./scripts/restore_firmware.sh

net-jetson:
	./scripts/net_jetson.sh

net-pi:
	./scripts/net_pi.sh

run-jetson:
	./scripts/run_jetson.sh

run-pi:
	./scripts/run_pi.sh

share-net:
	./scripts/share_net.sh

test: processor sim
	@./jetson_processor/csi_processor -p 9999 >/tmp/track.jsonl 2>/dev/null & \
	 P=$$!; sleep 0.4; timeout 12 ./tools/sim_collector -H 127.0.0.1 -p 9999; \
	 kill -9 $$P 2>/dev/null; \
	 echo "wrote /tmp/track.jsonl ($$(wc -l </tmp/track.jsonl) lines)"

clean:
	$(MAKE) -C jetson_processor clean
	$(MAKE) -C pi_collector clean 2>/dev/null || true
	$(MAKE) -C viz clean 2>/dev/null || true
	rm -f tools/sim_collector cuda/localize_cuda
