#ifndef __prop_ricean_h__
#define __prop_ricean_h__

/***************************************************************************
 *
 *           Copyright 2000 by Carnegie Mellon University
 * 
 *                       All Rights Reserved
 * 
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appear in all copies and that
 * both that copyright notice and this permission notice appear in
 * supporting documentation, and that the name of CMU not be
 * used in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 * 
 * CMU DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
 * ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
 * CMU BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
 * ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
 * ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 * 
 * Author:  Ratish J. Punnoose
 * 
 ***************************************************************************/




#include <basetrace.h>
#include <packet-stamp.h>
#include <wireless-phy.h>
#include <propagation.h>
#include <tworayground.h>

class PropRicean : public TwoRayGround {
public:
	PropRicean();
	virtual double Pr(PacketStamp *tx, PacketStamp *rx, WirelessPhy *ifp);
	virtual int command(int argc, const char*const* argv);
	~PropRicean();

protected:
	int LoadDataFile(const char *filename);

	/*
	 * Configured via TCL
	 */
	double  max_velocity;       /* Maximum velocity of vehicle/objects in 
				   environment.  Used for computing doppler */

	/* Internal values */
	int N;                  /* Num points in table */
	float fm0;              /* Max doppler freq in table */
	float fm;               /* Max doppler freq in scenario */
	float fs;               /* Sampling rate */
	float dt;               /* Sampling period = 1/fs */
	
	float K;                /* Ricean K factor */


	float *data1;           /* Data values for inphase and quad phase */
	float *data2;
	int initialized;
	int rice_max_node_id;  	/* Used for computing the table offset*/

	BaseTrace  *trtarget_;

	void trace(char *fmt, ...);
};


#endif /* __cmu_prop_ricean_h__ */
