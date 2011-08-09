/* -*-	Mode:C++; c-basic-offset:8; tab-width:8; indent-tabs-mode:t -*- */
/*
 * Developed by: Ankur Khandelwal, Siddhartha Saha, Ashwini Kumar, Sabyasachi Roy @ IIT-Kanpur
 *
 */
/* omni-antenna.h
   omni-directional antenna
*/

#ifndef ns_dirantenna_h
#define ns_dirantenna_h

#include <antenna.h>

class DirAntenna : public Antenna {

public:
  DirAntenna();
  
	virtual double getTxGain(double, double, double, double);
	virtual double getRxGain(double, double, double, double);
  
        virtual Antenna * copy()
	// return a pointer to a copy of this antenna that will return the 
	// same thing for get{Rx,Tx}Gain that this one would at this point
	// in time.  This is needed b/c if a pkt is sent with a directable
	// antenna, this antenna may be have been redirected by the time we
	// call getTxGain on the copy to determine if the pkt is received.
        {
            // since the Gt and Gr are constant w.r.t time, we can just return
            // this object itself
            return this;
        }

        virtual void release()
        // release a copy created by copy() above
        {
                  // don't do anything
        }

/* BEGIN<Sabya,Ashwini> */
	
 virtual int command(int argc, const char*const* argv);
 int getangle(){ return Angle_;}
 int getwidth(){ return Width_;}
 int getLA(){ return lowerAngle;}
 int getUA(){ return upperAngle;}
 double getSAR(){ return solidAngleRatio;}
 void setSAR();
 void initialize_radiation_pattern();

/* END<Sabya,Ashwini> */ 

protected:
	double Gt_;		       // gain of transmitter (db)
	double Gr_;		       // gain of receiver (db)
	int Angle_;                    // orientation of the antenna (from the positive X-axis) (degrees) 
	int Width_;                    // this is the width of the dir-antenna, basically, the angular range to which it can transfer.

/* BEGIN<Sabya,Ashwini> */
	
	int Type_;		       // Specifying the "type" of directional antenna to use. Type 0 implies the width should also be specified 
	
	float gainVals[360];  // Stores the gain values for this antenna

	/* END<Sabya,Ashwini> */ 
private:
	int lowerAngle;
	int upperAngle;
	double solidAngleRatio;

};


#endif // ns_dirantenna_h
