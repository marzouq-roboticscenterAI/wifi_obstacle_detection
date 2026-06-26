/* csi_viz.c -- 3D OpenGL visualizer for the WiFi tracker (runs on the Jetson).
 *
 * Reads the processor's JSON-per-tick stream on STDIN and renders a top-down-ish
 * 3D room: the floor grid, each TX anchor (cube) with a beam to the Pi/RX whose
 * brightness = that link's live motion energy, the RX marker, the raw fix
 * (yellow), and the tracked person (sphere on a stalk) with a fading trail.
 * A HUD shows speed / confidence / active-link count.
 *
 * Decoupled by design -- pipe it:
 *     ./jetson_processor/csi_processor | ./viz/csi_viz
 * The processor stays fully headless-capable; this is just a viewer.
 *
 * Build (on the Jetson):  sudo apt install freeglut3-dev
 *                         make -C viz
 * Controls: drag = orbit, scroll = zoom, 'r' = reset view, q/ESC = quit.
 */
#include <GL/freeglut.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>

#include "../jetson_processor/config.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- shared scene state (updated from stdin, read by the renderer) ------- */
static float g_tx=0, g_ty=0, g_speed=0, g_conf=0;   /* track + quality        */
static int   g_tvalid=0, g_nactive=0;
static float g_fx=0, g_fy=0; static int g_fhave=0;   /* raw fix               */
static float g_energy[N_ANCHORS];                    /* per-anchor live energy */

#define TRAIL_MAX 256
static float g_trail[TRAIL_MAX][2];
static int   g_trail_n=0, g_trail_head=0;

/* camera orbit */
static float cam_yaw=0.7f, cam_pitch=0.9f, cam_dist=9.0f;
static int   mouse_x=0, mouse_y=0, mouse_down=0;

/* ---- tiny tolerant JSON field readers (format is fixed and self-produced) */
static int jget(const char *from, const char *key, float *out)
{
    const char *p = from ? strstr(from, key) : NULL;
    if (!p) return 0;
    p = strchr(p + strlen(key), ':');
    if (!p) return 0;
    return sscanf(p + 1, "%f", out) == 1;
}

static void push_trail(float x, float y)
{
    g_trail[g_trail_head][0] = x;
    g_trail[g_trail_head][1] = y;
    g_trail_head = (g_trail_head + 1) % TRAIL_MAX;
    if (g_trail_n < TRAIL_MAX) g_trail_n++;
}

static void parse_line(const char *line)
{
    const char *tb = strstr(line, "\"track\":");
    const char *fb = strstr(line, "\"fix\":");
    float v;
    if (tb) {
        if (jget(tb, "\"x\"", &v))      g_tx = v;
        if (jget(tb, "\"y\"", &v))      g_ty = v;
        if (jget(tb, "\"speed\"", &v))  g_speed = v;
        if (jget(tb, "\"valid\"", &v))  g_tvalid = (int)v;
    }
    if (fb) {
        if (jget(fb, "\"x\"", &v))      g_fx = v;
        if (jget(fb, "\"y\"", &v))      g_fy = v;
        if (jget(fb, "\"conf\"", &v))   g_conf = v;
        if (jget(fb, "\"n_active\"", &v)) g_nactive = (int)v;
        if (jget(fb, "\"have\"", &v))   g_fhave = (int)v;
    }
    /* links: map each {"name":"..","energy":..} to its anchor */
    for (int k = 0; k < N_ANCHORS; k++) g_energy[k] = 0.0f;
    const char *q = strstr(line, "\"links\":");
    while (q && (q = strstr(q, "\"name\":\""))) {
        q += 8;
        char nm[32]; int i = 0;
        while (*q && *q != '"' && i < 31) nm[i++] = *q++;
        nm[i] = 0;
        float e = 0; jget(q, "\"energy\"", &e);
        for (int k = 0; k < N_ANCHORS; k++)
            if (!strcmp(nm, ANCHORS[k].name)) g_energy[k] = e;
    }
    if (g_tvalid) push_trail(g_tx, g_ty);
}

/* ---- stdin pump (non-blocking, line-buffered) --------------------------- */
static void pump_stdin(void)
{
    static char buf[8192]; static int len = 0;
    char tmp[4096];
    ssize_t n;
    while ((n = read(STDIN_FILENO, tmp, sizeof(tmp))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            char c = tmp[i];
            if (c == '\n') { buf[len] = 0; if (len) parse_line(buf); len = 0; }
            else if (len < (int)sizeof(buf) - 1) buf[len++] = c;
        }
    }
}

/* ---- drawing helpers ---------------------------------------------------- */
static void energy_color(float e, float *r, float *g, float *b)
{
    /* normalize against the motion floor; low=green, high=red */
    float t = (e - MOTION_NOISE_FLOOR) / (8.0f * MOTION_NOISE_FLOOR);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    *r = t; *g = 1.0f - t; *b = 0.15f;
}

static void draw_text(float x, float y, const char *s)
{
    glRasterPos2f(x, y);
    for (; *s; s++) glutBitmapCharacter(GLUT_BITMAP_9_BY_15, *s);
}

static void draw_floor(void)
{
    /* filled floor */
    glColor3f(0.10f, 0.12f, 0.16f);
    glBegin(GL_QUADS);
        glVertex3f(0,0,0); glVertex3f(ROOM_W_M,0,0);
        glVertex3f(ROOM_W_M,0,ROOM_H_M); glVertex3f(0,0,ROOM_H_M);
    glEnd();
    /* grid */
    glColor3f(0.25f, 0.28f, 0.33f);
    glBegin(GL_LINES);
    for (float x = 0; x <= ROOM_W_M + 1e-3f; x += 0.5f) {
        glVertex3f(x,0,0); glVertex3f(x,0,ROOM_H_M);
    }
    for (float z = 0; z <= ROOM_H_M + 1e-3f; z += 0.5f) {
        glVertex3f(0,0,z); glVertex3f(ROOM_W_M,0,z);
    }
    glEnd();
}

static void draw_cube_at(float x, float y, float s)
{
    glPushMatrix();
    glTranslatef(x, s*0.5f, y);
    glutSolidCube(s);
    glPopMatrix();
}

static void display(void)
{
    glClearColor(0.04f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* ---- 3D scene ---- */
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    int w = glutGet(GLUT_WINDOW_WIDTH), h = glutGet(GLUT_WINDOW_HEIGHT);
    gluPerspective(50.0, (double)w / (h?h:1), 0.1, 100.0);

    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    float cx = ROOM_W_M*0.5f, cz = ROOM_H_M*0.5f;
    float ex = cx + cam_dist * cosf(cam_pitch) * sinf(cam_yaw);
    float ey =      cam_dist * sinf(cam_pitch);
    float ez = cz + cam_dist * cosf(cam_pitch) * cosf(cam_yaw);
    gluLookAt(ex, ey, ez, cx, 0.6f, cz, 0, 1, 0);

    glEnable(GL_DEPTH_TEST);
    draw_floor();

    /* link beams + anchors */
    for (int k = 0; k < N_ANCHORS; k++) {
        float r,g,b; energy_color(g_energy[k], &r,&g,&b);
        glLineWidth(1.0f + 5.0f * (r));          /* hotter link = thicker */
        glColor3f(r,g,b);
        glBegin(GL_LINES);
            glVertex3f(ANCHORS[k].x, 0.05f, ANCHORS[k].y);
            glVertex3f(RX_X,         0.05f, RX_Y);
        glEnd();
        glColor3f(0.8f, 0.8f, 0.2f);
        draw_cube_at(ANCHORS[k].x, ANCHORS[k].y, 0.22f);
    }
    glLineWidth(1.0f);

    /* RX (the Pi) */
    glColor3f(0.3f, 0.6f, 1.0f);
    draw_cube_at(RX_X, RX_Y, 0.28f);

    /* raw fix marker */
    if (g_fhave) {
        glColor3f(0.9f, 0.9f, 0.2f);
        glPushMatrix(); glTranslatef(g_fx, 0.05f, g_fy);
        glutSolidSphere(0.10, 12, 12); glPopMatrix();
    }

    /* trail */
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i < g_trail_n; i++) {
        int idx = (g_trail_head - g_trail_n + i + TRAIL_MAX*2) % TRAIL_MAX;
        float a = (float)i / (g_trail_n>1?g_trail_n-1:1);
        glColor3f(0.2f*a, 0.7f*a, 1.0f*a);
        glVertex3f(g_trail[idx][0], 0.05f, g_trail[idx][1]);
    }
    glEnd();

    /* tracked person */
    if (g_tvalid) {
        glColor3f(0.2f, 0.9f, 1.0f);
        glBegin(GL_LINES);                       /* stalk to floor */
            glVertex3f(g_tx, 0.0f, g_ty);
            glVertex3f(g_tx, 1.6f, g_ty);
        glEnd();
        glPushMatrix(); glTranslatef(g_tx, 1.75f, g_ty);
        glutSolidSphere(0.18, 16, 16); glPopMatrix();
    }

    /* ---- 2D HUD ---- */
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    gluOrtho2D(0, w, 0, h);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glColor3f(0.9f, 0.95f, 1.0f);
    char line[160];
    snprintf(line, sizeof line,
             "person: (%.2f, %.2f) m   speed %.2f m/s   conf %.2f   active links %d   %s",
             g_tx, g_ty, g_speed, g_conf, g_nactive,
             g_tvalid ? "TRACKING" : "-- no track --");
    draw_text(10, h - 22, line);
    draw_text(10, 12, "drag: orbit   scroll: zoom   r: reset   q: quit");
    glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

    glutSwapBuffers();
}

static void timer(int v)
{
    (void)v;
    pump_stdin();
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);    /* ~60 fps */
}

static void on_key(unsigned char k, int x, int y)
{
    (void)x; (void)y;
    if (k == 'q' || k == 27) { glutLeaveMainLoop(); }
    if (k == 'r') { cam_yaw=0.7f; cam_pitch=0.9f; cam_dist=9.0f; }
}
static void on_mouse(int b, int s, int x, int y)
{
    if (b == GLUT_LEFT_BUTTON) { mouse_down = (s==GLUT_DOWN); mouse_x=x; mouse_y=y; }
    if (b == 3 && s==GLUT_DOWN) cam_dist *= 0.9f;   /* wheel up */
    if (b == 4 && s==GLUT_DOWN) cam_dist *= 1.1f;   /* wheel down */
    if (cam_dist < 1.5f) cam_dist = 1.5f;
    if (cam_dist > 40.f) cam_dist = 40.f;
}
static void on_motion(int x, int y)
{
    if (!mouse_down) return;
    cam_yaw   += (x - mouse_x) * 0.01f;
    cam_pitch += (y - mouse_y) * 0.01f;
    if (cam_pitch < 0.15f) cam_pitch = 0.15f;
    if (cam_pitch > 1.50f) cam_pitch = 1.50f;
    mouse_x = x; mouse_y = y;
}

int main(int argc, char **argv)
{
    /* non-blocking stdin so the GL loop never stalls waiting for data */
    int fl = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, fl | O_NONBLOCK);

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(1100, 800);
    glutCreateWindow("WiFi person tracker -- live");

    glutDisplayFunc(display);
    glutKeyboardFunc(on_key);
    glutMouseFunc(on_mouse);
    glutMotionFunc(on_motion);
    glutTimerFunc(16, timer, 0);

    for (int k = 0; k < N_ANCHORS; k++) g_energy[k] = 0.0f;
    glutMainLoop();
    return 0;
}
