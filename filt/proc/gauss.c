#include <math.h>

#include <rsf.h>

#include "gauss.h"

static float *shape;

void gauss_init(int nfft, float rect)
{
    int iw, nw;
    float dw, w;

    /* determine frequency sampling (for real to complex FFT) */
    nw = nfft/2+1;
    dw = 2.*SF_PI/nfft;

    rect = sqrtf((rect*rect-1.)/12.);

    shape = sf_floatalloc(nw);

    for (iw=0; iw < nw; iw++) {
	w = iw*dw*rect;
	w *= w;
	shape[iw] = expf(-w)/nfft;
    }

    sf_freqfilt_init(nfft,nw);
    sf_freqfilt_set(shape);
}

void gauss_close(void) {
    free(shape);
    sf_freqfilt_close();
}

/* 	$Id$	 */
