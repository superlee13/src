/* Double square-root eikonal solver interface */
/*
  Copyright (C) 2011 University of Texas at Austin
  
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

#include "dsreiko.h"

struct Upd {
    double value, delta;
};

static double thres, tol, miu;
static int nloop;

static float *o, *v, *d;
static int *n, *in, s[3];
static float **x, **xn, **x1;
static int *offsets, *flag;
static float *t, *alpha;

void pqueue_insert(float* v1);
float* pqueue_extract(void);
void pqueue_update(int index);
int neighbors_default();
int neighbours(float* time, int i);
int update(float value, float* time, int i, int f, float al);
float qsolve(float* time, int i, int *f, float *al);
bool updaten(float* res, struct Upd *xj[], double vr, double vs, int *f, float *al);
double bisect(struct Upd *xj[],bool r, double vr, bool s, double vs, double min, double max);
double eval(struct Upd *xj[], bool r, double vr, bool s, double vs, double p);

void dsreiko_init(int *n_in   /* length */,
		  float *o_in /* origin */,
		  float *d_in /* increment */,
		  float thres_in,
		  float tol_in,
		  int nloop_in)
/*< initialize >*/
{
    int maxband;

    n = n_in;
    o = o_in;
    d = d_in;

    s[0] = 1; s[1] = n[0]; s[2] = n[0]*n[1];

    /* maximum length of heap */
    maxband = 0;

    if (n[0] > 1) maxband += 2*n[1]*n[2];
    if (n[1] > 1) maxband += 2*n[0]*n[2];
    if (n[2] > 1) maxband += 2*n[0]*n[1];

    x = (float **) sf_alloc ((10*maxband+1),sizeof (float *));
    in = sf_intalloc(n[0]*n[1]*n[2]);

    offsets = (int *) sf_alloc (n[0]*n[1]*n[2],sizeof (int));

    thres = thres_in;
    tol = tol_in;
    nloop = nloop_in;

    miu = d[0]/d[1];
}

void dsreiko_fastmarch(float *time /* time */,
		       float *v_in /* slowness squared */,
		       int *f_in   /* upwind flag */,
		       float *alpha_in /* barycentric coordinate */)
/*< fast-marching solver >*/
{
    float *p;
    int npoints, i;

    t = time;
    v = v_in;
    flag = f_in;
    alpha = alpha_in;

    xn = x;
    x1 = x+1;

    /* initialize from zero-offset plane */
    for (npoints =  neighbors_default();
	 npoints > 0;
	 npoints -= neighbours(t,i)) {
	/* Pick smallest value in the NarrowBand
	   mark as good, decrease points_left */

	p = pqueue_extract();

	if (p == NULL) {
	    sf_warning("%s: heap exausted!",__FILE__);
	    break;
	}
	
	i = p-t;

	in[i] = SF_IN;
    }
}

void dsreiko_mirror(float *time /*time*/)
/*< source-receiver reciprocity >*/
{
    int i, j, k;

    for (k=1; k < n[1]; k++) {
	for (j=0; j < k; j++) {
	    for (i=0; i < n[0]; i++) {
		time[k*s[2]+j*s[1]+i] = time[j*s[2]+k*s[1]+i];
	    }
	}
    }
}

void dsreiko_close()
/*< finalize >*/
{
    free(x);
    free(in);
}

void pqueue_insert(float* v1)
/* insert an element (smallest first) */
{
    int newOffset, tIndex;
    float **xi, **xq;
    unsigned int q;
    
    xi = ++xn;

    q = (unsigned int) (xn-x);
    for (q >>= 1; q > 0; q >>= 1) {
	xq = x + q;
	if (*v1 > **xq) break;
	*xi = *xq;

	/* moved down, update its offset */
	newOffset = xi-x;
	tIndex = (*xi)-t;
	offsets[tIndex] = newOffset;

	xi = xq;
    }
    *xi = v1; 
    
    /* far enough up, record the offset */
    newOffset = xi-x;
    tIndex = v1-t;
    offsets[tIndex] = newOffset;
}

float* pqueue_extract(void)
/* extract the smallest element */
{
    int newOffset, tIndex;
    unsigned int c;
    int nn;
    float *vv, *formerlyLast;
    float **xi, **xc;
    
    /* check if queue is empty */
    nn = (int) (xn-x);
    if (nn == 0) return NULL;

    vv = *x1;
    tIndex = vv-t;
    offsets[tIndex] = -1;

    *(xi = x1) = formerlyLast = *(xn--);
    nn--;
    for (c = 2; c <= (unsigned int) nn; c <<= 1) {
	xc = x + c;
	if (c < (unsigned int) nn && **xc > **(xc+1)) {
	    c++; xc++;
	}
	if (*formerlyLast <= **xc) break;
	*xi = *xc; 

	/* moved up, update its offset */
	newOffset = xi-x;
	tIndex = (*xi)-t;
	offsets[tIndex] = newOffset;

	xi = xc;
    }
    *xi = formerlyLast;

    /* far enough down, record the offset */
    newOffset = xi-x;
    tIndex = *xi-t;
    offsets[tIndex] = newOffset;

    return vv;
}

void pqueue_update(int index)
/* restore the heap */
{
    int newOffset, tIndex;
    unsigned int c;
    float **xc, **xi;
    
    c = offsets[index];
    xi = x+c;

    for (c >>= 1; c > 0; c >>= 1) {
	xc = x + c;
	if (t[index] > **xc) break;
	*xi = *xc;
	
	/* moved down, update its offset */
	newOffset = xi-x;
	tIndex = (*xi)-t;
	offsets[tIndex] = newOffset;
	
	xi = xc; 
    }
    *xi = t+index; 

    /* far enough up, record the offset */
    newOffset = xi-x;
    tIndex = *xi-t;
    offsets[tIndex] = newOffset;
}

int neighbors_default()
/* initialize source */
{
    int i, j, k, nxy;
    double vr, vs, temp[2];

    /* total number of points */
    nxy = n[0]*n[1]*n[2];

    /* set all points to be out */
    for (i=0; i < nxy; i++) {
	in[i] = SF_OUT;
	t[i] = SF_HUGE;
	offsets[i] = -1;
    }
    
    /* zero-offset and h = s-r */
    for (j=0; j < n[1]; j++) {
	for (k=0; k <= j; k++) {
	    for (i=0; i < n[0]; i++) {
		in[j*s[2]+k*s[1]+i] = SF_IN;
		t[j*s[2]+k*s[1]+i] = 0.;
		if (flag != NULL) flag[j*s[2]+k*s[1]+i] = 0;
		if (alpha != NULL) alpha[j*s[2]+k*s[1]+i] = 0.;
	    }
	}
    }

    /* h = r-s */
    for (j=0; j < n[1]-1; j++) {
	for (i=0; i < n[0]; i++) {
	    vr = (double)v[(j+1)*s[1]+i];
	    vs = (double)v[j*s[1]+i];

	    in[j*s[2]+(j+1)*s[1]+i] = SF_FRONT;

	    temp[0] = sqrt(vr)*d[1];
	    temp[1] = sqrt(vs)*d[2];

	    if (temp[0] <= temp[1]) {
		t[j*s[2]+(j+1)*s[1]+i] = temp[0];
		if (flag != NULL) flag[j*s[2]+(j+1)*s[1]+i] = 2;
		if (alpha != NULL) alpha[j*s[2]+(j+1)*s[1]+i] = 0.;
	    } else {
		t[j*s[2]+(j+1)*s[1]+i] = temp[1];
		if (flag != NULL) flag[j*s[2]+(j+1)*s[1]+i] = 3;
		if (alpha != NULL) alpha[j*s[2]+(j+1)*s[1]+i] = 0.;
	    }

	    pqueue_insert(t+j*s[2]+(j+1)*s[1]+i);
	}
    }
    
    return (nxy-n[0]*(n[1]*n[1]+3*n[1]-2)/2);
}

int neighbours(float* time, int i)
/* update neighbors of gridpoint i, return number of updated points */
{
    int j, k, ix, np;
    float ttemp, altemp;
    int ftemp;

    np = 0;
    for (j=0; j < 3; j++) {
	ix = (i/s[j])%n[j];

	/* try both directions */
	if (ix+1 <= n[j]-1) {
	    k = i+s[j]; 
	    if (in[k] != SF_IN) {
		ttemp = qsolve(time,k,&ftemp,&altemp);
		np += update(ttemp,time,k,ftemp,altemp);
	    }
	}
	if (ix-1 >= 0 ) {
	    k = i-s[j];
	    if (in[k] != SF_IN) {
		ttemp = qsolve(time,k,&ftemp,&altemp);
		np += update(ttemp,time,k,ftemp,altemp);
	    }
	}
    }
    return np;
}

int update(float value, float* time, int i, int f, float al)
/* update gridpoint i with new value and modify wave front */
{
    /* only update when smaller than current value */
    if (value < time[i]) {
	time[i] = value;
	if (flag != NULL) flag[i] = f;
	if (alpha != NULL) alpha[i] = al;
	if (in[i] == SF_OUT) { 
	    in[i] = SF_FRONT;      
	    pqueue_insert(time+i);
	    return 1;
	} else {
	    pqueue_update(i);
	}
    }
    
    return 0;
}

float qsolve(float* time, int i, int *f, float *al)
/* find new traveltime at gridpoint i */
{
    int j, k, ix[3];
    float a, b, res;
    double vr, vs;
    struct Upd xx[3], *xj[3];

    for (j=0; j<3; j++) 
	ix[j] = (i/s[j])%n[j];

    vr = v[ix[1]*s[1]+ix[0]];
    vs = v[ix[2]*s[1]+ix[0]];

    for (j=0; j < 3; j++) {
	if (ix[j] > 0) { 
	    k = i-s[j];
	    a = time[k];
	} else {
	    a = SF_HUGE;
	}
	
	if (ix[j] < n[j]-1) {
	    k = i+s[j];
	    b = time[k];
	} else {
	    b = SF_HUGE;
	}
	
	xj[j] = xx+j;
	xj[j]->delta = 1./(d[j]*d[j]);

	if (a < b) {
	    xj[j]->value = a;
	    if (j==0) *al = -1.;
	} else {
	    xj[j]->value = b;
	    if (j==0) *al = 1.;
	}	
    }

    /* z-r-s */
    if (xx[0].value < SF_HUGE && 
	xx[1].value < SF_HUGE && 
	xx[2].value < SF_HUGE) {

	*f = 7;
	if (updaten(&res,xj,vr,vs,f,al)) 
	    return res;

	if (xx[1].value <= xx[2].value) {
	    *f = 6;
	    if (updaten(&res,xj,vr,vs,f,al)) 
		return res;
	
	    *f = 8;
	    if (updaten(&res,xj,vr,vs,f,al)) 
		return res;
	} else {
	    *f = 5;
	    if (updaten(&res,xj,vr,vs,f,al)) 
		return res;

	    *f = 9;
	    if (updaten(&res,xj,vr,vs,f,al)) 
		return res;
	}

	return SF_HUGE;
    }

    /* r-s */
    if (xx[0].value == SF_HUGE && 
	xx[1].value <  SF_HUGE && 
	xx[2].value <  SF_HUGE) {

	*f = 4;
	if (updaten(&res,xj,vr,vs,f,al)) 
	    return res;

	return SF_HUGE;
    }

    /* z-s */
    if (xx[0].value <  SF_HUGE && 
	xx[1].value == SF_HUGE && 
	xx[2].value <  SF_HUGE) {

	*f = 5;
	if (updaten(&res,xj,vr,vs,f,al)) 
	    return res;
	
	*f = 9;
	if (updaten(&res,xj,vr,vs,f,al)) 
	    return res;
	
	return SF_HUGE;
    }

    /* z-r */
    if (xx[0].value <  SF_HUGE && 
	xx[1].value <  SF_HUGE && 
	xx[2].value == SF_HUGE) {

	*f = 6;
	if (updaten(&res,xj,vr,vs,f,al)) 
	    return res;
	
	*f = 8;
	if (updaten(&res,xj,vr,vs,f,al)) 
	    return res;
	
	return SF_HUGE;
    }

    /* z */
    if (xx[0].value <  SF_HUGE && 
	xx[1].value == SF_HUGE && 
	xx[2].value == SF_HUGE) {

	*f = 1;
	if (updaten(&res,xj,vr,vs,f,al)) 
	    return res;

	return SF_HUGE;
    }

    /* r */
    if (xx[0].value == SF_HUGE && 
	xx[1].value <  SF_HUGE && 
	xx[2].value == SF_HUGE) {

	*f = 2;
	if (updaten(&res,xj,vr,vs,f,al)) 
	    return res;

	return SF_HUGE;
    }

    /* s */
    if (xx[0].value == SF_HUGE && 
	xx[1].value == SF_HUGE && 
	xx[2].value <  SF_HUGE) {

	*f = 3;
	if (updaten(&res,xj,vr,vs,f,al)) 
	    return res;

	return SF_HUGE;
    }

    return SF_HUGE;
}

bool updaten(float* res, struct Upd *xj[], double vr, double vs, int *f, float *al)
/* calculate new traveltime */
{
    double tt, temp[2];
    double kappa1, kappa2;
    double min, max;
    int j;
    
    /* one-sided */
    if (*f == 1) {
	tt = (sqrt(vs)+sqrt(vr))/sqrt(xj[0]->delta)+xj[0]->value;
	
	*res = tt;
	*al *= 1.;
	return true;
    }
    
    if (*f == 2) {
	tt = sqrt(vr/xj[1]->delta)+xj[1]->value;

	*res = tt;
	*al *= 0.;
	return true;
    }

    if (*f == 3) {
	tt = sqrt(vs/xj[2]->delta)+xj[2]->value;

	*res = tt;
	*al *= 0.;
	return true;
    }

    /* two-sided */
    if (*f == 4) {
	temp[0] = sqrt(vr/xj[1]->delta)+xj[1]->value;
	temp[1] = sqrt(vs/xj[2]->delta)+xj[2]->value;
	
	if (temp[0] <= temp[1]) {
	    *res = temp[0];
	    *f = 2;
	} else {
	    *res = temp[1];
	    *f = 3;
	}

	*al *= 0.;	

	return true;
    }

    if (*f == 5) {
	if (xj[0]->value >= xj[2]->value)
	    min = xj[0]->value;
	else
	    min = xj[2]->value;

	max = SF_MIN((sqrt(vs)+sqrt(vr))/sqrt(xj[0]->delta)+xj[0]->value,sqrt(vs/xj[2]->delta)+xj[2]->value);

	if (min > max) return false;

	if (eval(xj,false,vr,true,vs,min) > 0.) return false;
	if (eval(xj,false,vr,true,vs,max) < 0.) return false;

	tt = bisect(xj,false,vr,true,vs,min,max);

	if (tt < 0.) return false;

	*res = tt;
	
	kappa2 = (tt-xj[2]->value)*(tt-xj[2]->value)*xj[2]->delta/vs;
	if (kappa2 > 1.-thres) return false;
	*al *= 1./(miu*sqrt(kappa2/(1.-kappa2))+1.);
	if (*al < thres) return false;

	return true;
    }

    if (*f == 6) {
	if (xj[0]->value >= xj[1]->value)
	    min = xj[0]->value;
	else
	    min = xj[1]->value;

	max = SF_MIN((sqrt(vs)+sqrt(vr))/sqrt(xj[0]->delta)+xj[0]->value,sqrt(vr/xj[1]->delta)+xj[1]->value);

	if (min > max) return false;

	if (eval(xj,true,vr,false,vs,min) > 0.) return false;
	if (eval(xj,true,vr,false,vs,max) < 0.) return false;

	tt = bisect(xj,true,vr,false,vs,min,max);

	if (tt < 0.) return false;

	*res = tt;
	
	kappa1 = (tt-xj[1]->value)*(tt-xj[1]->value)*xj[1]->delta/vr;
	if (kappa1 > 1.-thres) return false;
	*al *= 1./(miu*sqrt(kappa1/(1.-kappa1))+1.);
	if (*al < thres) return false;

	return true;
    }

    if (*f == 8) {
	temp[0] = (sqrt(vs)+sqrt(vr))/sqrt(xj[0]->delta)+xj[0]->value;
	temp[1] = sqrt(vr/xj[1]->delta)+xj[1]->value;

	if (temp[0] <= temp[1]) {
	    *res = temp[0];
	    *f = 1;
	    *al *= 1.;
	} else {
	    *res = temp[1];
	    *f = 2;
	    *al *= 0.;
	}

	return true;
    }
    
    if (*f == 9) {
	temp[0] = (sqrt(vs)+sqrt(vr))/sqrt(xj[0]->delta)+xj[0]->value;
	temp[1] = sqrt(vs/xj[2]->delta)+xj[2]->value;

	if (temp[0] <= temp[1]) {
	    *res = temp[0];
	    *f = 1;
	    *al *= 1.;
	} else {
	    *res = temp[1];
	    *f = 3;
	    *al *= 0.;
	}

	return true;
    }

    /* three-sided */
    if (*f == 7) {
	min = 0.;
	for (j=0; j < 3; j++) {
	    if (xj[j]->value > min)
		min = xj[j]->value;
	}       

	max = SF_MIN((sqrt(vs)+sqrt(vr))/sqrt(xj[0]->delta)+xj[0]->value,SF_MIN(sqrt(vr/xj[1]->delta)+xj[1]->value,sqrt(vs/xj[2]->delta)+xj[2]->value));

	if (min > max) return false;

	if (eval(xj,true,vr,true,vs,min) > 0.) return false;
	if (eval(xj,true,vr,true,vs,max) < 0.) return false;

	tt = bisect(xj,true,vr,true,vs,min,max);
	
	if (tt < 0.) return false;

	*res = tt;

	kappa1 = (tt-xj[1]->value)*(tt-xj[1]->value)*xj[1]->delta/vr;
	if (kappa1 > 1.-thres) return false;
	kappa2 = (tt-xj[2]->value)*(tt-xj[2]->value)*xj[2]->delta/vs;
	if (kappa2 > 1.-thres) return false;
	*al *= 1./(miu*sqrt(kappa1/(1.-kappa1))+miu*sqrt(kappa2/(1.-kappa2))+1.);
	if (*al < thres) return false;

	return true;
    }

    return false;
}

double bisect(struct Upd *xj[],
	      bool r, double vr,
	      bool s, double vs,
	      double min, double max)
/* quartic solve (bisection method) */
{
    int iloop=0;
    double left, right, middle, val;

    left = min;
    right = max;

    middle = 0.5*(left+right);    
    val = eval(xj,r,vr,s,vs,middle);

    while (fabs(val) > tol && iloop < nloop) {
	if (val <= 0.) {
	    left = middle;
	} else {
	    right = middle;
	}

	middle = 0.5*(left+right);
	val = eval(xj,r,vr,s,vs,middle);

	iloop++;
    }

    if (fabs(val) <= tol)
	return middle;
    else
	return -1.;
}

double eval(struct Upd *xj[],
	    bool r, double vr,
	    bool s, double vs,
	    double p)
/* evaluate funtional */
{
    double val;

    val = (p-xj[0]->value)*sqrt(xj[0]->delta);

    if (r) 
	val -= sqrt(vr-(p-xj[1]->value)*(p-xj[1]->value)*xj[1]->delta);
    else
	val -= sqrt(vr);

    if (s) 
	val -= sqrt(vs-(p-xj[2]->value)*(p-xj[2]->value)*xj[2]->delta);
    else
	val -= sqrt(vs);

    return val;
}
