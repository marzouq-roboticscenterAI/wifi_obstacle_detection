/* sim_collector.c -- stands in for the Raspberry Pi. Connects to the Jetson
 * processor over TCP and streams SYNTHETIC CSI simulating a person walking
 * through the room, so the full DSP+localize+track pipeline can be validated
 * with no radio hardware.
 *
 * Physics model (simplified but faithful to the sensing principle): for each
 * TX anchor, the line-of-sight path to the Pi (RX) carries CSI. When the
 * simulated person is near that path, we inject time-varying multipath fading
 * onto the subcarrier amplitudes -> the processor sees elevated motion energy
 * on exactly the links whose paths the person is crossing -> it triangulates.
 *
 * Build: make (top-level)   Run: ./sim_collector -H 127.0.0.1 -p 9999
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "../common/csi_protocol.h"
#include "../jetson_processor/config.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define N_SUB 64                 /* simulate a 20 MHz channel */

static volatile sig_atomic_t run = 1;
static void on_sig(int s){ (void)s; run = 0; }

static uint64_t now_ns(void){
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec*1000000000ull + ts.tv_nsec;
}

static float frand(void){ return (float)rand() / (float)RAND_MAX; }

static float dist_point_seg(float px,float py,float ax,float ay,float bx,float by){
    float vx=bx-ax, vy=by-ay, wx=px-ax, wy=py-ay;
    float vv=vx*vx+vy*vy, t=(vv>1e-9f)?(wx*vx+wy*vy)/vv:0.f;
    if(t<0)t=0; else if(t>1)t=1;
    float cx=ax+t*vx, cy=ay+t*vy, dx=px-cx, dy=py-cy;
    return sqrtf(dx*dx+dy*dy);
}

int main(int argc,char**argv){
    const char *host="127.0.0.1"; int port=9999, opt;
    while((opt=getopt(argc,argv,"H:p:h"))!=-1){
        if(opt=='H')host=optarg; else if(opt=='p')port=atoi(optarg);
        else{fprintf(stderr,"usage: %s [-H host] [-p port]\n",argv[0]);return 1;}
    }
    signal(SIGINT,on_sig); signal(SIGTERM,on_sig);
    srand(12345);

    int fd=socket(AF_INET,SOCK_STREAM,0);
    int one=1; setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));
    struct sockaddr_in sa={0}; sa.sin_family=AF_INET; sa.sin_port=htons(port);
    inet_pton(AF_INET,host,&sa.sin_addr);
    while(run && connect(fd,(struct sockaddr*)&sa,sizeof(sa))!=0){
        perror("connect (is the processor running?)"); sleep(1);
        close(fd); fd=socket(AF_INET,SOCK_STREAM,0);
        setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&one,sizeof(one));
    }
    fprintf(stderr,"[sim] connected to %s:%d, %d anchors\n",host,port,N_ANCHORS);

    /* per-subcarrier baseline amplitude with guard/DC nulls (exercise dead-sub) */
    float base[N_SUB], phi[N_SUB];
    for(int k=0;k<N_SUB;k++){
        int nul = (k<4)||(k>=60)||(k==31)||(k==32);
        base[k] = nul ? 1.0f : (800.0f + 400.0f*frand());
        phi[k]  = 2.0f*M_PI*frand();
    }

    uint8_t buf[sizeof(csi_wire_hdr_t)+N_SUB*4];
    csi_wire_hdr_t *h=(csi_wire_hdr_t*)buf;
    int16_t *csi=(int16_t*)(buf+sizeof(*h));

    uint64_t t0=now_ns();
    long frame=0;
    const float fade_hz=3.0f;      /* body Doppler */
    const float sigma=0.5f;        /* path influence width (m) */

    while(run){
        double t=(double)(now_ns()-t0)*1e-9;
        /* person walks left->right across the room at y=2.5, 0.5 m/s, looping */
        float period=10.0f;
        float ph=fmodf((float)t,2*period);
        float px = (ph<period) ? (0.5f+5.0f*ph/period)
                               : (5.5f-5.0f*(ph-period)/period);
        float py = 2.5f;

        for(int a=0;a<N_ANCHORS;a++){
            float d=dist_point_seg(px,py,ANCHORS[a].x,ANCHORS[a].y,RX_X,RX_Y);
            float m=0.6f*expf(-(d*d)/(2*sigma*sigma));   /* fade depth 0..0.6 */

            for(int k=0;k<N_SUB;k++){
                float amp=base[k];
                if(base[k]>2.0f)
                    amp *= (1.0f + m*sinf(2*M_PI*fade_hz*(float)t + phi[k])
                                 + 0.01f*(frand()-0.5f));
                float th=phi[k]+0.3f*(float)t;            /* arbitrary phase */
                csi[2*k+0]=(int16_t)(amp*cosf(th));
                csi[2*k+1]=(int16_t)(amp*sinf(th));
            }
            h->magic=CSI_WIRE_MAGIC; h->t_ns=now_ns();
            memcpy(h->src_mac,ANCHORS[a].mac,6);
            h->seq=(uint16_t)frame; h->chanspec=0x1000+a;
            h->core=0; h->nss=0; h->rssi=0; h->_pad=0; h->n_sub=N_SUB;
            if(send(fd,buf,sizeof(*h)+N_SUB*4,MSG_NOSIGNAL)<=0){
                fprintf(stderr,"[sim] link down\n"); goto done;
            }
        }
        frame++;
        usleep(1250);   /* ~800 frames/s total across links (~200 Hz/link) */
    }
done:
    close(fd);
    fprintf(stderr,"[sim] stopped after %ld frame-sets\n",frame);
    return 0;
}
