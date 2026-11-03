#include "usurper_flame_visual.h"
#include "usurper_circle_generated.h"

static unsigned alpha8(double alpha) {
    if (alpha <= 0) return 0;
    if (alpha >= 1) return 255;
    return (unsigned)(alpha * 255 + 0.5);
}
void us_flame_plume(float hx, float hy, double reach, double clock, const us_flame_painter_t *painter) {
    const double pi = 3.14159265358979323846;
    us_flame_vertex_t points[26];
    for (unsigned layer = 0; layer < 3u; ++layer) {
        for (unsigned side = 0; side < 2u; ++side) for (unsigned step = 0; step < 13u; ++step) {
            double t = (side ? 12u - step : step) / 12.0, wave = us_fx_sin_double(t * pi);
            double width = (3 + wave * 44) * (1 - layer * 0.26);
            double curl = us_fx_sin_double(t * 18 - clock * 22 + layer) * wave * 3;
            float along = (float)(10 + t * reach * (1 - layer * 0.14));
            float across = (float)((side ? 1 : -1) * width + curl);
            points[side * 13u + step].x = hx * along + hy * across;
            points[side * 13u + step].y = hy * along - hx * across;
        }
        painter->polygon(painter->context, points, 26u, layer, alpha8((float)(0.14 + layer * 0.2)));
    }
    for (unsigned i = 0; i < 18u; ++i) {
        double t = clock * (1.6 + (i % 3u) * 0.15) + i * 0.137;
        t -= (int64_t)t; if (t < 0) t += 1;
        double wave = us_fx_sin_double(t * pi);
        float across = (float)(us_fx_sin_double(i * 2.4 + t * 7) * 44 * t * 0.75);
        float along = (float)(12 + t * reach), radius = (float)(1.5 + 3 * wave);
        painter->circle(painter->context, hx * along + hy * across, hy * along - hx * across,
            radius, 3u, alpha8((float)(wave * 0.5)));
    }
    painter->circle(painter->context, hx * 12, hy * 12, 5, 0u, alpha8(0.4f));
    painter->circle(painter->context, hx * 12, hy * 12, 2.5f, 4u, alpha8(0.95f));
}
static int ceil_float(float value) { int integer = (int)value; return value > integer ? integer + 1 : integer; }
static float raster_vertex(float value) {
    /* Match the compatibility renderer's 8-bit subpixel vertex grid.
     * Unquantized intersections can cross a fragment center by < 1/256px. */
    double scaled = (double)value * 256;
    return (float)((int64_t)(scaled + (scaled < 0 ? -0.5 : 0.5)) / 256.0);
}
typedef struct { int first, last; int64_t x, slope; } raster_edge_t;
void us_flame_raster_polygon(const us_flame_vertex_t *points, unsigned count, unsigned color, unsigned alpha,
    unsigned width, unsigned height, void (*span)(void *, int, int, unsigned, unsigned, unsigned), void *context) {
    if (!alpha || count < 3u || count > US_RASTER_POLYGON_MAX || !width || !height) return;
    raster_edge_t edges[US_RASTER_POLYGON_MAX]; unsigned edge_count = 0;
    int first = (int)height, last = 0;
    for (unsigned i = 0; i < count; ++i) {
        us_flame_vertex_t a = points[i], b = points[(i + 1u) % count];
        a.x = raster_vertex(a.x); a.y = raster_vertex(a.y);
        b.x = raster_vertex(b.x); b.y = raster_vertex(b.y);
        if (a.y == b.y) continue;
        if (a.y > b.y) { us_flame_vertex_t temporary = a; a = b; b = temporary; }
        int start = ceil_float(a.y - 0.5f), end = ceil_float(b.y - 0.5f);
        if (start < 0) start = 0;
        if (end > (int)height) end = (int)height;
        if (end <= start) continue;
        double slope = ((double)b.x - a.x) / ((double)b.y - a.y);
        double x = a.x + (start + 0.5 - a.y) * slope;
        edges[edge_count++] = (raster_edge_t){start, end, (int64_t)(x * 65536), (int64_t)(slope * 65536)};
        if (start < first) first = start;
        if (end > last) last = end;
    }
    for (int y = first; y < last; ++y) {
        int64_t intersections[US_RASTER_POLYGON_MAX]; unsigned hits = 0;
        for (unsigned i = 0; i < edge_count; ++i) if (y >= edges[i].first && y < edges[i].last) {
            int64_t x = edges[i].x; edges[i].x += edges[i].slope;
            unsigned insert = hits;
            while (insert && intersections[insert - 1u] > x) { intersections[insert] = intersections[insert - 1u]; --insert; }
            intersections[insert] = x; ++hits;
        }
        for (unsigned i = 0; i + 1u < hits; i += 2u) {
            int left = (int)((intersections[i] + 32767) >> 16);
            int right = (int)((intersections[i + 1u] + 32767) >> 16);
            if (left < 0) left = 0;
            if (right > (int)width) right = (int)width;
            if (right > left) span(context, left, y, (unsigned)(right - left), color, alpha);
        }
    }
}
void us_flame_circle_polygon(float x, float y, float radius, us_flame_vertex_t points[64]) {
    for (unsigned i = 0; i < 64u; ++i) {
        points[i].x = x + usurper_circle_vertices[i][0] * radius;
        points[i].y = y + usurper_circle_vertices[i][1] * radius;
    }
}
void us_flame_raster_circle(float x, float y, float radius, unsigned color, unsigned alpha,
    unsigned width, unsigned height, void (*span)(void *, int, int, unsigned, unsigned, unsigned), void *context) {
    if (radius <= 0 || !alpha) return;
    us_flame_vertex_t points[64]; us_flame_circle_polygon(x, y, radius, points);
    us_flame_raster_polygon(points, 64u, color, alpha, width, height, span, context);
}
