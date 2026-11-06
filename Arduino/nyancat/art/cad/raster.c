// Minimal z-buffer STL rasterizer for engineering previews.
// usage: raster scene.txt W H yaw elev dist tx ty tz out.ppm [clip_axis clip_val]
// scene line: path r g b dx dy dz   (binary STL, colour 0..1, offset)
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { float x, y, z; } v3;
static v3 V(float x, float y, float z) { v3 r = {x, y, z}; return r; }
static v3 sub(v3 a, v3 b) { return V(a.x - b.x, a.y - b.y, a.z - b.z); }
static float dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static v3 cross(v3 a, v3 b) { return V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static v3 nrm(v3 a) { float l = sqrtf(dot(a, a)) + 1e-12f; return V(a.x / l, a.y / l, a.z / l); }
int W, H; float *zb; float *cb; float *nb;
v3 cam, fw, rt, up; float foc;
int clipAx = -1; float clipV = 0;
static int proj(v3 p, float *sx, float *sy, float *d) {
  v3 q = sub(p, cam);
  float z = dot(q, fw);
  if (z < 1) return 0;
  *sx = W * 0.5f + foc * dot(q, rt) / z;
  *sy = H * 0.5f - foc * dot(q, up) / z;
  *d = z;
  return 1;
}
static void tri(v3 a, v3 b, v3 c, v3 col) {
  if (clipAx >= 0) {
    float ca = clipAx == 0 ? a.x : clipAx == 1 ? a.y : a.z;
    float cbv = clipAx == 0 ? b.x : clipAx == 1 ? b.y : b.z;
    float cc = clipAx == 0 ? c.x : clipAx == 1 ? c.y : c.z;
    if (ca > clipV && cbv > clipV && cc > clipV) return;
  }
  v3 n = nrm(cross(sub(b, a), sub(c, a)));
  float x0, y0, d0, x1, y1, d1, x2, y2, d2;
  if (!proj(a, &x0, &y0, &d0) || !proj(b, &x1, &y1, &d1) || !proj(c, &x2, &y2, &d2)) return;
  float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
  if (fabsf(area) < 1e-9f) return;
  int mnx = (int)fmaxf(0, floorf(fminf(x0, fminf(x1, x2)))), mxx = (int)fminf(W - 1, ceilf(fmaxf(x0, fmaxf(x1, x2))));
  int mny = (int)fmaxf(0, floorf(fminf(y0, fminf(y1, y2)))), mxy = (int)fminf(H - 1, ceilf(fmaxf(y0, fmaxf(y1, y2))));
  for (int y = mny; y <= mxy; y++)
    for (int x = mnx; x <= mxx; x++) {
      float px = x + 0.5f, py = y + 0.5f;
      float w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area;
      float w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area;
      float w2 = 1 - w0 - w1;
      if (w0 < 0 || w1 < 0 || w2 < 0) continue;
      float iz = w0 / d0 + w1 / d1 + w2 / d2;
      float z = 1 / iz;
      int i = y * W + x;
      if (z < zb[i]) {
        zb[i] = z;
        cb[i * 3] = col.x; cb[i * 3 + 1] = col.y; cb[i * 3 + 2] = col.z;
        nb[i * 3] = n.x; nb[i * 3 + 1] = n.y; nb[i * 3 + 2] = n.z;
      }
    }
}
int main(int argc, char **argv) {
  W = atoi(argv[2]); H = atoi(argv[3]);
  float yaw = atof(argv[4]) * M_PI / 180, el = atof(argv[5]) * M_PI / 180, dist = atof(argv[6]);
  v3 tg = V(atof(argv[7]), atof(argv[8]), atof(argv[9]));
  if (argc > 12) { clipAx = atoi(argv[11]); clipV = atof(argv[12]); }
  // camera in front (-z) of the object, rotated by yaw around y
  cam = V(tg.x + dist * cosf(el) * sinf(yaw), tg.y + dist * sinf(el), tg.z - dist * cosf(el) * cosf(yaw));
  fw = nrm(sub(tg, cam)); rt = nrm(cross(V(0, 1, 0), fw)); up = cross(fw, rt);
  foc = W * 1.6f;
  zb = malloc(sizeof(float) * W * H); cb = calloc(W * H * 3, 4); nb = calloc(W * H * 3, 4);
  for (int i = 0; i < W * H; i++) zb[i] = 1e30f;
  FILE *sc = fopen(argv[1], "r");
  char path[1024]; float r, g, b, dx, dy, dz;
  while (fscanf(sc, "%1023s %f %f %f %f %f %f", path, &r, &g, &b, &dx, &dy, &dz) == 7) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "missing %s\n", path); continue; }
    fseek(f, 80, SEEK_SET);
    unsigned n; if (fread(&n, 4, 1, f) != 1) { fclose(f); continue; }
    for (unsigned t = 0; t < n; t++) {
      float d[12]; unsigned short at;
      if (fread(d, 4, 12, f) != 12) break;
      if (fread(&at, 2, 1, f) != 1) break;
      v3 a = V(d[3] + dx, d[4] + dy, d[5] + dz), bb = V(d[6] + dx, d[7] + dy, d[8] + dz), c = V(d[9] + dx, d[10] + dy, d[11] + dz);
      tri(a, bb, c, V(r, g, b));
    }
    fclose(f);
  }
  v3 L1 = nrm(V(-0.5f, 0.9f, -0.6f)), L2 = nrm(V(0.7f, 0.2f, -0.5f));
  FILE *o = fopen(argv[10], "wb");
  fprintf(o, "P6 %d %d 255\n", W, H);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      int i = y * W + x;
      float c[3] = {0.96f, 0.955f, 0.945f};
      if (zb[i] < 1e29f) {
        v3 n = V(nb[i * 3], nb[i * 3 + 1], nb[i * 3 + 2]);
        if (dot(n, fw) > 0) n = V(-n.x, -n.y, -n.z);
        float l = 0.30f + 0.62f * fmaxf(0, dot(n, L1)) + 0.25f * fmaxf(0, dot(n, L2)) + 0.12f * (0.5f + 0.5f * n.y);
        // depth-discontinuity outline
        float e = 0;
        if (x > 0 && x < W - 1 && y > 0 && y < H - 1) {
          float z0 = zb[i];
          float m = fmaxf(fmaxf(fabsf(zb[i - 1] - z0), fabsf(zb[i + 1] - z0)), fmaxf(fabsf(zb[i - W] - z0), fabsf(zb[i + W] - z0)));
          if (m > 0.8f) e = 0.55f;
          v3 n1 = V(nb[(i + 1) * 3], nb[(i + 1) * 3 + 1], nb[(i + 1) * 3 + 2]);
          v3 n2 = V(nb[(i + W) * 3], nb[(i + W) * 3 + 1], nb[(i + W) * 3 + 2]);
          if (fabsf(dot(n1, n)) < 0.85f || fabsf(dot(n2, n)) < 0.85f) e = fmaxf(e, 0.30f);
        }
        for (int k = 0; k < 3; k++) c[k] = fminf(1, cb[i * 3 + k] * l) * (1 - e);
      }
      unsigned char p[3] = {(unsigned char)(powf(c[0], 1 / 1.6f) * 255), (unsigned char)(powf(c[1], 1 / 1.6f) * 255), (unsigned char)(powf(c[2], 1 / 1.6f) * 255)};
      fwrite(p, 1, 3, o);
    }
  fclose(o);
  return 0;
}
