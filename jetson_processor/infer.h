/* infer.h -- EXTENSION POINT for learned body-tracking / pose estimation.
 *
 * This is intentionally a stub. Full body/pose reconstruction from WiFi (the
 * "RF-Pose" / "DensePose-from-WiFi" line of work) is a neural-network problem
 * that needs:
 *   1. SPATIAL DIVERSITY in the CSI input. Those papers used 3x3 antenna CSI
 *      tensors (multiple TX antennas x multiple RX antennas x subcarriers).
 *      A single 1x1 BCM43455 link does NOT provide enough information for pose;
 *      you would need several receiver Pis and/or several transmitters so the
 *      stacked CSI forms a usable [links x subcarriers x time] tensor.
 *   2. A model trained with synchronized camera/keypoint ground truth.
 *   3. Export to ONNX -> TensorRT for real-time inference on the Orin Nano GPU.
 *
 * The Jetson is the right place to run such a model. When you have one, build
 * the input tensor from dsp_t's per-link windows and run it through TensorRT
 * (C++), then call back into the JSON emitter. The classical localizer in this
 * repo gives you a working coarse-tracking baseline in the meantime.
 */
#ifndef INFER_H
#define INFER_H

#include "dsp.h"

typedef struct {
    int   n_keypoints;
    float kp_x[32];
    float kp_y[32];
    float kp_conf[32];
} pose_t;

/* Returns 1 if a pose was produced. Default build: returns 0 (not implemented).
 * Replace with a TensorRT-backed implementation in infer_trt.cpp. */
int pose_infer(const dsp_t *d, pose_t *out);

#endif /* INFER_H */
