/* localize_cuda.cu -- GPU radio-tomographic localizer for the Jetson Orin Nano.
 *
 * Same model as jetson_processor/localize.c (least-squares fit of observed
 * per-link motion energy to expected emax*exp(-d_seg^2/2sigma^2)), but every
 * grid cell is evaluated by a CUDA thread. On the small default grid the CPU is
 * already fine; the GPU wins as you scale up (finer grid, many receivers/links,
 * or a particle/voxel field for multi-person). This file both provides the
 * drop-in host function and a self-test that checks GPU==CPU and benchmarks.
 *
 * Build/verify on this laptop (RTX 5060) or on the Jetson:
 *   nvcc -O3 -arch=compute_75 -code=compute_75 -o localize_cuda localize_cuda.cu
 *   ./localize_cuda
 * (compute_75 PTX JITs forward onto newer GPUs incl. Blackwell/Orin.)
 */
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cfloat>

#include "../jetson_processor/config.h"

#define MAXK 64

struct fixres { float x, y, min_sse; };

/* ---- GPU kernel: one thread per grid cell -> SSE score --------------- */
__global__ void score_kernel(const float* __restrict obs,
                             const float* __restrict txx,
                             const float* __restrict txy,
                             int nk, float emax, float inv2s2,
                             float* __restrict out_sse)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = GRID_NX * GRID_NY;
    if (idx >= total) return;

    int ix = idx % GRID_NX, iy = idx / GRID_NX;
    float px = (ix + 0.5f) * GRID_RES_M;
    float py = (iy + 0.5f) * GRID_RES_M;

    float sse = 0.0f;
    for (int k = 0; k < nk; k++) {
        /* distance from cell to segment TX_k -> RX */
        float ax = txx[k], ay = txy[k];
        float vx = RX_X - ax, vy = RX_Y - ay;
        float wx = px - ax,   wy = py - ay;
        float vv = vx*vx + vy*vy;
        float t  = (vv > 1e-9f) ? (wx*vx + wy*vy)/vv : 0.0f;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        float cx = ax + t*vx, cy = ay + t*vy;
        float dx = px - cx, dy = py - cy;
        float d2 = dx*dx + dy*dy;
        float pred = emax * __expf(-d2 * inv2s2);
        float diff = obs[k] - pred;
        sse += diff*diff;
    }
    out_sse[idx] = sse;
}

/* host wrapper: returns best cell. Caller-managed device buffers for speed in a
 * real loop; here we (re)alloc for clarity. */
extern "C" fixres localize_cuda(const float* obs, const float* txx,
                                const float* txy, int nk, float emax)
{
    const int total = GRID_NX * GRID_NY;
    static float *d_obs=nullptr,*d_txx=nullptr,*d_txy=nullptr,*d_sse=nullptr;
    static float *h_sse=nullptr;
    if (!d_sse) {
        cudaMalloc(&d_obs, MAXK*sizeof(float));
        cudaMalloc(&d_txx, MAXK*sizeof(float));
        cudaMalloc(&d_txy, MAXK*sizeof(float));
        cudaMalloc(&d_sse, total*sizeof(float));
        h_sse = (float*)malloc(total*sizeof(float));
    }
    cudaMemcpy(d_obs, obs, nk*sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(d_txx, txx, nk*sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(d_txy, txy, nk*sizeof(float), cudaMemcpyHostToDevice);

    float inv2s2 = 1.0f/(2.0f*LINK_SIGMA_M*LINK_SIGMA_M);
    int threads = 256, blocks = (total + threads - 1)/threads;
    score_kernel<<<blocks, threads>>>(d_obs,d_txx,d_txy,nk,emax,inv2s2,d_sse);
    cudaMemcpy(h_sse, d_sse, total*sizeof(float), cudaMemcpyDeviceToHost);

    fixres r; r.min_sse = FLT_MAX; r.x = RX_X; r.y = RX_Y;
    for (int idx = 0; idx < total; idx++) {
        if (h_sse[idx] < r.min_sse) {
            r.min_sse = h_sse[idx];
            int ix = idx % GRID_NX, iy = idx / GRID_NX;
            r.x = (ix+0.5f)*GRID_RES_M; r.y = (iy+0.5f)*GRID_RES_M;
        }
    }
    return r;
}

/* ---- CPU reference (identical math) for verification ----------------- */
static fixres localize_cpu(const float* obs,const float* txx,const float* txy,
                           int nk,float emax)
{
    float inv2s2 = 1.0f/(2.0f*LINK_SIGMA_M*LINK_SIGMA_M);
    fixres r; r.min_sse=FLT_MAX; r.x=RX_X; r.y=RX_Y;
    for (int iy=0; iy<GRID_NY; iy++) for (int ix=0; ix<GRID_NX; ix++){
        float px=(ix+0.5f)*GRID_RES_M, py=(iy+0.5f)*GRID_RES_M, sse=0;
        for (int k=0;k<nk;k++){
            float ax=txx[k],ay=txy[k],vx=RX_X-ax,vy=RX_Y-ay,wx=px-ax,wy=py-ay;
            float vv=vx*vx+vy*vy,t=(vv>1e-9f)?(wx*vx+wy*vy)/vv:0; t=t<0?0:(t>1?1:t);
            float cx=ax+t*vx,cy=ay+t*vy,dx=px-cx,dy=py-cy;
            float pred=emax*expf(-(dx*dx+dy*dy)*inv2s2),diff=obs[k]-pred;
            sse+=diff*diff;
        }
        if(sse<r.min_sse){r.min_sse=sse;r.x=px;r.y=py;}
    }
    return r;
}

int main(void)
{
    /* simulate a person at (4.2, 2.5): build per-anchor expected energies. */
    float gx=4.2f, gy=2.5f, emax=0.02f;
    float obs[MAXK], txx[MAXK], txy[MAXK];
    int nk = N_ANCHORS;
    float inv2s2 = 1.0f/(2.0f*LINK_SIGMA_M*LINK_SIGMA_M);
    for (int k=0;k<nk;k++){
        txx[k]=ANCHORS[k].x; txy[k]=ANCHORS[k].y;
        float ax=txx[k],ay=txy[k],vx=RX_X-ax,vy=RX_Y-ay,wx=gx-ax,wy=gy-ay;
        float vv=vx*vx+vy*vy,t=(wx*vx+wy*vy)/vv; t=t<0?0:(t>1?1:t);
        float cx=ax+t*vx,cy=ay+t*vy,dx=gx-cx,dy=gy-cy;
        obs[k]=emax*expf(-(dx*dx+dy*dy)*inv2s2);
    }

    fixres c = localize_cpu(obs,txx,txy,nk,emax);
    fixres g = localize_cuda(obs,txx,txy,nk,emax);   /* warm up + correctness */
    cudaDeviceSynchronize();

    printf("grid %dx%d (%d cells), %d anchors\n", GRID_NX,GRID_NY,GRID_NX*GRID_NY,nk);
    printf("ground truth (%.2f,%.2f)\n", gx,gy);
    printf("CPU  fix (%.2f,%.2f) sse=%.3g\n", c.x,c.y,c.min_sse);
    printf("CUDA fix (%.2f,%.2f) sse=%.3g\n", g.x,g.y,g.min_sse);
    int match = (fabsf(c.x-g.x)<1e-3f && fabsf(c.y-g.y)<1e-3f);
    printf("match: %s\n", match? "YES":"NO");

    /* benchmark */
    const int N=2000;
    cudaEvent_t a,b; cudaEventCreate(&a); cudaEventCreate(&b);
    cudaEventRecord(a);
    for(int i=0;i<N;i++) localize_cuda(obs,txx,txy,nk,emax);
    cudaEventRecord(b); cudaEventSynchronize(b);
    float ms=0; cudaEventElapsedTime(&ms,a,b);
    printf("CUDA: %.1f us/fix over %d iters\n", ms*1000.0f/N, N);

    struct timespec t0,t1; clock_gettime(CLOCK_MONOTONIC,&t0);
    for(int i=0;i<N;i++) localize_cpu(obs,txx,txy,nk,emax);
    clock_gettime(CLOCK_MONOTONIC,&t1);
    double cpus=((t1.tv_sec-t0.tv_sec)*1e9+(t1.tv_nsec-t0.tv_nsec))/1e3/N;
    printf("CPU : %.1f us/fix\n", cpus);
    return match?0:1;
}
