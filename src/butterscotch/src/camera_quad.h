#ifndef BS_CAMERA_QUAD_H
#define BS_CAMERA_QUAD_H
#include "matrix_math.h"
#include <math.h>

// 2 = visible, 1 = bounded read-ahead, 0 = outside. Test the actual GL
// projection; camera scalar coordinates can be stale after matrix_set/apply.
static inline int CameraQuad_classify(const Matrix4f* clip, const float xy[8],
                                      float marginX, float marginY) {
    // A custom perspective or invalid matrix is not safe for this 2D culler.
    if (clip->m[3] != 0 || clip->m[7] != 0 || clip->m[15] != 1) return 2;
    float l=INFINITY, r=-INFINITY, t=INFINITY, b=-INFINITY;
    for (int i=0;i<4;i++) {
        float x,y;
        Matrix4f_transformPoint(clip,xy[i*2],xy[i*2+1],&x,&y);
        if (!isfinite(x) || !isfinite(y)) return 2;
        l=fminf(l,x); r=fmaxf(r,x); t=fminf(t,y); b=fmaxf(b,y);
    }
    if(r>=-1 && l<=1 && b>=-1 && t<=1) return 2;
    return r>=-1-marginX && l<=1+marginX && b>=-1-marginY && t<=1+marginY ? 1 : 0;
}
#endif
