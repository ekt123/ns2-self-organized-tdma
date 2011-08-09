
/* -*-	Mode:C++; c-basic-offset:8; tab-width:8; indent-tabs-mode:t -*- */
/*
 * Developed by:- Ankur Khandelwal : Siddhartha Saha : Ashwini Kumar : Sabyasachi Roy @ IIT-Kanpur
 *
 * dir-antenna.cc
   $Id: dir-antenna.cc,v 1.4 2005/02/07 15:10:05 bhaskar Exp $
   */

#include <dir-antenna.h>
#include <math.h>


static class DirAntennaClass : public TclClass {
public:
  DirAntennaClass() : TclClass("Antenna/DirAntenna") {}
  TclObject* create(int, const char*const*) {
    return (new DirAntenna);
  }
	
} class_DirAntenna;

/* BEGIN<Sabya,Ashwini> */

double DirAntenna::getRxGain(double x, double y, double z, double lambda)
  // return the gain for a signal from a node at vector dX, dY, dZ
  // from the receiver at wavelength lambda
{
 
/*=======================================================================================
 */
	/*
	 * For now, just return 1.0 if the ray (x,y) falls on the cone defined by
	 * (angle, angle+width), zero otherwise
	 */
double angle;
double dist_ = sqrt(x*x+y*y+z*z);
if(dist_==0.0)
{
	return 1.0;
}	
angle = atan2(y,x);
/* angle is angle from -PI to +PI, inclusive, in radian,
 * convert to degrees
 */
angle = angle*180/M_PI;
/* Bring it to 0-360 range */
if(angle < 0) angle += 360;
if(Type_ == 0)
{		
	/* Now to see if it lies inside the cone :
	 */
	Gr_ = 0.0;
	if(upperAngle > lowerAngle){
		/* Normal case*/
		if(angle >= lowerAngle && angle <= upperAngle) Gr_ = 1.0;
	}else if(upperAngle < lowerAngle){
		/* Like lowerAngle = 350 and upper angle = 10 */
		if(angle >= lowerAngle || angle <= upperAngle) Gr_ = 1.0;
	}else{
		/* both are equal, => 360 width */
		Gr_ = 1.0;
	}
	/* A wild hack, multiply the gain with the ratio of the solid angles */
	
	return Gr_*solidAngleRatio;
}
else
{
	double diff_angle;
	diff_angle = angle - lowerAngle;
	if(diff_angle < 0)
	{
		while(diff_angle < 0)
			diff_angle = diff_angle + 360;
	}
	if(diff_angle >= 360)
	{
		while(diff_angle > 360)
			diff_angle = diff_angle - 360;
	}
	double gain;
	gain = (gainVals[((int)diff_angle)%360] + gainVals[(((int)diff_angle)+1)%360])/2.0;
	//	fprintf(stderr,"RX Gain %f %f\n",diff_angle,gainVals[((int)diff_angle)%360]);
	return (pow(10.0,gain/10));

}	
}


double DirAntenna::getTxGain(double x, double y, double z, double lambda)
  // return the gain for a signal to a node at vector dX, dY, dZ
  // from the transmitter at wavelength lambda
{

/*=======================================================================================
 */
	/*
	 * For now, just return 1.0 if the ray (x,y) falls on the cone defined by
	 * (angle, angle+width), zero otherwise
	 */
	
double angle;
double dist_ = sqrt(x*x+y*y+z*z);
if(dist_==0.0)
{
	return 1.0;
}	
angle = atan2(y,x);
/* angle is angle from -PI to +PI, inclusive, in radian,
 * convert to degrees
 */
angle = angle*180/M_PI;
/* Bring it to 0-360 range */
if(angle < 0) angle += 360;

if(Type_ == 0)
{	
	

	/* Now to see if it lies inside the cone :
	 */
	Gt_ = 0.0;
	if(upperAngle > lowerAngle){
		/* Normal case*/
		if(angle >= lowerAngle && angle <= upperAngle) Gt_ = 1.0;
	}else if(upperAngle < lowerAngle){
		/* Like lowerAngle = 350 and upper angle = 10 */
		if(angle >= lowerAngle || angle <= upperAngle) Gt_ = 1.0;
	}else{
		/* both are equal, => 360 width */
		Gt_ = 1.0;
	}
	/* A wild hack, multiply the gain with the ratio of the solid angles */

	
	return Gt_*solidAngleRatio;
}	
else
{
	double diff_angle;
	diff_angle = angle - lowerAngle;
	if(diff_angle < 0)
	{
		while(diff_angle < 0)
			diff_angle = diff_angle + 360;
	}
	if(diff_angle >= 360)
	{
		while(diff_angle > 360)
			diff_angle = diff_angle - 360;
	}
	double gain;
	gain = (gainVals[((int)diff_angle)%360] + gainVals[(((int)diff_angle)+1)%360])/2.0;
	return (pow(10.0,gain/10));
}
}

/* END<Sabya,Ashwini> */

DirAntenna::DirAntenna()
{
 
	 char *antn;
	 char *token;
	 int i=0;
	 Gt_ = 1.0;
	 Gr_ = 1.0;
	 Angle_ = 0;
	 bind("Gt_", &Gt_);
	 bind("Gr_", &Gr_);
	 bind("Angle_",&Angle_);
	 bind("Width_",&Width_);
	
	 /* BEGIN<Sabya,Ashwini> */

	 bind("Type_",&Type_);
	 
	 /* END<Sabya,Ashwini> */

	 /* calculate lower and upper angle */
	 lowerAngle = Angle_;
	 while(lowerAngle < 0){
		 lowerAngle += 360;
	 }
	 upperAngle = lowerAngle + Width_;
	 while(upperAngle < 0){
		 upperAngle += 360;
	 }
	 
	 upperAngle %= 360;
	 lowerAngle %= 360;
	 solidAngleRatio = 2/(1 - cos(M_PI*Width_/360));
	 
	 /* BEGIN<Sabya,Ashwini> */

	 // Checking for the antenna type given
	 if (Type_ > 8 || Type_ < 0)
	 {
		 printf("Invalid antenna type given..should be between 0 & 8;\n");
		 exit(1);
	 }
	 if(Type_ != 0)
		 initialize_radiation_pattern();

	 /* END<Sabya,Ashwini> */

	 
	 
}

int DirAntenna::command(int argc, const char*const* argv)
{
	TclObject *obj; 

	if(argc == 3) {
		if (strcasecmp(argv[1], "setAngle") == 0) {
			Angle_ = atoi(argv[2]);
			setSAR();
			return TCL_OK;
		}
		if (strcasecmp(argv[1], "setWidth") == 0) {
			Width_ = atoi(argv[2]);
			setSAR();
			return TCL_OK;
		}
		
		/* BEGIN<Sabya,Ashwini> */

		if(strcasecmp(argv[1], "setType") == 0) {
			Type_ = atoi(argv[2]);
			if(Type_ > 8 || Type_ < 0)
			{	
				printf("Erroneous Type Input\n");
				exit(0);
			}	
			if(Type_ != 0)
				initialize_radiation_pattern();
			return TCL_OK;
		}
		/* END<Sabya,Ashwini> */

		if (strcasecmp(argv[1], "setX") == 0) {
			X_ = atof(argv[2]);
			return TCL_OK;
		}
		if (strcasecmp(argv[1], "setY") == 0) {
			Y_ = atof(argv[2]);
			return TCL_OK;
		}
		if (strcasecmp(argv[1], "setZ") == 0) {
			Z_ = atof(argv[2]);
			return TCL_OK;
		}
	}
	return Antenna::command(argc,argv);
}

void DirAntenna::setSAR()
{
	//printf(" the current value of antenna parameters:\n");
	//printf("------------------------------------------\n");
	//printf("angle=%d, width=%d, solidangle=%f lower=%d upper=%d\n",getangle(),getwidth(),getSAR(),getLA(),getUA());
	lowerAngle = Angle_;
	 while(lowerAngle < 0){
		 lowerAngle += 360;
	 }
	 upperAngle = lowerAngle + Width_;
	 while(upperAngle < 0){
		 upperAngle += 360;
	 }
	 
	 upperAngle %= 360;
	 lowerAngle %= 360;
	 solidAngleRatio = 2/(1 - cos(M_PI*Width_/360));
	 
}

/* BEGIN<Sabya,Ashwini> */

void DirAntenna::initialize_radiation_pattern()
{
	FILE *fp;
	char *radiation_file;
	//radiation_file = (char *)malloc(256);
	radiation_file = getenv("NS_ANTENNA_FILE");
	if(radiation_file == NULL) {
		fprintf(stderr, "Env variable NS_ANTENNA_FILE has to be set, see documentation (?)\n");
		exit(65);
	}
	fp = fopen(radiation_file,"r");
	if(fp == NULL) {
		perror("fopen");
		fprintf(stderr, "Can't open %s\n", radiation_file);
		exit(34);
	}
	char str[1024];
	int type;
	int angle;
	float gain;
	
	while(!feof(fp)){
		fgets(str,1024,fp);
		sscanf(str,"%d%d%f",&type,&angle,&gain);
		if(type+1 == Type_){
			gainVals[angle] = gain;
		}
	}
}	

/* END<Sabya,Ashwini> */

