// Tiny SDF raymarcher that fakes an FDM 3D print photo.
// usage: render <scene A|B|C|D> <yaw_deg> <elev_deg> <W> <H> <ss> <screen.rgb> <out.f32>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dispatch/dispatch.h>

typedef struct { float x, y, z; } v3;
static inline v3 V(float x, float y, float z) { v3 r = {x, y, z}; return r; }
static inline v3 add(v3 a, v3 b) { return V(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline v3 sub(v3 a, v3 b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline v3 mul(v3 a, float s) { return V(a.x * s, a.y * s, a.z * s); }
static inline v3 vmul(v3 a, v3 b) { return V(a.x * b.x, a.y * b.y, a.z * b.z); }
static inline float dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float len(v3 a) { return sqrtf(dot(a, a)); }
static inline v3 nrm(v3 a) { return mul(a, 1.0f / (len(a) + 1e-12f)); }
static inline v3 cross(v3 a, v3 b) { return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
static inline v3 vmix(v3 a, v3 b, float t) { return add(a, mul(sub(b, a), t)); }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float fractf(float x) { return x - floorf(x); }
static inline float hash3(int x, int y, int z) {
  unsigned h = (unsigned)x * 73856093u ^ (unsigned)y * 19349663u ^ (unsigned)z * 83492791u;
  h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
  return (h & 0xffffff) / 16777216.0f;
}

// ---------------- materials ----------------
enum {
  M_GROUND, M_BLACK, M_CRUST, M_FROST, M_SPRINK, M_GRAY, M_WHITE, M_CHEEK,
  M_R1, M_R2, M_R3, M_R4, M_R5, M_R6, M_FRAME, M_SCREEN, M_IVORY, M_IVORY2,
  M_SLOT, M_CATG, M_STRIPE, M_BELLY, M_GALAXY, M_STAR, M_SCREW, M_EARPINK, M_NMAT
};
typedef struct { unsigned hex; float spec, shin, layer, sparkle; } Mat;
static Mat MATS[M_NMAT] = {
  [M_GROUND] = {0xD9D5CE, 0.02f, 8, 0, 0},
  [M_BLACK] = {0x222226, 0.10f, 40, 1, 0},
  [M_CRUST] = {0xF0BF8E, 0.06f, 30, 1, 0},
  [M_FROST] = {0xF58AD6, 0.07f, 30, 1, 0},
  [M_SPRINK] = {0xE02E86, 0.07f, 30, 1, 0},
  [M_GRAY] = {0x8C8F96, 0.07f, 30, 1, 0},
  [M_WHITE] = {0xF1F1EC, 0.06f, 30, 1, 0},
  [M_CHEEK] = {0xF0908F, 0.06f, 30, 1, 0},
  [M_R1] = {0xE3202B, 0.07f, 30, 1, 0},
  [M_R2] = {0xF5901E, 0.07f, 30, 1, 0},
  [M_R3] = {0xF6DC1E, 0.07f, 30, 1, 0},
  [M_R4] = {0x3AC63A, 0.07f, 30, 1, 0},
  [M_R5] = {0x1E8EE6, 0.07f, 30, 1, 0},
  [M_R6] = {0x6A3ED6, 0.07f, 30, 1, 0},
  [M_FRAME] = {0x0C0C0E, 0.30f, 120, 0, 0},
  [M_SCREEN] = {0x000000, 0, 0, 0, 0},
  [M_IVORY] = {0xE9DFC7, 0.06f, 28, 1, 0},
  [M_IVORY2] = {0xD2C6A9, 0.06f, 28, 1, 0},
  [M_SLOT] = {0x26262A, 0.05f, 20, 1, 0},
  [M_CATG] = {0x8A8E96, 0.06f, 26, 1, 0},
  [M_STRIPE] = {0x5A5E66, 0.06f, 26, 1, 0},
  [M_BELLY] = {0xE6E3DD, 0.06f, 26, 1, 0},
  [M_GALAXY] = {0x17181F, 0.12f, 45, 1, 1},
  [M_STAR] = {0xF4F3EC, 0.07f, 30, 1, 0},
  [M_SCREW] = {0x8E949B, 0.55f, 90, 0, 0},
  [M_EARPINK] = {0xF1A3B8, 0.06f, 28, 1, 0},
};
static v3 srgb2lin(unsigned hex) {
  float c[3] = {((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f};
  for (int i = 0; i < 3; i++) c[i] = c[i] <= 0.04045f ? c[i] / 12.92f : powf((c[i] + 0.055f) / 1.055f, 2.4f);
  return V(c[0], c[1], c[2]);
}

// ---------------- primitives ----------------
static inline float sdBox(v3 p, v3 b) {
  v3 q = V(fabsf(p.x) - b.x, fabsf(p.y) - b.y, fabsf(p.z) - b.z);
  float o = len(V(maxf(q.x, 0), maxf(q.y, 0), maxf(q.z, 0)));
  return o + minf(maxf(q.x, maxf(q.y, q.z)), 0);
}
static inline float sdRBox(v3 p, v3 b, float r) { return sdBox(p, sub(b, V(r, r, r))) - r; }
static inline float sdRect2(float x, float y, float bx, float by, float r) {
  float qx = fabsf(x) - bx + r, qy = fabsf(y) - by + r;
  return hypotf(maxf(qx, 0), maxf(qy, 0)) + minf(maxf(qx, qy), 0) - r;
}
static inline float extrude(float d2, float z, float hz) {
  float wx = d2, wy = fabsf(z) - hz;
  return minf(maxf(wx, wy), 0) + hypotf(maxf(wx, 0), maxf(wy, 0));
}
static inline float smin(float a, float b, float k) {
  float h = clampf(0.5f + 0.5f * (b - a) / k, 0, 1);
  return mixf(b, a, h) - k * h * (1 - h);
}
static inline float smax(float a, float b, float k) { return -smin(-a, -b, k); }
static inline float sdEllipsoid(v3 p, v3 r) {
  float k0 = len(V(p.x / r.x, p.y / r.y, p.z / r.z));
  float k1 = len(V(p.x / (r.x * r.x), p.y / (r.y * r.y), p.z / (r.z * r.z)));
  return k0 * (k0 - 1.0f) / (k1 + 1e-9f);
}
static inline float sdCapsule(v3 p, v3 a, v3 b, float r) {
  v3 pa = sub(p, a), ba = sub(b, a);
  float h = clampf(dot(pa, ba) / dot(ba, ba), 0, 1);
  return len(sub(pa, mul(ba, h))) - r;
}
static inline float sdCyl(v3 p, float r, float h) {  // axis z
  float dx = hypotf(p.x, p.y) - r, dz = fabsf(p.z) - h;
  return minf(maxf(dx, dz), 0) + hypotf(maxf(dx, 0), maxf(dz, 0));
}
// 2D triangle (iq)
static float sdTri2(float px, float py, float ax, float ay, float bx, float by, float cx, float cy) {
  float e0x = bx - ax, e0y = by - ay, e1x = cx - bx, e1y = cy - by, e2x = ax - cx, e2y = ay - cy;
  float v0x = px - ax, v0y = py - ay, v1x = px - bx, v1y = py - by, v2x = px - cx, v2y = py - cy;
  float h0 = clampf((v0x * e0x + v0y * e0y) / (e0x * e0x + e0y * e0y), 0, 1);
  float h1 = clampf((v1x * e1x + v1y * e1y) / (e1x * e1x + e1y * e1y), 0, 1);
  float h2 = clampf((v2x * e2x + v2y * e2y) / (e2x * e2x + e2y * e2y), 0, 1);
  float pq0x = v0x - e0x * h0, pq0y = v0y - e0y * h0;
  float pq1x = v1x - e1x * h1, pq1y = v1y - e1y * h1;
  float pq2x = v2x - e2x * h2, pq2y = v2y - e2y * h2;
  float s = (e0x * e2y - e0y * e2x) > 0 ? 1.f : -1.f;
  float d0 = pq0x * pq0x + pq0y * pq0y, s0 = s * (v0x * e0y - v0y * e0x);
  float d1 = pq1x * pq1x + pq1y * pq1y, s1 = s * (v1x * e1y - v1y * e1x);
  float d2 = pq2x * pq2x + pq2y * pq2y, s2 = s * (v2x * e2y - v2y * e2x);
  float d = minf(d0, minf(d1, d2));
  float sg = minf(s0, minf(s1, s2));
  return -sqrtf(d) * (sg > 0 ? 1.f : -1.f);
}

// elliptic arc tube in the xz plane (approximate distance)
static float sdArc(v3 p, v3 c, float rx, float rz, float a0, float a1, float r) {
  v3 q = sub(p, c);
  float a = atan2f(q.z / rz, q.x / rx);
  if (a < a0 || a > a1) {
    float m = 0.5f * (a0 + a1);
    float da = a - m;
    while (da > (float)M_PI) da -= 2 * (float)M_PI;
    while (da < -(float)M_PI) da += 2 * (float)M_PI;
    a = clampf(m + da, a0, a1);
  }
  v3 k = V(rx * cosf(a), 0, rz * sinf(a));
  return len(sub(q, k)) - r;
}

// ---------------- module (Waveshare ESP32-S3-Touch-LCD-1.47) ----------------
typedef struct { v3 o, ax, ay, az; float usbSide; } Frame;
static Frame MOD;
static inline v3 toLocal(Frame f, v3 p) { v3 d = sub(p, f.o); return V(dot(d, f.ax), dot(d, f.ay), dot(d, f.az)); }
static float sdModule(v3 p, int *m) {
  v3 q = toLocal(MOD, p);
  float d = extrude(sdRect2(q.x, q.y, 22.25f, 12.275f, 5.75f), q.z - 3.3f, 3.3f);
  *m = (q.z < 0.08f) ? M_SCREEN : M_FRAME;
  return d;
}
// window cutter in module space: rounded rect opening with a 45deg chamfer towards the front
static float sdWindow(v3 p, float hw, float hh, float r, float chamferStart) {
  v3 q = toLocal(MOD, p);
  float grow = maxf(0, -(q.z - chamferStart));
  return maxf(sdRect2(q.x, q.y, hw + grow, hh + grow, r + grow) * 0.7071f, q.z - 0.3f);
}

// ---------------- scene globals ----------------
static char SCENE = 'A';
static float YAW = 0, OBJ_H = 40;
static v3 LAYER_AX;          // print up axis in object space
static float LAYER_H = 0.2f;
static float BACKDROP_Z = 110;

#define U(dist, mat) do { float _d = (dist); if (_d < d) { d = _d; m = (mat); } } while (0)

// ---------------- scene A: pixel nyan ----------------
static char GRID[18][44];
static const float PX = 2.4f;
static const float GCX = 21.5f;
typedef struct { float zf, zb; int mat; int pop; } Cell;
static Cell cellOf(char c) {
  Cell r = {0, 0, -1, 0};
  switch (c) {
    case 'K': r = (Cell){0.0f, 14, M_BLACK, 1}; break;
    case 'C': r = (Cell){-0.8f, 14, M_CRUST, 1}; break;
    case 'P': r = (Cell){-1.6f, 14, M_FROST, 1}; break;
    case 'M': r = (Cell){-2.2f, 14, M_SPRINK, 1}; break;
    case 'k': r = (Cell){-3.0f, 11, M_BLACK, 0}; break;
    case 'G': r = (Cell){-3.6f, 11, M_GRAY, 0}; break;
    case 'g': r = (Cell){-2.2f, 9, M_GRAY, 0}; break;
    case 'W': r = (Cell){-4.2f, 11, M_WHITE, 0}; break;
    case 'R': r = (Cell){-4.0f, 11, M_CHEEK, 0}; break;
    case '1': r = (Cell){1.0f, 7, M_R1, 0}; break;
    case '2': r = (Cell){1.0f, 7, M_R2, 0}; break;
    case '3': r = (Cell){1.0f, 7, M_R3, 0}; break;
    case '4': r = (Cell){1.0f, 7, M_R4, 0}; break;
    case '5': r = (Cell){1.0f, 7, M_R5, 0}; break;
    case '6': r = (Cell){1.0f, 7, M_R6, 0}; break;
  }
  return r;
}
static float sceneA(v3 p, int *mo) {
  float d = 1e9f; int m = -1;
  // bounding
  float bb = sdBox(sub(p, V(0, 21.6f, 5)), V(54, 23, 11));
  if (bb > 2) {
    d = bb;
    m = M_BLACK;
  } else {
    int c0 = (int)floorf(p.x / PX + GCX), r0 = (int)floorf(18 - p.y / PX);
    float dpop = 1e9f, doth = 1e9f; int mp = -1, mo2 = -1;
    for (int r = r0 - 2; r <= r0 + 2; r++) {
      if (r < 0 || r >= 18) continue;
      for (int c = c0 - 2; c <= c0 + 2; c++) {
        if (c < 0 || c >= 43) continue;
        Cell ce = cellOf(GRID[r][c]);
        if (ce.mat < 0) continue;
        v3 cen = V((c - GCX + 0.5f) * PX, (17 - r + 0.5f) * PX, 0.5f * (ce.zf + ce.zb));
        float bd = sdBox(sub(p, cen), V(PX * 0.5f, PX * 0.5f, 0.5f * (ce.zb - ce.zf)));
        if (ce.pop) { if (bd < dpop) { dpop = bd; mp = ce.mat; } }
        else { if (bd < doth) { doth = bd; mo2 = ce.mat; } }
      }
    }
    float bound = 1.5f * PX;
    if (dpop > bound) dpop = bound;
    if (doth > bound) doth = bound;
    // screen window through the pop-tart
    float w = sdWindow(p, 20.0f, 10.75f, 3.0f, -0.6f);
    dpop = maxf(dpop, -w);
    // back cover recess + screws on the pop-tart back (z = 14)
    {
      v3 q = sub(p, V(-3.6f, 25.2f, 14.0f));
      float groove = extrude(fabsf(sdRect2(q.x, q.y, 25, 14, 3)) - 0.35f, q.z, 0.5f);
      dpop = maxf(dpop, -groove);
      for (int i = 0; i < 4; i++) {
        float sx = (i & 1) ? 19.5f : -19.5f, sy = (i & 2) ? 8.5f : -8.5f;
        v3 s = sub(q, V(sx, sy, 0));
        float hole = sdCyl(s, 2.1f, 1.2f);
        dpop = maxf(dpop, -hole);
        float head = sdCyl(sub(s, V(0, 0, 0.9f)), 1.8f, 0.45f);
        if (head < d) { d = head; m = M_SCREW; }
      }
    }
    if (dpop < d) { d = dpop; m = (mp < 0 || p.z > 11.5f) ? M_BLACK : mp; }
    if (doth < d) { d = doth; m = mo2 < 0 ? M_BLACK : mo2; }
  }
  // kickstand (printed separately, clipped into the back)
  {
    v3 a = V(-3.6f, 30, 14), b = V(-3.6f, 0.9f, 42);
    v3 pa = sub(p, a), ba = sub(b, a);
    float h = clampf(dot(pa, ba) / dot(ba, ba), 0, 1);
    v3 c = sub(pa, mul(ba, h));
    float ks = sdBox(V(c.x, c.y, c.z), V(14, 0.9f, 0.9f));
    ks = maxf(ks, -p.y);
    U(ks - 0.6f, M_BLACK);
  }
  int mm;
  U(sdModule(p, &mm), mm);
  *mo = m;
  return d;
}

// ---------------- scene B: retro terminal ----------------
static const float TB = 0.105f;  // tan(6deg) front tilt
static float sceneB(v3 p, int *mo) {
  float d = 1e9f; int m = M_IVORY;
  v3 q = p; q.z -= q.y * TB;  // shear: front plane becomes vertical
  float bb = sdBox(sub(p, V(0, 38, 12)), V(42, 40, 42));
  if (bb > 3) { *mo = M_IVORY; return bb; }
  // main shell: front block + tapered rear
  float front = sdRBox(sub(q, V(0, 31, -6)), V(32, 31, 18), 3.0f);
  v3 r = sub(p, V(0, 31, 16));
  float taper = 1.0f - clampf((p.z - 4) / 40.0f, 0, 1) * 0.18f;
  float rear = sdRBox(V(r.x / taper, (r.y - 2) / taper, r.z), V(30, 27, 14), 4.0f) * taper;
  float body = smin(front, rear, 2.0f);
  // chin groove
  {
    float g = sdBox(sub(q, V(0, 24, -24)), V(33, 0.35f, 0.6f));
    body = maxf(body, -g);
  }
  // screen bezel recess
  {
    v3 b = sub(q, V(0, 41, -24));
    float rec = extrude(sdRect2(b.x, b.y, 27, 16.5f, 4.0f), b.z, 1.2f);
    body = maxf(body, -rec);
    float bez = extrude(sdRect2(b.x, b.y, 27.0f, 16.5f, 4.0f), b.z - 0.8f, 0.45f);
    bez = maxf(bez, -sdWindow(p, 20.0f, 10.75f, 3.0f, -0.4f));
    U(bez, M_IVORY2);
  }
  float w = sdWindow(p, 20.0f, 10.75f, 3.0f, -0.4f);
  body = maxf(body, -w);
  // floppy slot
  {
    v3 s = sub(q, V(12, 14, -24));
    float sl = extrude(sdRect2(s.x, s.y, 9.5f, 0.9f, 0.9f), s.z, 2.0f);
    body = maxf(body, -sl);
    U(extrude(sdRect2(s.x, s.y, 9.5f, 0.9f, 0.9f), s.z - 1.6f, 0.4f), M_SLOT);
  }
  // rear vents (horizontal slots on both sides of the taper)
  for (int i = 0; i < 6; i++) {
    float y = 20 + i * 4.2f;
    float v = sdRBox(sub(p, V(0, y, 22)), V(40, 0.8f, 6), 0.7f);
    body = maxf(body, -v);
  }
  // usb-c opening on the right side
  {
    float u = sdRBox(sub(p, V(31.5f, 41, -14)), V(6, 2.0f, 5.2f), 1.8f);
    body = maxf(body, -u);
  }
  U(body, M_IVORY);
  // rainbow badge (flush multicolor inlay, slightly proud)
  {
    v3 s = sub(q, V(-20, 14, -24.15f));
    float badge = extrude(sdRect2(s.x, s.y, 4.2f, 3.6f, 1.6f), s.z, 0.25f);
    int k = (int)floorf((3.6f - s.y) / 1.2f);
    k = k < 0 ? 0 : k > 5 ? 5 : k;
    U(badge, M_R1 + k);
  }
  // ears
  for (int sgn = -1; sgn <= 1; sgn += 2) {
    v3 e = sub(q, V(sgn * 20.0f, 61, -10));
    float t = sdTri2(e.x * sgn, e.y, -10, 0, 9, 0, 4, 15);
    float ear = extrude(t, e.z, 4.5f) - 1.2f;
    U(ear, M_IVORY);
    float t2 = sdTri2(e.x * sgn, e.y, -6, 1.5f, 6.5f, 1.5f, 3.2f, 10.5f);
    float inner = extrude(t2, e.z + 5.5f, 0.35f) - 0.2f;
    U(inner, M_EARPINK);
  }
  // paws over the front edge
  for (int sgn = -1; sgn <= 1; sgn += 2) {
    v3 a = sub(q, V(sgn * 11.0f, 4.2f, -25.5f));
    float paw = sdEllipsoid(a, V(6, 4.2f, 5));
    paw = maxf(paw, -p.y);
    U(paw, M_IVORY);
    for (int t = -1; t <= 1; t++) {
      float toe = sdBox(sub(a, V(t * 2.6f, 0.5f, -4.6f)), V(0.25f, 2.5f, 0.6f));
      if (toe < 0.4f) d = maxf(d, -toe);
    }
  }
  // tail cable curling on the desk behind
  {
    float tl = sdArc(p, V(14, 2.4f, 40), 10, 10, -1.5708f, 3.6f, 2.4f);
    tl = minf(tl, sdCapsule(p, V(14, 2.4f, 30), V(14, 12, 26), 2.4f));
    tl = maxf(tl, -p.y);
    U(tl, M_CATG);
  }
  int mm;
  U(sdModule(p, &mm), mm);
  *mo = m;
  return d;
}

// ---------------- scene C: chubby cat ----------------
static float sceneC(v3 p, int *mo) {
  float d = 1e9f; int m = M_CATG;
  float bb = sdBox(sub(p, V(0, 40, 0)), V(56, 44, 40));
  if (bb > 3) { *mo = M_CATG; return bb; }
  float body = sdRBox(sub(p, V(0, 31, 0)), V(36, 31, 23), 15.0f);
  float belly = sdEllipsoid(sub(p, V(0, 18, -2)), V(37, 20, 25));
  body = smin(body, belly, 6.0f);
  // flatten the face so the screen sits in a plane
  body = smax(body, -(p.z + 22.0f), 3.0f);
  // ears
  for (int sgn = -1; sgn <= 1; sgn += 2) {
    v3 e = sub(p, V(sgn * 22.0f, 58, 0));
    float t = sdTri2(e.x * sgn, e.y, -11, 0, 10, 0, 6, 20);
    float ear = extrude(t, e.z, 6.5f) - 2.5f;
    body = smin(body, ear, 4.0f);
  }
  // whisker grooves
  for (int sgn = -1; sgn <= 1; sgn += 2)
    for (int i = 0; i < 3; i++) {
      v3 a = V(sgn * 26.0f, 30.0f - i * 2.6f + 1.5f, -21.5f), b = V(sgn * 34.0f, 30.0f - i * 3.6f + 2.5f, -15.9f);
      float g = sdCapsule(p, a, b, 0.45f);
      body = maxf(body, -g);
    }
  float w = sdWindow(p, 20.0f, 10.75f, 3.0f, -0.6f);
  body = maxf(body, -w);
  // usb on the back
  {
    float u = sdRBox(sub(p, V(0, 8, 23)), V(5.2f, 2.2f, 4), 1.8f);
    body = maxf(body, -u);
  }
  // material zones on the body
  int bm = M_CATG;
  {
    float bel = sdEllipsoid(sub(p, V(0, 15, -14)), V(22, 13, 14));
    if (bel < 0) bm = M_BELLY;
    if (p.y > 52) {
      float sx = fabsf(p.x);
      if ((sx < 1.6f || fabsf(sx - 6.5f) < 1.4f) && p.z < 8 && p.y > 54 + sx * 0.25f) bm = M_STRIPE;
    }
    // cheeks
    for (int sgn = -1; sgn <= 1; sgn += 2) {
      float ch = len(sub(p, V(sgn * 27.0f, 29, -21)));
      if (ch < 4.2f) bm = M_CHEEK;
    }
    // inner ears
    for (int sgn = -1; sgn <= 1; sgn += 2) {
      v3 e = sub(p, V(sgn * 22.0f, 58, 0));
      float t = sdTri2(e.x * sgn, e.y, -6, 3, 6, 3, 4, 14);
      if (t < 0 && p.z < -3) bm = M_EARPINK;
    }
  }
  U(body, bm);
  // screen bezel ring (black, separate part)
  {
    v3 q = toLocal(MOD, p);
    float ring = extrude(fabsf(sdRect2(q.x, q.y, 22.6f, 13.3f, 4.2f)) - 1.1f, q.z + 0.3f, 0.9f) - 0.2f;
    U(ring, M_BLACK);
  }
  // front paws
  for (int sgn = -1; sgn <= 1; sgn += 2) {
    float paw = sdEllipsoid(sub(p, V(sgn * 13.0f, 4.5f, -24)), V(8, 5.5f, 7));
    paw = maxf(paw, -p.y);
    U(paw, M_BELLY);
  }
  // tail curling around the right side, from the back to the front paw
  {
    v3 c = V(2, 3.4f, 2);
    float tl = sdArc(p, c, 40, 29, -1.25f, 1.9f, 3.4f);
    tl = maxf(tl, -p.y);
    float ang = atan2f((p.z - c.z) / 29, (p.x - c.x) / 40);
    int tm = (fractf(ang * 2.2f) < 0.28f) ? M_STRIPE : M_CATG;
    U(tl, tm);
  }
  int mm;
  U(sdModule(p, &mm), mm);
  *mo = m;
  return d;
}

// ---------------- scene D: galaxy wedge ----------------
static float sdWedge2(float z, float y) {
  // convex polygon in (z,y): front-bottom, back-bottom, back-top, top-front, rounded
  float d = -1e9f;
  float pz[4] = {-22, 22, 22, 4}, py[4] = {0, 0, 36, 44};
  for (int i = 0; i < 4; i++) {
    int j = (i + 1) % 4;
    float ex = pz[j] - pz[i], ey = py[j] - py[i];
    float l = hypotf(ex, ey);
    float nx = ey / l, ny = -ex / l;  // outward for clockwise? check sign below
    float s = (z - pz[i]) * nx + (y - py[i]) * ny;
    d = maxf(d, s);
  }
  return d;
}
static v3 D_FACE_O, D_FACE_U, D_FACE_N;  // front face frame
static float sceneD(v3 p, int *mo) {
  float d = 1e9f; int m = M_GALAXY;
  float bb = sdBox(sub(p, V(0, 26, 0)), V(42, 30, 30));
  if (bb > 3) { *mo = M_GALAXY; return bb; }
  float w2 = sdWedge2(p.z, p.y);
  float body = extrude(w2 + 3.0f, p.x, 32.0f) - 3.0f;
  float w = sdWindow(p, 20.0f, 10.75f, 3.0f, -0.6f);
  body = maxf(body, -w);
  // usb at the back
  body = maxf(body, -sdRBox(sub(p, V(0, 10, 25)), V(5.2f, 2.2f, 5), 1.8f));
  // rubber feet recesses not visible; decor zones
  int bm = M_GALAXY;
  {
    // coordinates on the front face
    v3 r = sub(p, D_FACE_O);
    float fu = dot(r, D_FACE_U), fn = dot(r, D_FACE_N);
    // rainbow trail: on the left side panel and onto the face up to the screen
    float band = fu;
    if (fn > -1.2f && p.x < -20.5f && fabsf(band) < 7.2f) {
      int k = (int)floorf((7.2f - band) / 2.4f);
      bm = M_R1 + (k < 0 ? 0 : k > 5 ? 5 : k);
    }
    if (p.x < -34.6f) {
      // side panel: stripes running along z, following the face height
      float yy = p.y - 22.0f + (p.z + 4) * 0.0f;
      if (fabsf(yy) < 7.2f && p.z < 14 && p.z > -14 + (p.y) * -0.0f) {
        float wave = (fmodf(fabsf(p.z + 40), 8.0f) < 4.0f) ? 1.2f : 0.0f;
        float y2 = yy + wave;
        if (fabsf(y2) < 7.2f) {
          int k = (int)floorf((7.2f - y2) / 2.4f);
          bm = M_R1 + (k < 0 ? 0 : k > 5 ? 5 : k);
        }
      }
    }
    // stars (inlaid) on the face around the screen
    static const float S[][3] = {{26, 14, 1.3f}, {29, -8, 0.8f}, {23, -15, 0.6f}, {-27, 17, 0.7f},
                                 {12, 18, 0.6f}, {-10, -17, 0.8f}, {17, -18, 1.1f}, {30, 4, 0.5f}};
    if (fn > -1.2f)
      for (unsigned i = 0; i < sizeof(S) / sizeof(S[0]); i++) {
        float dx = fabsf(p.x - S[i][0]), dy = fabsf(fu - S[i][1]);
        float sz = S[i][2] * 2.2f;
        float star = minf(dx * 3.2f + dy, dy * 3.2f + dx);  // 4-point sparkle
        if (star < sz || hypotf(dx, dy) < S[i][2] * 0.7f) bm = M_STAR;
      }
  }
  U(body, bm);
  // little ears on the top edge
  for (int sgn = -1; sgn <= 1; sgn += 2) {
    v3 e = sub(p, V(sgn * 24.0f, 43.0f, 6.5f));
    float t = sdTri2(e.x * sgn, e.y, -7, -3, 6, -3, 3, 8);
    float ear = extrude(t, e.z, 3.2f) - 1.0f;
    U(ear, M_GALAXY);
  }
  int mm;
  U(sdModule(p, &mm), mm);
  *mo = m;
  return d;
}

// ---------------- world ----------------
static inline v3 toObj(v3 p) {
  float c = cosf(YAW), s = sinf(YAW);
  return V(c * p.x - s * p.z, p.y, s * p.x + c * p.z);
}
static inline v3 toWorldDir(v3 n) {
  float c = cosf(YAW), s = sinf(YAW);
  return V(c * n.x + s * n.z, n.y, -s * n.x + c * n.z);
}
static float sdGround(v3 p) {
  float R = 70, zb = BACKDROP_Z;
  if (p.z <= zb - R) return p.y;
  if (p.y >= R) return zb - p.z;
  float qx = p.z - (zb - R), qy = p.y - R;
  return R - hypotf(qx, qy);
}
static float mapObj(v3 po, int *m) {
  switch (SCENE) {
    case 'A': return sceneA(po, m);
    case 'B': return sceneB(po, m);
    case 'C': return sceneC(po, m);
    default: return sceneD(po, m);
  }
}
static float map(v3 p, int *m) {
  int mo;
  float d = mapObj(toObj(p), &mo) * 0.8f;
  float g = sdGround(p);
  if (g < d) { *m = M_GROUND; return g; }
  *m = mo;
  return d;
}
static v3 normalAt(v3 p) {
  const float e = 0.012f;
  int m;
  v3 k1 = V(1, -1, -1), k2 = V(-1, -1, 1), k3 = V(-1, 1, -1), k4 = V(1, 1, 1);
  v3 n = add(add(mul(k1, map(add(p, mul(k1, e)), &m)), mul(k2, map(add(p, mul(k2, e)), &m))),
             add(mul(k3, map(add(p, mul(k3, e)), &m)), mul(k4, map(add(p, mul(k4, e)), &m))));
  return nrm(n);
}
static float softShadow(v3 ro, v3 rd, float k) {
  float res = 1, t = 0.06f + 0.05f * hash3((int)(ro.x * 997), (int)(ro.y * 991), (int)(ro.z * 983)), ph = 1e10f;
  for (int i = 0; i < 160 && t < 220; i++) {
    int m;
    float h = map(add(ro, mul(rd, t)), &m);
    float y = h * h / (2.0f * ph);
    if (y > 0.9f * h) y = 0;
    float dd = sqrtf(maxf(h * h - y * y, 0));
    res = minf(res, k * dd / maxf(0.0f, t - y));
    ph = h;
    if (res < 0.001f) return 0;
    t += clampf(h, 0.03f, 1.8f);
  }
  res = clampf(res, 0, 1);
  return res * res * (3 - 2 * res);
}
static float ambOcc(v3 p, v3 n) {
  float occ = 0, sca = 1;
  for (int i = 1; i <= 6; i++) {
    float h = 0.25f + 1.2f * i;
    int m;
    float dd = map(add(p, mul(n, h)), &m);
    occ += (h - dd) * sca;
    sca *= 0.72f;
  }
  return clampf(1 - 0.09f * occ, 0, 1);
}

static unsigned char *TEX;  // 320x172 RGB
static v3 texAt(float u, float v) {
  int x = (int)clampf(u * 320, 0, 319), y = (int)clampf(v * 172, 0, 171);
  unsigned char *t = TEX + (y * 320 + x) * 3;
  return V(powf(t[0] / 255.f, 2.2f), powf(t[1] / 255.f, 2.2f), powf(t[2] / 255.f, 2.2f));
}

static v3 LKEY, LFILL, LRIM;
static v3 env(v3 rd) {
  float t = clampf(rd.y * 0.5f + 0.5f, 0, 1);
  v3 c = vmix(V(0.55f, 0.53f, 0.50f), V(1.0f, 1.0f, 1.0f), t);
  float sb = dot(rd, LKEY);
  if (sb > 0.93f) c = add(c, mul(V(1, 0.97f, 0.92f), 5.0f * clampf((sb - 0.93f) * 40, 0, 1)));
  return c;
}

static v3 shade(v3 ro, v3 rd) {
  float t = 0;
  int m = -1;
  int hit = 0;
  for (int i = 0; i < 400 && t < 900; i++) {
    float h = map(add(ro, mul(rd, t)), &m);
    if (h < 0.0025f * (1 + t * 0.004f)) { hit = 1; break; }
    t += h;
  }
  if (!hit) return env(rd);
  v3 p = add(ro, mul(rd, t));
  v3 n = normalAt(p);
  v3 po = toObj(p);
  Mat mt = MATS[m];
  v3 base = srgb2lin(mt.hex);
  if (m == M_GROUND) {
    float nz = hash3((int)floorf(p.x * 3), (int)floorf(p.y * 3), (int)floorf(p.z * 3));
    base = mul(base, 0.97f + 0.06f * nz);
  }
  float layerShade = 1;
  if (mt.layer > 0) {
    // object-space normal
    float c = cosf(YAW), s = sinf(YAW);
    v3 no = V(c * n.x - s * n.z, n.y, s * n.x + c * n.z);
    float tt = dot(po, LAYER_AX) / LAYER_H;
    float f = fractf(tt);
    float u = 2 * f - 1;
    float A = 0.022f;
    float db = A * -2.5f * powf(fabsf(u), 1.5f) * (u > 0 ? 1 : -1) * 2 / LAYER_H;
    float na = dot(no, LAYER_AX);
    v3 g = mul(LAYER_AX, db);
    v3 gt = sub(g, mul(no, dot(no, g)));
    float wall = 1 - fabsf(na);
    // tiny per-layer wobble (extrusion inconsistency)
    float wob = (hash3((int)floorf(tt), 0, 7) - 0.5f) * 0.12f * wall;
    no = nrm(sub(no, mul(gt, 1.0f + wob)));
    layerShade = 1 - 0.10f * powf(fabsf(u), 6.0f) * wall;
    // top-surface infill lines (diagonal, 0.42mm pitch)
    if (fabsf(na) > 0.97f) {
      v3 e1 = nrm(cross(LAYER_AX, fabsf(LAYER_AX.y) > 0.9f ? V(1, 0, 0) : V(0, 1, 0)));
      v3 e2 = cross(LAYER_AX, e1);
      int top = (int)floorf(tt);
      v3 dir = (top & 1) ? nrm(add(e1, e2)) : nrm(sub(e1, e2));
      float w2 = dot(po, dir) / 0.42f;
      float fu = 2 * fractf(w2) - 1;
      no = nrm(add(no, mul(dir, 0.08f * fu * (1 - fu * fu))));
      layerShade *= 0.985f + 0.03f * (1 - fu * fu);
    }
    n = toWorldDir(no);
  }
  // slight colour variance in filament
  base = mul(base, layerShade);

  if (m == M_SCREEN) {
    v3 q = toLocal(MOD, po);
    float cx = MOD.usbSide * 1.245f;
    float ax = q.x - cx, ay = q.y;
    v3 col = V(0.004f, 0.004f, 0.005f);
    if (sdRect2(ax, ay, 16.465f, 8.875f, 2.6f) < 0) {
      float u = (ax + 16.465f) / 32.93f, v = (8.875f - ay) / 17.75f;
      col = add(mul(texAt(u, v), 0.85f), V(0.004f, 0.004f, 0.006f));
    }
    v3 r = sub(rd, mul(n, 2 * dot(rd, n)));
    float fr = 0.04f + 0.96f * powf(1 - clampf(-dot(rd, n), 0, 1), 5);
    float sh = softShadow(add(p, mul(n, 0.05f)), r, 16);
    return add(col, mul(env(r), fr * (0.35f + 0.65f * sh)));
  }

  float sk = softShadow(add(p, mul(n, 0.03f)), LKEY, 10);
  float ao = ambOcc(p, n);
  float dk = maxf(dot(n, LKEY), 0), df = maxf(dot(n, LFILL), 0) , dr = maxf(dot(n, LRIM), 0);
  float sky = 0.5f + 0.5f * n.y;
  v3 lin = V(0, 0, 0);
  lin = add(lin, mul(V(1.0f, 0.95f, 0.88f), 2.4f * dk * sk));
  lin = add(lin, mul(V(0.75f, 0.82f, 0.95f), 0.45f * df * (0.5f + 0.5f * ao)));
  lin = add(lin, mul(V(0.9f, 0.92f, 1.0f), 0.42f * sky * ao));
  lin = add(lin, mul(V(1, 1, 1), 0.55f * dr * ao));
  lin = add(lin, mul(V(0.6f, 0.55f, 0.5f), 0.12f * maxf(-n.y, 0) * ao));  // bounce
  v3 col = vmul(base, lin);
  // specular
  v3 h = nrm(sub(LKEY, rd));
  float spk = powf(maxf(dot(n, h), 0), mt.shin) * sk * dk;
  v3 rr = sub(rd, mul(n, 2 * dot(rd, n)));
  float fr = 0.04f + 0.96f * powf(1 - clampf(-dot(rd, n), 0, 1), 5);
  col = add(col, mul(V(1, 0.97f, 0.92f), mt.spec * spk * 3.0f));
  col = add(col, mul(env(rr), mt.spec * 0.6f * fr * ao));
  if (m == M_FRAME || m == M_SCREW) col = add(col, mul(env(rr), mt.spec * 0.25f * ao));
  if (mt.sparkle > 0) {
    int gx = (int)floorf(po.x * 9), gy = (int)floorf(po.y * 9), gz = (int)floorf(po.z * 9);
    float hsh = hash3(gx, gy, gz);
    if (hsh < 0.03f) {
      float tw = hash3(gx + 11, gy - 3, gz + 5);
      float vis = powf(maxf(0, dot(nrm(V(tw - 0.5f, 0.8f, -0.5f)), h)), 2.0f);
      col = add(col, mul(V(0.9f, 0.9f, 1.0f), (0.02f + 0.9f * vis * vis) * (0.3f + 0.7f * sk)));
    }
  }
  return col;
}

int main(int argc, char **argv) {
  if (argc < 9) { fprintf(stderr, "usage\n"); return 1; }
  SCENE = argv[1][0];
  YAW = atof(argv[2]) * (float)M_PI / 180;
  float elev = atof(argv[3]) * (float)M_PI / 180;
  int W = atoi(argv[4]), H = atoi(argv[5]), SS = atoi(argv[6]);
  FILE *tf = fopen(argv[7], "rb");
  TEX = malloc(320 * 172 * 3);
  if (!tf || fread(TEX, 1, 320 * 172 * 3, tf) != 320 * 172 * 3) { fprintf(stderr, "tex\n"); return 1; }
  fclose(tf);

  float fov = 24, dist = 330, ty = 22;
  LAYER_AX = V(0, 1, 0);
  // module frame defaults (front facing -z)
  MOD.ax = V(1, 0, 0); MOD.ay = V(0, 1, 0); MOD.az = V(0, 0, 1); MOD.usbSide = 1;
  if (SCENE == 'A') {
    FILE *g = fopen("grid_a.txt", "r");
    for (int r = 0; r < 18; r++) { if (!fgets(GRID[r], 44, g)) break; fgetc(g); }
    fclose(g);
    LAYER_AX = V(0, 0, -1);
    MOD.o = V(-3.6f, 25.2f, 0.6f);
    MOD.usbSide = -1;
    dist = 250; ty = 20; OBJ_H = 43;
  } else if (SCENE == 'B') {
    float a = atanf(TB);
    MOD.ay = V(0, cosf(a), sinf(a));
    MOD.az = V(0, -sinf(a), cosf(a));
    MOD.o = V(0, 41, -24 + 41 * TB + 1.6f);
    dist = 255; ty = 36;
  } else if (SCENE == 'C') {
    MOD.o = V(0, 38, -21.2f);
    dist = 265; ty = 36;
  } else {
    // front face from (z=-22,y=0) to (z=4,y=44)
    v3 a = V(0, 0, -22), b = V(0, 44, 4);
    D_FACE_U = nrm(sub(b, a));
    D_FACE_N = nrm(V(0, 26, -44));
    v3 c = mul(add(a, b), 0.5f);
    c = add(c, mul(D_FACE_U, 1.5f));
    D_FACE_O = c;
    MOD.ay = D_FACE_U;
    MOD.az = mul(D_FACE_N, -1);
    MOD.ax = cross(MOD.ay, MOD.az);
    if (MOD.ax.x < 0) MOD.ax = mul(MOD.ax, -1);
    MOD.o = add(c, mul(MOD.az, 1.6f));
    dist = 225; ty = 22;
  }
  LKEY = nrm(V(-0.55f, 0.95f, -0.55f));
  LFILL = nrm(V(0.8f, 0.35f, -0.4f));
  LRIM = nrm(V(0.3f, 0.6f, 1.0f));

  v3 target = V(0, ty, 0);
  v3 ro = add(target, V(0, sinf(elev) * dist, -cosf(elev) * dist));
  v3 fw = nrm(sub(target, ro));
  v3 rt = nrm(cross(V(0, 1, 0), fw));
  v3 up = cross(fw, rt);
  float tanh_ = tanf(fov * (float)M_PI / 360);
  float *out = calloc((size_t)W * H * 3, sizeof(float));
  dispatch_apply((size_t)H, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^(size_t yy) {
    int y = (int)yy;
    for (int x = 0; x < W; x++) {
      v3 acc = V(0, 0, 0);
      for (int sy = 0; sy < SS; sy++)
        for (int sx = 0; sx < SS; sx++) {
          float jx = (sx + 0.5f) / SS + (hash3(x, y, sx * 7 + sy) - 0.5f) / SS * 0.8f;
          float jy = (sy + 0.5f) / SS + (hash3(y, x, sy * 5 + sx + 3) - 0.5f) / SS * 0.8f;
          float u = ((x + jx) / W * 2 - 1) * tanh_ * W / H;
          float v = (1 - (y + jy) / H * 2) * tanh_;
          v3 rd = nrm(add(fw, add(mul(rt, u), mul(up, v))));
          acc = add(acc, shade(ro, rd));
        }
      acc = mul(acc, 1.0f / (SS * SS));
      float *o = out + ((size_t)y * W + x) * 3;
      o[0] = acc.x; o[1] = acc.y; o[2] = acc.z;
    }
  });
  FILE *f = fopen(argv[8], "wb");
  fwrite(out, sizeof(float), (size_t)W * H * 3, f);
  fclose(f);
  return 0;
}
