/* 2D Hybrid Radon transform - diffractions + reflections in time domain */
/*
  Copyright (C) 2012 University of Texas at Austin
  
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
#include "aastretch.h"

// data parameters
static int tn_;    static float to_, td_;
static int dipn_;  static float dipo_, dipd_;
// model parameters
static int xin_;   static float xio_, xid_;
static int dip0n_; static float dip0o_, dip0d_;

static float *tmp, *amp, *str, *tx;

static float anti_;
static int   invMod_;
static int   tLim_;

void ditime2d_init (float dipo,  float dipd,  int dipn,  // dip angle axis 
				    float xio,   float xid,   int xin,   // xi axis
			  	    float dip0o, float dip0d, int dip0n, // refl dip axis
		  		    float to,    float td,    int tn,    // time axis 
		  			float anti,                          // antialiasing
					int invMod) 						 
/*< initialize >*/
{

	to_   = to;   td_   = td;   tn_   = tn;  
    dipo_ = dipo; dipd_ = dipd; dipn_ = dipn;  

    dip0o_ = dip0o; dip0d_ = dip0d; dip0n_ = dip0n;  
    xio_  = xio; xid_  = xid;  xin_  = xin;  

	tLim_ = to_ + td_ * (tn_ - 1);

	invMod_ = invMod;
	anti_   = anti;

    aastretch_init  (false, tn_, to_, td_, tn_);
    sf_halfint_init (true, 2 * tn_, 1.f - 1.f / tn_);

    amp = sf_floatalloc (tn_);
    str = sf_floatalloc (tn_);
    tx  = sf_floatalloc (tn_);
    tmp = sf_floatalloc (tn_);

	return;
}

void ditime2d_close (void)
/*< free allocated storage >*/
{
    free(amp);
    free(str);
    free(tx);
    free(tmp);

    aastretch_close();
    sf_halfint_close();
}

void ditime2d_lop (bool adj, bool add, int nm, int nd, 
		  		   float *modl, float *data) 
/*< operator >*/
{
    switch (invMod_) {
		case 0:
		    if (nm != tn_ * xin_ || nd != tn_ * dipn_) 
			sf_error ("%s: wrong dimensions nm=%d nd=%d",__FILE__, nm, nd);
		    break;
		case 1:
		    if (nm != tn_ * dip0n_ || nd != tn_ * dipn_) 
			sf_error ("%s: wrong dimensions nm=%d nd=%d",__FILE__, nm, nd);
		    break;
		case 2:
		    if (nm != tn_ * (xin_ + dip0n_) || nd != tn_ * dipn_) 
			sf_error ("%s: wrong dimensions nm=%d nd=%d %d %d %d ",__FILE__, nm, nd, tn_, xin_, dip0n_);
		    break;
    }

    sf_adjnull (adj, add, nm, nd, modl, data);

    for (int id = 0; id < dipn_; ++id) { 
		const float curDip = dipo_ + id * dipd_;
		const float a      = curDip * SF_PI / 180.f;
		const float cos_a  = cos (a);
		const float sin_a  = sin (a);

		// diffracion part
		if (1 != invMod_) {	
		    for (int ixi = 0; ixi < xin_; ++ixi) { 
				const float curXi = xio_ + ixi * xid_;
	
				for (int it = 0; it < tn_; ++it) { 
				    const float curTime = to_ + it * td_;
				    const float t = curTime * (curXi * sin_a + sqrt ( curXi * curXi + cos_a * cos_a ) ) / cos_a;			    

				    str [it] = t;
//				    tx [it] = anti * fabsf (p - p1) * dx;
					tx[it] = 0.f;
				    amp [it] = 1.;
				}
		
				aastretch_define (str, tx, amp);
		
			    sf_chain (sf_halfint_lop, aastretch_lop,
					      adj, true, tn_, tn_, tn_, modl + ixi * tn_, data + id * tn_, tmp);
	
		    }
		}

		// reflection part
		if (0 != invMod_) {
		    const int offset = (1 == invMod_) ? 0 : xin_;

		    for (int id0 = 0; id0 < dip0n_; ++id0) { 
				const int   curDip0 = dip0o_ + id0 * dip0d_;
				const float a0 = curDip0 * SF_PI / 180.f;
				const float cos_a0 = cos (a0);	
				const float sin_a0 = sin (a0);	
		
				for (int it = 0; it < tn_; ++it) {		
				    const float t0 = to_ + it * td_;
				    const float t = t0 * cos_a * cos_a0 / (1 - sin_a * sin_a0);
		    
				    if (t > 0. && t < tLim_) {

						str[it] = t;
						tx[it] = 0.f;
	//					tx[it] = fabsf(anti*sx)/t;
						float w = 1.f; /* can be changed later */
						amp[it] = 1.f;
				    } else {
						str[it] = t0 - 2. * td_;
						tx[it] = 0.;
						amp[it] = 0.;
				    }		
				}
		
				aastretch_define (str, tx, amp);

			    sf_chain (sf_halfint_lop,aastretch_lop,
					      adj,true, tn_, tn_, tn_, modl + (offset + id0) * tn_, data + id * tn_, tmp);
		    }
		}
	}
}