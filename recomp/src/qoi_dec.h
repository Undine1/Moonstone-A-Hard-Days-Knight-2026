/* qoi_dec.h -- minimal QOI image decoder (RGB24 output), for the boot splash only.
 *
 * QOI ("Quite OK Image", qoiformat.org) is a trivial lossless format; this is the
 * whole decoder. Public-domain algorithm. We decode ONLY our own embedded splash
 * (recomp/src/splash_image.h) -- never untrusted/network input -- so there is no
 * attack surface. Bounds-checked against the caller's output capacity regardless.
 */
#ifndef QOI_DEC_INCLUDED
#define QOI_DEC_INCLUDED
#include <stdint.h>

/* Decode QOI bytes `in` (length n) into `out` as RGB24 (capacity `outcap` bytes).
 * Returns the number of bytes written, or 0 on a bad header / capacity overflow. */
static int qoi_decode_rgb(const uint8_t *in, int n, uint8_t *out, int outcap) {
    if (n < 14 || in[0] != 'q' || in[1] != 'o' || in[2] != 'i' || in[3] != 'f') return 0;
    long w = ((long)in[4] << 24) | (in[5] << 16) | (in[6] << 8) | in[7];
    long h = ((long)in[8] << 24) | (in[9] << 16) | (in[10] << 8) | in[11];
    long need = w * h * 3;
    if (w <= 0 || h <= 0 || need > outcap) return 0;

    uint8_t idx[64][4]; for (int i = 0; i < 64; i++) idx[i][0] = idx[i][1] = idx[i][2] = idx[i][3] = 0;
    uint8_t r = 0, g = 0, b = 0, a = 255;
    long o = 0; int p = 14;

    while (o < need && p < n) {
        uint8_t op = in[p++];
        if (op == 0xFE) {                         /* QOI_OP_RGB  */ if (p+3 > n) break; r = in[p]; g = in[p+1]; b = in[p+2]; p += 3; }
        else if (op == 0xFF) {                    /* QOI_OP_RGBA */ if (p+4 > n) break; r = in[p]; g = in[p+1]; b = in[p+2]; a = in[p+3]; p += 4; }
        else if ((op & 0xC0) == 0x00) {           /* QOI_OP_INDEX*/ r = idx[op][0]; g = idx[op][1]; b = idx[op][2]; a = idx[op][3]; }
        else if ((op & 0xC0) == 0x40) {           /* QOI_OP_DIFF */ r += ((op >> 4) & 3) - 2; g += ((op >> 2) & 3) - 2; b += (op & 3) - 2; }
        else if ((op & 0xC0) == 0x80) {           /* QOI_OP_LUMA */ if (p >= n) break; uint8_t d = in[p++]; int dg = (op & 0x3f) - 32; r += dg + ((d >> 4) & 0xf) - 8; g += dg; b += dg + (d & 0xf) - 8; }
        else {                                    /* QOI_OP_RUN  */ int run = (op & 0x3f) + 1; while (run-- > 0 && o < need) { out[o++] = r; out[o++] = g; out[o++] = b; } continue; }
        idx[(r*3 + g*5 + b*7 + a*11) & 63][0] = r;
        idx[(r*3 + g*5 + b*7 + a*11) & 63][1] = g;
        idx[(r*3 + g*5 + b*7 + a*11) & 63][2] = b;
        idx[(r*3 + g*5 + b*7 + a*11) & 63][3] = a;
        out[o++] = r; out[o++] = g; out[o++] = b;
    }
    return (int)o;
}
#endif
