/* Dot product with upwind gradient */
/*
  Copyright (C) 2009 University of Texas at Austin
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <rsf.h>

#ifdef _OPENMP
#include <omp.h>
#endif

#ifndef _upgrad_h

typedef struct Upgrad *upgrad;
/* abstract data type */
/*^*/

#endif

struct Upgrad {
    int *order;
    unsigned char **update;
    float **ww;
};

static int ndim, nt, ss[3];
static const int *nn;
static float dd[3];
static float **t0;

static int fermat(const void *a, const void *b)
/* comparison for traveltime sorting from small to large */
{
    int its;
    float ta, tb;

#ifdef _OPENMP
    its = omp_get_thread_num();
#else
    its = 0;
#endif

    ta = t0[its][*(int *)a];
    tb = t0[its][*(int *)b];

    if (ta >  tb) return 1;
    if (ta == tb) return 0;
    return -1;
}

void upgrad_init(int mdim        /* number of dimensions */,
		 const int *mm   /* [dim] data size */,
		 const float *d  /* [dim] data sampling */)
/*< initialize >*/
{
    int i, mts;

    if (mdim > 3) sf_error("%s: dim=%d > 3",__FILE__,mdim);

    ndim = mdim;
    nn = mm;

    nt = 1;
    for (i=0; i < ndim; i++) {
	ss[i] = nt;
	nt *= nn[i];
	dd[i] = 1.0/(d[i]*d[i]);
    }

#ifdef _OPENMP
    mts = omp_get_max_threads();
#else
    mts = 1;
#endif

    t0 = (float **)malloc(mts*sizeof(float *));
}

upgrad upgrad_alloc()
/*< allocate memory for stencil >*/
{
    upgrad upg;

    upg = (upgrad) sf_alloc(1,sizeof(*upg));

    upg->update = sf_ucharalloc2(2,nt);
    upg->ww = sf_floatalloc2(ndim+1,nt);
    upg->order = sf_intalloc(nt);

    return upg;
}

void upgrad_set(upgrad upg, float *r0 /* reference */)
/*< supply reference >*/
{
    int i, m, it, jt, ii[3], a, b, its;
    unsigned char *up;
    float t, t2;

#ifdef _OPENMP
    its = omp_get_thread_num();
#else
    its = 0;
#endif

    t0[its] = r0;

    /* sort from small to large traveltime */
    for (it = 0; it < nt; it++) {
	upg->order[it] = it;
    }
    qsort(upg->order, nt, sizeof(int), fermat);
     
    for (it = 0; it < nt; it++) {
	jt = upg->order[it];

	sf_line2cart(ndim,nn,jt,ii);
	up = upg->update[it];
	up[0] = up[1] = 0;
	t = t0[its][jt];
	upg->ww[it][ndim] = 0.;
	for (i=0, m=1; i < ndim; i++, m <<= 1) {
	    a = jt-ss[i];
	    b = jt+ss[i];
	    if ((ii[i] == 0) || 
		(ii[i] != nn[i]-1 && 1==fermat(&a,&b))) {
		up[1] |= m;
		t2 = t0[its][b];
	    } else {
		t2 = t0[its][a];
	    }

	    if (t2 < t) {
		up[0] |= m;
		upg->ww[it][i] = (t-t2)*dd[i];
		upg->ww[it][ndim] += upg->ww[it][i];
	    }	    
	}
    }
}

void upgrad_close(upgrad upg)
/*< free allocated storage >*/
{
    free(*(upg->ww));
    free(upg->ww);
    free(*(upg->update));
    free(upg->update);
    free(upg->order);
    free(upg);
}

void upgrad_solve(upgrad upg,
		  const float *rhs /* right-hand side */, 
		  float *x         /* solution */,
		  const float *x0  /* initial solution */)
/*< inverse operator >*/
{
    int it, jt, i, m, j;
    unsigned char *up;
    float num, den;
   
    for (it = 0; it < nt; it++) {
	jt = upg->order[it];

	num = rhs[jt];
	up = upg->update[it];
	den = upg->ww[it][ndim];

	if (den == 0.) { /* at the source, use boundary conditions */
	    x[jt] = (NULL != x0)? x0[jt]: 0.;
	    continue;
	}

	for (i=0, m=1; i < ndim; i++, m <<= 1) {
	    if (up[0] & m) {
		j = (up[1] & m)? jt+ss[i]:jt-ss[i];		
		num += upg->ww[it][i]*x[j];		
	    }
	}
	
	x[jt] = num/den;
    }
}

void upgrad_inverse(upgrad upg,
		    float *rhs       /* right-hand side */,
		    const float *x   /* solution */,
		    const float *x0  /* initial solution */)
/*< adjoint of inverse operator >*/
{
    int it, jt, i, m, j;
    unsigned char *up;
    float den, w;

    for (it = 0; it < nt; it++) {
	rhs[it] = 0.;
    }
   
    for (it = nt-1; it >= 0; it--) {
	jt = upg->order[it];

	rhs[jt] += x[jt];

	up = upg->update[it];
	den = upg->ww[it][ndim];

	if (den == 0.) { /* at the source, use boundary conditions */
	    rhs[jt] = (NULL != x0)? x0[jt]: 0.;
	} else {
	    rhs[jt] = rhs[jt]/den;
	}

	for (i=0, m=1; i < ndim; i++, m <<= 1) {
	    if (up[0] & m) {
		j = (up[1] & m)? jt+ss[i]:jt-ss[i];
		w = upg->ww[it][i]*rhs[jt];
		rhs[j] += w;
	    }
	}
    }
}

void upgrad_forw(upgrad upg,
		 const float *x /* solution */,
		 float *rhs     /* right-hand side */)
/*< forward operator >*/
{
    int it, jt, i, m, j;
    unsigned char *up;
    float num, x2;
   
    for (it = 0; it < nt; it++) {
	jt = upg->order[it];

	x2 = x[jt];
	up = upg->update[it];
	num = 0.;

	for (i=0, m=1; i < ndim; i++, m <<= 1) {
	    if (up[0] & m) {
		j = (up[1] & m)? jt+ss[i]:jt-ss[i];		
		num += upg->ww[it][i]*(x2-x[j]);		
	    }
	}
	
	rhs[jt] = num;
    }
}

void upgrad_adj(upgrad upg,
		float *x         /* solution */,
		const float *rhs /* right-hand side */)
/*< adjoint operator >*/
{
    int it, jt, i, m, j;
    unsigned char *up;
    float w;

    for (it = 0; it < nt; it++) {
	x[it] = 0.;
    }
   
    for (it = nt-1; it >= 0; it--) {
	jt = upg->order[it];
	up = upg->update[it];

	for (i=0, m=1; i < ndim; i++, m <<= 1) {
	    if (up[0] & m) {
		j = (up[1] & m)? jt+ss[i]:jt-ss[i];
		w = upg->ww[it][i]*rhs[jt];

		x[jt] += w;
		x[j]  -= w;
	    }
	}
    }
}
