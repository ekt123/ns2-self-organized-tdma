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



#include <math.h>
#include <stdio.h>
#include <stdarg.h>

#include <delay.h>
#include <packet.h>

#include <packet-stamp.h>
#include <antenna.h>
#include <mobilenode.h>

#include <wireless-phy.h>
#include <propagation.h>
#include <prop_ricean.h>

void
PropRicean::trace(char* fmt, ...)
{
	va_list ap;

	if(trtarget_) {
		assert(trtarget_);
		
		va_start(ap, fmt);
		vsprintf(trtarget_->buffer(), fmt, ap);
		trtarget_->dump();
		va_end(ap);
	}
}



static class PropRiceanClass: public TclClass {
public:
        PropRiceanClass() : TclClass("Propagation/Ricean") {}
        TclObject* create(int, const char*const*) {
                return (new PropRicean);
        }
} class_prop_ricean;


PropRicean::PropRicean() : TwoRayGround()
{

	N = 0;
	fm0 = fs = dt = fm = 0.0;
	K = 0.0; max_velocity = 0.0;
	data1 = data2 = 0;
	initialized = 0;
	rice_max_node_id = 0;

	trtarget_ = 0;
}

PropRicean::~PropRicean()
{
	if(initialized) {
		delete data1; delete data2;
		initialized = 0;
		N = 0;
		fm0 = fs = dt = fm = 0.0;
		K = 0.0; max_velocity = 0.0;
		trtarget_ = 0;
	}
}


/* ======================================================================
   Public Routines
   ====================================================================== */
int
PropRicean::command(int argc, const char*const* argv)
{
	TclObject *obj;
	int rc;

	if(argc == 3) {
		if (strcmp(argv[1], "LoadRiceFile") == 0)  {
			rc =  LoadDataFile(argv[2]);
			if(rc == TCL_OK)
				initialized = 1;
			return rc;
                }
		if (strcmp(argv[1], "MaxVelocity") == 0) {
			if(initialized) {
				fprintf(stderr, 
					"Prop_Ricean: Specify \
 MaxVelocity Parameter before LoadFile\n");
				return TCL_ERROR;
			}
			else {
				max_velocity = atof(argv[2]);

				return TCL_OK;
			}
		}
		if (strcmp(argv[1], "RiceanKdB") == 0) {
			float K_dB;
			K_dB = atof(argv[2]);

			K = pow(10.0, K_dB/10.0);
			
			return TCL_OK;
		}
		if (strcmp(argv[1], "RiceanK") == 0) {
			K = atof(argv[2]);

			return TCL_OK;
		}
		if (strcmp(argv[1], "RiceMaxNodeID") == 0) {
			rice_max_node_id = atoi(argv[2]);

			return TCL_OK;
		}
		if (strcmp(argv[1], "tracetarget") == 0) {
			
			if( (obj = TclObject::lookup(argv[2])) == 0) {
				fprintf(stderr,
					"Propagation: %s lookup of %s failed\n",
					argv[1], argv[2]);
				return TCL_ERROR;
			}
			trtarget_ = (BaseTrace *) obj;
			assert(trtarget_);
			
			return TCL_OK;
		}
	


	}

	return TwoRayGround::command(argc, argv);
}



#define BUF_SZ 4096
int PropRicean::LoadDataFile(const char *filename)
{
	char buf[BUF_SZ];
	char arg1[BUF_SZ];
	char arg2[BUF_SZ];

	char *ret;
	FILE *fdstream;
	float *tmp1, *tmp2;
	int rc = TCL_ERROR, ret_val;
	int k = 0;



	fdstream = fopen(filename, "r");
	if(!fdstream) {
		printf("%s : ", filename);
		perror("file open ");
		goto quit;
	}

	while(1) {
		ret = fgets(buf, BUF_SZ-1, fdstream);
		if(!ret)
			goto quit;
		if( buf[0] == '#')
			continue;

		sscanf(buf, "%s %s", arg1, arg2);

		if(!strcmp(arg1, "DATA")) {
			printf("found DATA\n");
			break;
		}


		if(!strcmp(arg1, "fm")) {
			fm0 = atof(arg2);
		} else if(!strcmp(arg1, "fs")) {
			fs = atof(arg2);
		} else if(!strcmp(arg1, "N")) {
			N = atoi(arg2);
		} 


	}
	
	data1 = new float[N];
	data2 = new float[N];

	tmp1 = data1;
	tmp2 = data2;

	for(k=0; k<N; k++) {
		ret_val = fscanf(fdstream, "%f %f", tmp1++, tmp2++);
		if(ret_val != 2) {
			printf("input error\n");
			goto quit;
		}
	}

	rc = TCL_OK;
			
 quit:
	printf("%d data points read\n", k);
	printf("fm0 = %f fm = %f  fs = %f\n", fm0, fm, fs);

//  	for(int m = 0; m < k; m++) 
//  		printf("%.5e\t%.5e\n", data1[m], data2[m]);


	return rc;

}



double
PropRicean::Pr(PacketStamp *tx, PacketStamp *rx, WirelessPhy *ifp)
{
	double Pr, Pr_Rice=0.0, Pr_tot;
	int table_offset = 0;  /* Compute a unique table offset
				  for unique pairs of nodes */
	
	Pr = TwoRayGround::Pr(tx, rx, ifp);

	if( initialized) { /* Ricean loss */
		// double A, inph, quad, Anew;
		double time_index;
		double envelope_fac, tmp_x1, tmp_x2, x1_interp, x2_interp;
		int n1id, n2id, tmp;

		/* Has the max node id parameter been set?
		   This is needed to compute the table offset */
		if (rice_max_node_id) {
			n1id = tx->getNode()->nodeid();
			n2id = rx->getNode()->nodeid();
			
			/* Find the maximum of the two */
			if (n1id < n2id) {
				tmp = n1id;
				n1id = n2id;
				n2id = tmp;
			}

			table_offset = int(floor(
				double((n1id-1)*N)/
				double(rice_max_node_id) +  
				double((n2id)*N)/
				double(rice_max_node_id*rice_max_node_id)));
				
		} 

		fm = max_velocity / ifp->getLambda();

		time_index = ( Scheduler::instance().clock() *
				        fs *   fm / fm0) ;

		/* Create an offset into the table, that is dependent
		   per node pair */
		time_index += table_offset;

		time_index = time_index -
			double(N)*floor(time_index/double(N));


		/* New Stuff */
		{
			/* Do envelope interpolation using Legendre
			   polynomials */
			double X0, X1, X2, X3;
			int ind0, ind1, ind2, ind3;
			
			ind1 = int(floor(time_index));
			ind0 = (ind1-1+N) % N;
			ind2 = (ind1+1) % N;
			ind3 = (ind1+2) % N;
			
			X1 = time_index - ind1;
			X0 = X1+1.0;
			X2 = X1-1.0;
			X3 = X1-2.0;

			x1_interp = data1[ind0]*X1*X2*X3/(-6.0) +
				data1[ind1]*X0*X2*X3*(0.5) +
				data1[ind2]*X0*X1*X3*(-0.5) +
				data1[ind3]*X0*X1*X2/6.0;
				
			x2_interp = data2[ind0]*X1*X2*X3/(-6.0) +
				data2[ind1]*X0*X2*X3*(0.5) +
				data2[ind2]*X0*X1*X3*(-0.5) +
				data2[ind3]*X0*X1*X2/6.0;
			

		}

		/* Find the envelope multiplicative factor */
		tmp_x1 = x1_interp + sqrt(2.0 * K);
		tmp_x2 = x2_interp;

		envelope_fac = (tmp_x1*tmp_x1 + tmp_x2*tmp_x2) / 
			(2.0 * (K+1)); 

		//Pr_Rice = 10.0 * log10(envelope_fac);
		Pr_Rice = envelope_fac;
	}

	
	//Pr_tot = Pr + Pr_Rice;
	Pr_tot = Pr * Pr_Rice;


	if( trtarget_) {
		double tX, tY, tZ, rX, rY, rZ;
		tx->getNode()->getLoc(&tX, &tY, &tZ);
		rx->getNode()->getLoc(&rX, &rY, &rZ);

		trace("PR %.12f _%d_ _%d_  %6d\tPRP  1  RIC  %.4f %.4f %.4f  -->  %.4f %.4f %.4f  :  %.3f  %.3f  %.3f", 
		      Scheduler::instance().clock(),
		      tx->getNode()->nodeid(),
		      rx->getNode()->nodeid(),
		      table_offset,
		      tX, tY, tZ, rX, rY, rZ, 
		      10.0*log10(Pr_tot), 
		      10.0*log10(Pr), 
		      10.0*log10(Pr_Rice) );

	}

	return Pr_tot;
}


