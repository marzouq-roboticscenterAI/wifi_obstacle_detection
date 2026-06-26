/* infer_stub.c -- default no-op pose backend. Swap for a TensorRT C++ impl. */
#include "infer.h"
int pose_infer(const dsp_t *d, pose_t *out){ (void)d; (void)out; return 0; }
