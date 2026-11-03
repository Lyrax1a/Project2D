#include "usurper_fixed.h"
#include "usurper_direction_fast.h"

static const int32_t cordic_angles[] = {2949120,1740967,919879,466945,234379,117304,
    58666,29335,14668,7334,3667,1833,917,458,229,115,57,29,14,7,4,2};
static const int32_t cordic_radians[] = {843314857,497837829,263043837,133525159,67021687,
    33543516,16775851,8388437,4194283,2097149,1048576,524288,262144,131072,65536,
    32768,16384,8192,4096,2048,1024,512,256,128,64,32,16,8,4,2};

static int32_t normalize(int32_t angle) {
    angle %= US_FX_TURN;
    return angle < 0 ? angle + US_FX_TURN : angle;
}

uint32_t us_fx_length(int32_t x, int32_t y) {
    uint64_t value = (uint64_t)((int64_t)x*x + (int64_t)y*y), root = 0;
    uint64_t bit = (uint64_t)1u << 62;
    while (bit > value) bit >>= 2;
    while (bit) {
        if (value >= root + bit) { value -= root + bit; root = (root >> 1) + bit; }
        else root >>= 1;
        bit >>= 2;
    }
    return (uint32_t)root;
}

int32_t us_fx_angle(int32_t x, int32_t y) {
    if (!x) return y > 0 ? 90 * US_FX_ONE : y < 0 ? 270 * US_FX_ONE : 0;
    if (!y) return x < 0 ? 180 * US_FX_ONE : 0;
    int32_t angle = 0;
    if (x < 0) { x = -x; y = -y; angle = 180 * US_FX_ONE; }
    for (unsigned i = 0; i < 22u; ++i) {
        int32_t old_x = x;
        if (y > 0) { x += y >> i; y -= old_x >> i; angle += cordic_angles[i]; }
        else { x -= y >> i; y += old_x >> i; angle -= cordic_angles[i]; }
    }
    return normalize(angle);
}

void us_fx_direction(int32_t angle, int32_t *x, int32_t *y) {
    angle = normalize(angle);
    if (!(angle % (90 * US_FX_ONE))) {
        static const int32_t cx[4] = {US_FX_ONE,0,-US_FX_ONE,0}, sy[4] = {0,US_FX_ONE,0,-US_FX_ONE};
        *x = cx[angle / (90 * US_FX_ONE)]; *y = sy[angle / (90 * US_FX_ONE)]; return;
    }
    if (angle > 180 * US_FX_ONE) angle -= US_FX_TURN;
    int32_t sign = 1;
    if (angle > 90 * US_FX_ONE) { angle -= 180 * US_FX_ONE; sign = -1; }
    else if (angle < -90 * US_FX_ONE) { angle += 180 * US_FX_ONE; sign = -1; }
    int32_t cx = 163008219, sy = 0;
    for (unsigned i = 0; i < 22u; ++i) {
        int32_t old_x = cx;
        if (angle >= 0) { cx -= sy >> i; sy += old_x >> i; angle -= cordic_angles[i]; }
        else { cx += sy >> i; sy -= old_x >> i; angle += cordic_angles[i]; }
    }
    *x = sign * (cx / 4096); *y = sign * (sy / 4096);
}

int32_t us_fx_turn_toward(int32_t angle, int32_t target, int32_t step) {
    int32_t delta = normalize(target - angle);
    /* The Vector2 angle at the negative X axis is float PI.
     * Its exact opposite follows the positive half-turn in rotate_toward. */
    if (delta > 180 * US_FX_ONE) delta -= US_FX_TURN;
    if (delta > step) delta = step;
    else if (delta < -step) delta = -step;
    return normalize(angle + delta);
}

unsigned us_fx_clip_motion(int32_t *x, int32_t *y, int32_t dx, int32_t dy,
                           int32_t left, int32_t top, int32_t right, int32_t bottom) {
    if (*x < left) *x = left; else if (*x > right) *x = right;
    if (*y < top) *y = top; else if (*y > bottom) *y = bottom;
    uint32_t travel = 1073741824u, fraction;
    if (dx > 0 && (int64_t)*x + dx > right) { fraction = (uint32_t)((int64_t)(right - *x) * 1073741824 / dx); if (fraction < travel) travel = fraction; }
    else if (dx < 0 && (int64_t)*x + dx < left) { fraction = (uint32_t)((int64_t)(left - *x) * 1073741824 / dx); if (fraction < travel) travel = fraction; }
    if (dy > 0 && (int64_t)*y + dy > bottom) { fraction = (uint32_t)((int64_t)(bottom - *y) * 1073741824 / dy); if (fraction < travel) travel = fraction; }
    else if (dy < 0 && (int64_t)*y + dy < top) { fraction = (uint32_t)((int64_t)(top - *y) * 1073741824 / dy); if (fraction < travel) travel = fraction; }
    *x += (int32_t)((int64_t)dx * travel / 1073741824);
    *y += (int32_t)((int64_t)dy * travel / 1073741824);
    return travel < 1073741824u;
}

float us_fx_sqrt_float(float value) {
    /* Correctly round sqrt of a positive normal IEEE float using a 48-bit
     * integer radicand; gameplay squared distances are normal or zero. */
    if (value <= 0.0f) return 0.0f;
    union { float f; uint32_t u; } bits = {.f = value};
    int exponent = (int)((bits.u >> 23) & 255u) - 127;
    uint64_t radicand = (uint64_t)((bits.u & 0x7fffffu) | 0x800000u) << (23 + (exponent & 1));
    uint64_t value_left = radicand, root = 0, bit = (uint64_t)1u << 46;
    while (bit > value_left) bit >>= 2;
    while (bit) {
        if (value_left >= root + bit) { value_left -= root + bit; root = (root >> 1) + bit; }
        else root >>= 1;
        bit >>= 2;
    }
    if (radicand - root * root > root) ++root;
    exponent = (exponent - (exponent & 1)) / 2;
    if (root >= 0x1000000u) { root >>= 1; ++exponent; }
    bits.u = ((uint32_t)(exponent + 127) << 23) | ((uint32_t)root & 0x7fffffu);
    return bits.f;
}

float us_fx_angle_float(float x, float y) {
    const double pi = 3.14159265358979323846;
    if (!x) return y > 0 ? (float)(pi / 2) : y < 0 ? (float)(-pi / 2) : 0.0f;
    if (!y) return x < 0 ? (float)pi : 0.0f;
    /* Unit-length aim vectors need as much precision as field coordinates.
     * Power-of-two scaling preserves float32 mantissas and bounds CORDIC's
     * intermediate gain below INT32_MAX, including diagonal vectors. */
    union {float f;uint32_t u;} xb={.f=x},yb={.f=y};
    unsigned xe=(xb.u>>23)&255u,ye=(yb.u>>23)&255u,maximum=xe>ye?xe:ye;
    int32_t cx,sy;
    if(maximum&&maximum<255u) {
        /* Normalization scales only by powers of two. Extract the exact
         * float significands directly, preserving the former truncation
         * into the CORDIC input without repeated software float multiplies. */
        uint32_t xm=(xb.u&0x7fffffu)|(xe?0x800000u:0u),ym=(yb.u&0x7fffffu)|(ye?0x800000u:0u);
        int xs=(int)(xe?xe:1u)+5-(int)maximum,ys=(int)(ye?ye:1u)+5-(int)maximum;
        uint32_t xx=xs>=0?xm<<xs:xs<=-32?0u:xm>>(-xs);
        uint32_t yy=ys>=0?ym<<ys:ys<=-32?0u:ym>>(-ys);
        cx=xb.u>>31?-(int32_t)xx:(int32_t)xx;sy=yb.u>>31?-(int32_t)yy:(int32_t)yy;
    } else {
        float magnitude=x<0?-x:x,ay=y<0?-y:y;if(ay>magnitude)magnitude=ay;
        while(magnitude<256.0f){magnitude*=2;x*=2;y*=2;}
        while(magnitude>=512.0f){magnitude*=0.5f;x*=0.5f;y*=0.5f;}
        cx=(int32_t)(x*1048576.0f);sy=(int32_t)(y*1048576.0f);
    }
    int64_t angle = 0;
    if (cx < 0) { angle = sy < 0 ? -3373259426LL : 3373259426LL; cx = -cx; sy = -sy; }
    for (unsigned i = 0; i < 30u && sy; ++i) {
        int32_t previous = cx;
        if (sy > 0) { cx += sy >> i; sy -= previous >> i; angle += cordic_radians[i]; }
        else { cx -= sy >> i; sy += previous >> i; angle -= cordic_radians[i]; }
    }
    return (float)(angle / 1073741824.0);
}

static double reduced_radians(double radians) {
    const double pi = 3.14159265358979323846, tau = 6.28318530717958647692;
    radians -= (int64_t)(radians / tau) * tau;
    if (radians > pi) radians -= tau;
    else if (radians < -pi) radians += tau;
    return radians;
}
double us_fx_sin_double(double radians) {
    const double pi = 3.14159265358979323846;
    double x = reduced_radians(radians);
    if (x > pi * 0.5) x = pi - x;
    else if (x < -pi * 0.5) x = -pi - x;
    double y = x * x;
    double value = 1.0 / 51090942171709440000.0;
    value = -1.0 / 121645100408832000.0 + y * value;
    value = 1.0 / 355687428096000.0 + y * value;
    value = -1.0 / 1307674368000.0 + y * value;
    value = 1.0 / 6227020800.0 + y * value;
    value = -1.0 / 39916800.0 + y * value;
    value = 1.0 / 362880.0 + y * value;
    value = -1.0 / 5040.0 + y * value;
    value = 1.0 / 120.0 + y * value;
    value = -1.0 / 6.0 + y * value;
    return x * (1.0 + y * value);
}
double us_fx_cos_double(double radians) {
    const double pi = 3.14159265358979323846;
    double x = reduced_radians(radians), sign = 1;
    if (x > pi * 0.5) { x = pi - x; sign = -1; }
    else if (x < -pi * 0.5) { x = -pi - x; sign = -1; }
    double y = x * x;
    double value = -1.0 / 1124000727777607680000.0;
    value = 1.0 / 2432902008176640000.0 + y * value;
    value = -1.0 / 6402373705728000.0 + y * value;
    value = 1.0 / 20922789888000.0 + y * value;
    value = -1.0 / 87178291200.0 + y * value;
    value = 1.0 / 479001600.0 + y * value;
    value = -1.0 / 3628800.0 + y * value;
    value = 1.0 / 40320.0 + y * value;
    value = -1.0 / 720.0 + y * value;
    value = 1.0 / 24.0 + y * value;
    value = -1.0 / 2.0 + y * value;
    return sign * (1.0 + y * value);
}
void us_fx_direction_float(float radians, float *x, float *y) {
    /* A drawn owner often asks for exactly the same rotation for many
     * primitives. Cache the complete float bit pattern, never an angle bin.
     * This routine is pure and is not called from an interrupt handler. */
    static struct { uint32_t bits,valid; float x,y; } cache[16];
    union { float f; uint32_t bits; } key={.f=radians};
    unsigned cacheable=(key.bits&0x7fffffffu)<=0x41800000u;
    unsigned slot=(key.bits^(key.bits>>9)^(key.bits>>16))&15u;
    if(cacheable&&cache[slot].valid&&cache[slot].bits==key.bits){
        *x=cache[slot].x;*y=cache[slot].y;return;
    }
    /* Preserve tiny Vector2 rotations: integer angle quantization introduces
     * nonzero sin(0) and drift during a long, slowly tracking flame. */
    float cx,sy;
    if(!us_direction_fast(radians,&cx,&sy)){
        cx=(float)us_fx_cos_double(radians);sy=(float)us_fx_sin_double(radians);
    }
    if(cacheable){cache[slot].bits=key.bits;cache[slot].x=cx;cache[slot].y=sy;cache[slot].valid=1;}
    *x=cx;*y=sy;
}

double us_fx_turn_double(double radians, double target, double step) {
    const double pi = 3.14159265358979323846;
    double difference = (double)target - radians;
    difference -= (int)(difference / (2 * pi)) * (2 * pi);
    if (difference > pi) difference -= 2 * pi;
    else if (difference < -pi) difference += 2 * pi;
    if (difference > step) difference = step;
    else if (difference < -step) difference = -step;
    return radians + difference;
}
float us_fx_turn_float(float radians, float target, double step) {
    return (float)us_fx_turn_double(radians, target, step);
}
