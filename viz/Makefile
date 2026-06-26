# 3D OpenGL visualizer (Jetson; needs a display + freeglut3-dev)
#   sudo apt install freeglut3-dev
CC     ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
LDLIBS := -lglut -lGLU -lGL -lm

csi_viz: csi_viz.c ../jetson_processor/config.h
	$(CC) $(CFLAGS) -o $@ csi_viz.c $(LDLIBS)

clean:
	rm -f csi_viz

.PHONY: clean
