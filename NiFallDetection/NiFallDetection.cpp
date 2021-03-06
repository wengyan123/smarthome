
//---------------------------------------------------------------------------
// Includes
//---------------------------------------------------------------------------
#include <XnCppWrapper.h>
#include <sys/time.h>
#include <math.h> 
#include<signal.h>
#include<unistd.h>
#include <string.h>

//---------------------------------------------------------------------------
// Defines
//---------------------------------------------------------------------------
#define SAMPLE_XML_PATH "../../Config/SamplesConfig.xml"
#define SAMPLE_XML_PATH_LOCAL "SamplesConfig.xml"

//---------------------------------------------------------------------------
// Globals
//---------------------------------------------------------------------------

xn::Context g_Context;
xn::ScriptNode g_scriptNode;
xn::DepthGenerator g_DepthGenerator;
xn::UserGenerator g_UserGenerator;

XnBool g_bNeedPose = FALSE;
XnChar g_strPose[20] = "";

#define MAX_NUM_USERS 15


//---------------------------------------------------------------------------
//New globsls ----------------------------Fall detection 
//---------------------------------------------------------------------------

#define NUM_JOINTS 22          //number of joints 
#define MAX_POSITIVE 99999
#define MIN_NEGATIVE -99999



double Xmax = MIN_NEGATIVE;
double Xmin = MAX_POSITIVE;
double Ymax = MIN_NEGATIVE;
double Ymin = MAX_POSITIVE;
double Zmax = MIN_NEGATIVE;
double Zmin = MAX_POSITIVE;
 
//tables for saving all x/y/z of  22 joints 
double X_table[NUM_JOINTS];
double Y_table[NUM_JOINTS];
double Z_table[NUM_JOINTS];

//current position (Pi)
double width_X;
double height_Y;
double depth_Z;

//position before(Pi-1) 
double old_width_X;
double old_height_Y;
double old_depth_Z;

double old_WD;
double WD;

double v_H;
double v_WD;

//timer
struct timeval timeNew, timeOld;

//-------------value for fall detection  algorithm-------------------------//

//Threshold
#define TvH 1.18
#define TvWD 1.20
#define TiH 0.1
#define falling 8
#define inactivity 10
#define Tcounter_same_a 8

//counter a and b ; a is the falling counter , b is inactivity counter 
int a=0, b=0;


bool activityDetection=false;
bool inactivityDetection=false;
bool fallDetected=false;

//these for avoid wrong  detection  of fall and reinitislise the counter a
int old_a=0;
int counter_same_a=0;

//file pointer for data out 
FILE * fp_data;



//---------------------------------------------------------------------------
// Code
//---------------------------------------------------------------------------

//file cofig
XnBool fileExists(const char *fn)
{
	XnBool exists;
	xnOSDoesFileExist(fn, &exists);
	return exists;
}

// Callback: New user was detected
void XN_CALLBACK_TYPE User_NewUser(xn::UserGenerator& /*generator*/, XnUserID nId, void* /*pCookie*/)
{
    XnUInt32 epochTime = 0;
    xnOSGetEpochTime(&epochTime);
    printf("%d New User %d\n", epochTime, nId);
    // New user found
    if (g_bNeedPose)
    {
        g_UserGenerator.GetPoseDetectionCap().StartPoseDetection(g_strPose, nId);
    }
    else
    {
        g_UserGenerator.GetSkeletonCap().RequestCalibration(nId, TRUE);
    }
}
// Callback: An existing user was lost
void XN_CALLBACK_TYPE User_LostUser(xn::UserGenerator& /*generator*/, XnUserID nId, void* /*pCookie*/)
{
    XnUInt32 epochTime = 0;
    xnOSGetEpochTime(&epochTime);
    printf("%d Lost user %d\n", epochTime, nId);	
}
// Callback: Detected a pose
void XN_CALLBACK_TYPE UserPose_PoseDetected(xn::PoseDetectionCapability& /*capability*/, const XnChar* strPose, XnUserID nId, void* /*pCookie*/)
{
    XnUInt32 epochTime = 0;
    xnOSGetEpochTime(&epochTime);
    printf("%d Pose %s detected for user %d\n", epochTime, strPose, nId);
    g_UserGenerator.GetPoseDetectionCap().StopPoseDetection(nId);
    g_UserGenerator.GetSkeletonCap().RequestCalibration(nId, TRUE);
}
// Callback: Started calibration
void XN_CALLBACK_TYPE UserCalibration_CalibrationStart(xn::SkeletonCapability& /*capability*/, XnUserID nId, void* /*pCookie*/)
{
    XnUInt32 epochTime = 0;
    xnOSGetEpochTime(&epochTime);
    printf("%d Calibration started for user %d\n", epochTime, nId);
}

void XN_CALLBACK_TYPE UserCalibration_CalibrationComplete(xn::SkeletonCapability& /*capability*/, XnUserID nId, XnCalibrationStatus eStatus, void* /*pCookie*/)
{
    XnUInt32 epochTime = 0;
    xnOSGetEpochTime(&epochTime);
    if (eStatus == XN_CALIBRATION_STATUS_OK)
    {
        // Calibration succeeded
        printf("%d Calibration complete, start tracking user %d\n", epochTime, nId);		
        g_UserGenerator.GetSkeletonCap().StartTracking(nId);
    }
    else
    {
        // Calibration failed
        printf("%d Calibration failed for user %d\n", epochTime, nId);
        if(eStatus==XN_CALIBRATION_STATUS_MANUAL_ABORT)
        {
            printf("Manual abort occured, stop attempting to calibrate!");
            return;
        }
        if (g_bNeedPose)
        {
            g_UserGenerator.GetPoseDetectionCap().StartPoseDetection(g_strPose, nId);
        }
        else
        {
            g_UserGenerator.GetSkeletonCap().RequestCalibration(nId, TRUE);
        }
    }
}


#define CHECK_RC(nRetVal, what)					    \
    if (nRetVal != XN_STATUS_OK)				    \
{								    \
    printf("%s failed: %s\n", what, xnGetStatusString(nRetVal));    \
    return nRetVal;						    \
}



//*************************************************************************

//fall detection  code
 
//*************************************************************************


//
//This function is to get all information of all(22) joints 
//
void getJointsInfo(XnUserID player, XnSkeletonJoint eJoint, int& rIndex){
     
       if (!g_UserGenerator.GetSkeletonCap().IsTracking(player))
	{
		printf("not tracked!\n");
		return;
	}

	if (!g_UserGenerator.GetSkeletonCap().IsJointActive(eJoint))
	{
		return;
	}
    
    
     XnSkeletonJointTransformation joint;
    
     // get joint information, this joint information has two value 1-confidence, this present accuracy of a joint we will use then; 2-position including x y Z
     g_UserGenerator.GetSkeletonCap().GetSkeletonJoint(player, eJoint, joint);
   
     // the confidence of a joint present between 0 to 1, in  our case i choose 0.5, only the confidence of a joint superior 0.5 we will use it ,or we consider that's corrupt/inaccuracy
 	if (joint.position.fConfidence < 0.5)
	{
          // printf("This joint got error, we ignore this joint!!!!!\n");
           if(rIndex < (NUM_JOINTS-1)) rIndex++;
           return;
	}
    
       //if that's a joint usable, we save value x y z in tables
         X_table[rIndex] = joint.position.position.X;
         Y_table[rIndex] = joint.position.position.Y;
         Z_table[rIndex] = joint.position.position.Z;
             
         
         if(rIndex < (NUM_JOINTS-1))rIndex++;
     
         if(rIndex==(NUM_JOINTS-1)) rIndex=0;
  }


//
// this function is to calcul some pre-value , which we will use in algo falling detection
//
void get_XYZ(){

    int j;
             //find  min and max of X/Y/Z 
            for(j=0; j < NUM_JOINTS ; j++){
                if(X_table[j] > Xmax) Xmax=X_table[j];
                if(X_table[j] < Xmin) Xmin=X_table[j];
                
                if(Y_table[j] > Ymax) Ymax=Y_table[j];
                if(Y_table[j] < Ymin) Ymin=Y_table[j];
                
                if(Z_table[j] > Zmax) Zmax=Z_table[j];
                if((Z_table[j]!=0) && (Z_table[j] < Zmin)) Zmin=Z_table[j];
                  
               
             }
            
             printf ("max X is : %6.2f \n", Xmax);
             printf ("min X is : %6.2f \n", Xmin);
             printf ("max Y is : %6.2f \n", Ymax);
             printf ("min Y is : %6.2f \n", Ymin);
             printf ("max Z is : %6.2f \n", Zmax);
             printf ("min Z is : %6.2f \n", Zmin);
             
             //+old , to save position(i-1)
               old_width_X=width_X;
               old_height_Y=height_Y;
               old_depth_Z= depth_Z;
               
             //new  , calcul new/current width/height/depeth
                width_X = abs(Xmin - Xmax);
                height_Y = abs(Ymin - Ymax);
                depth_Z = abs(Zmin - Zmax);
              
             printf ("width X is : %6.2f  \n", width_X);  
             printf ("height Y is : %6.2f \n", height_Y);
             printf ("depth Z is : %6.2f  \n", depth_Z);
          
             fprintf (fp_data,"width X is : %6.2f  \n", width_X);  
             fprintf (fp_data,"height Y is : %6.2f \n", height_Y);
             fprintf (fp_data,"depth Z is : %6.2f  \n", depth_Z);
             
             //+old , same way for width-depth
             old_WD=WD;
             WD= sqrt(depth_Z*depth_Z + width_X*width_X);
             
             
             
           
            //get current system time
            timeOld=timeNew;
            gettimeofday(&timeNew, NULL);
            
            //reinitialze
            Xmax = MIN_NEGATIVE;      
            Xmin = MAX_POSITIVE;
            Ymax = MIN_NEGATIVE;
            Ymin = MAX_POSITIVE;
            Zmax = MIN_NEGATIVE;
            Zmin = MAX_POSITIVE;

}

//
//This function apply the fall detection algorithm 
//

void detectFalling(){
    
    printf ("old WD is : %6.2f \n", old_WD);  
    printf ("new WD is : %6.2f \n", WD);  
    printf ("old H is : %6.2f  \n", old_height_Y);
    printf ("new H is : %6.2f  \n", height_Y);
    
    fprintf (fp_data,"old WD is : %6.2f \n", old_WD);  
    fprintf (fp_data,"new WD is : %6.2f \n", WD);  
    fprintf (fp_data,"old H is : %6.2f \n", old_height_Y);
    fprintf (fp_data,"new H is : %6.2f  \n", height_Y);
    
    printf ("Tn - To : %f us\n "  , ((timeNew.tv_sec*1000000 + timeNew.tv_usec)-(timeOld.tv_sec*1000000 + timeOld.tv_usec)));
   
    
    //volocity of height 
    v_H=((height_Y - old_height_Y)/((timeNew.tv_sec*1000000 + timeNew.tv_usec)-(timeOld.tv_sec*1000000 + timeOld.tv_usec)))*1000;
    printf ("v_H is : %6.2f m/s\n", v_H);
    fprintf (fp_data,"v_H is : %6.2f m/s\n", v_H);
    
    //volocity of width-depth
    v_WD=((WD - old_WD)/((timeNew.tv_sec*1000000 + timeNew.tv_usec)-(timeOld.tv_sec*1000000 + timeOld.tv_usec)))*1000;
    printf ("v_WD is : %6.2f m/s\n", v_WD);
    fprintf (fp_data,"v_WD is : %6.2f m/s\n", v_WD);
   
    
    
    /********fall detection algorithm *************/
  
    if ((abs(v_H) > TvH) && (v_WD > TvWD)){
        //time of falling = 9 frames, not 8 frames 
        if (a==falling){
            activityDetection=true;
            
            printf("activityDetection\n");
            fprintf(fp_data,"activityDetection\n");
        }
        a++;
    }
    
    if ((activityDetection==true)&& (abs(v_H)< TiH)){
        if(b==inactivity){
            inactivityDetection=true;
            fallDetected=true;
            
             printf("inactivityDetection\n");
             printf("fallDetected\n");
             
             
             fprintf(fp_data,"inactivityDetection\n");
             fprintf(fp_data,"fallDetected\n");
        }
        b++;
    }
    
      printf("conter a : %d\n", a);
      printf("conter b : %d\n", b);
      printf("conter old_a : %d\n", old_a);
      printf("conter counter_same_a : %d\n", counter_same_a);
      
      
      fprintf(fp_data,"conter a : %d\n", a);
      fprintf(fp_data,"conter b : %d\n", b);
      fprintf(fp_data,"conter old_a : %d\n", old_a);
      fprintf(fp_data,"conter counter_same_a : %d\n", counter_same_a);
      
      
      //avoid accumulation of counter 'a' accumulation ;if a mouvement is not a falling and the counter 'a' increment, so in the next detection the falling(time of falling) is lower than 8(that's we predefine) 
      if (old_a == a){
          counter_same_a++;
      }
      if (counter_same_a == Tcounter_same_a){
          a=0;
          counter_same_a=0;
      }
      old_a = a; 
  
}




int main()
{
    XnStatus nRetVal = XN_STATUS_OK;
    xn::EnumerationErrors errors;
    
     
    XnUserID aUsers[MAX_NUM_USERS];
    XnUInt16 nUsers;
     
    int j;
    int index=0;
    char command[50];
    
    //charge xml config file 
    const char *fn = NULL;
    if    (fileExists(SAMPLE_XML_PATH)) fn = SAMPLE_XML_PATH;
    else if (fileExists(SAMPLE_XML_PATH_LOCAL)) fn = SAMPLE_XML_PATH_LOCAL;
    else {
        printf("Could not find '%s' nor '%s'. Aborting.\n" , SAMPLE_XML_PATH, SAMPLE_XML_PATH_LOCAL);
        return XN_STATUS_ERROR;
    }
    printf("Reading config from: '%s'\n", fn);

    //read info from xml file
    nRetVal = g_Context.InitFromXmlFile(fn, g_scriptNode, &errors);
    if (nRetVal == XN_STATUS_NO_NODE_PRESENT)
    {
        XnChar strError[1024];
        errors.ToString(strError, 1024);
        printf("%s\n", strError);
        return (nRetVal);
    }
    else if (nRetVal != XN_STATUS_OK)
    {
        printf("Open failed: %s\n", xnGetStatusString(nRetVal));
        return (nRetVal);
    }
    
    //find node depth
    nRetVal = g_Context.FindExistingNode(XN_NODE_TYPE_DEPTH, g_DepthGenerator);
    CHECK_RC(nRetVal,"No depth");

    //find node user
    nRetVal = g_Context.FindExistingNode(XN_NODE_TYPE_USER, g_UserGenerator);
    if (nRetVal != XN_STATUS_OK)
    {
        nRetVal = g_UserGenerator.Create(g_Context);
        CHECK_RC(nRetVal, "Find user generator");
    }


    //create user
    XnCallbackHandle hUserCallbacks, hCalibrationStart, hCalibrationComplete, hPoseDetected;
    if (!g_UserGenerator.IsCapabilitySupported(XN_CAPABILITY_SKELETON))
    {
        printf("Supplied user generator doesn't support skeleton\n");
        return 1;
    }
    nRetVal = g_UserGenerator.RegisterUserCallbacks(User_NewUser, User_LostUser, NULL, hUserCallbacks);
    CHECK_RC(nRetVal, "Register to user callbacks");
    nRetVal = g_UserGenerator.GetSkeletonCap().RegisterToCalibrationStart(UserCalibration_CalibrationStart, NULL, hCalibrationStart);
    CHECK_RC(nRetVal, "Register to calibration start");
    nRetVal = g_UserGenerator.GetSkeletonCap().RegisterToCalibrationComplete(UserCalibration_CalibrationComplete, NULL, hCalibrationComplete);
    CHECK_RC(nRetVal, "Register to calibration complete");

    if (g_UserGenerator.GetSkeletonCap().NeedPoseForCalibration())
    {
        g_bNeedPose = TRUE;
        if (!g_UserGenerator.IsCapabilitySupported(XN_CAPABILITY_POSE_DETECTION))
        {
            printf("Pose required, but not supported\n");
            return 1;
        }
        nRetVal = g_UserGenerator.GetPoseDetectionCap().RegisterToPoseDetected(UserPose_PoseDetected, NULL, hPoseDetected);
        CHECK_RC(nRetVal, "Register to Pose Detected");
        g_UserGenerator.GetSkeletonCap().GetCalibrationPose(g_strPose);
    }

    g_UserGenerator.GetSkeletonCap().SetSkeletonProfile(XN_SKEL_PROFILE_ALL);

    nRetVal = g_Context.StartGeneratingAll();
    CHECK_RC(nRetVal, "StartGenerating");
    
    
    printf("Starting to run\n");
 
    if(g_bNeedPose)
    {
        printf("Assume calibration pose\n");
    }
    
//****************************************************
//modified
//****************************************************    
    
     //open file to save data out 
    if( (fp_data = fopen ("DataOut.txt", "w+")) ==NULL){
        printf("we cann't open file !!!");
        exit(0);
    }
    
   //draw scene : we create a son program to execute draw scene in parallel
     int pid;
     
     switch (pid=fork()){
        case -1:  exit(1); break;
        case  0:   
                 execl("Sample-NiDrawScene",NULL);break;
              
        default :  printf("main\n");            
     }
    

    //runing ,if no keyboard hit
	while (!xnOSWasKeyboardHit())
    {
        g_Context.WaitOneUpdateAll(g_UserGenerator);
        
        nUsers=MAX_NUM_USERS;
        
        //get user , aUsers=one user detected , nUsers= number of users
        g_UserGenerator.GetUsers(aUsers, nUsers);
        
        
        for(XnUInt16 i=0; i<nUsers; i++)
        {
            if(g_UserGenerator.GetSkeletonCap().IsTracking(aUsers[i])==FALSE)
                continue;

            //get 22 joints' information 
            getJointsInfo(aUsers[i],XN_SKEL_HEAD,index);
            getJointsInfo(aUsers[i],XN_SKEL_NECK,index);
            getJointsInfo(aUsers[i],XN_SKEL_TORSO,index);
            getJointsInfo(aUsers[i],XN_SKEL_WAIST,index);
             
            getJointsInfo(aUsers[i],XN_SKEL_LEFT_COLLAR,index);
            getJointsInfo(aUsers[i],XN_SKEL_LEFT_SHOULDER,index);
            getJointsInfo(aUsers[i],XN_SKEL_LEFT_ELBOW,index);
            getJointsInfo(aUsers[i],XN_SKEL_LEFT_WRIST,index); 
            getJointsInfo(aUsers[i],XN_SKEL_LEFT_HAND,index);
                   
            getJointsInfo(aUsers[i],XN_SKEL_RIGHT_COLLAR,index);
            getJointsInfo(aUsers[i],XN_SKEL_RIGHT_SHOULDER,index);
            getJointsInfo(aUsers[i],XN_SKEL_RIGHT_ELBOW,index);
            getJointsInfo(aUsers[i],XN_SKEL_RIGHT_WRIST,index);
            getJointsInfo(aUsers[i],XN_SKEL_RIGHT_HAND,index);
            
            getJointsInfo(aUsers[i],XN_SKEL_LEFT_HIP,index);
            getJointsInfo(aUsers[i],XN_SKEL_LEFT_KNEE,index);
            getJointsInfo(aUsers[i],XN_SKEL_LEFT_ANKLE,index);
            getJointsInfo(aUsers[i],XN_SKEL_LEFT_FOOT,index);
            
            getJointsInfo(aUsers[i],XN_SKEL_RIGHT_HIP,index);
            getJointsInfo(aUsers[i],XN_SKEL_RIGHT_KNEE,index);
            getJointsInfo(aUsers[i],XN_SKEL_RIGHT_ANKLE,index);
            getJointsInfo(aUsers[i],XN_SKEL_RIGHT_FOOT,index);
            
        
            //calcul xyz, time , velocity
            get_XYZ();
           
            //dectect fall
            detectFalling();
            
            
            fprintf(fp_data,"*****************************************************\n");
            fprintf(fp_data,"*****************************************************\n");
            fprintf(fp_data,"********************next step************************\n");
            fprintf(fp_data,"*****************************************************\n");
            fprintf(fp_data,"*****************************************************\n");
           
             
           
            //when detect a fall 
            if (fallDetected==true){
                
                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                printf("!!!!!!!!!!!!!!!!!!!fall detected!!!!!!!!!!!!!!!!!!!!!!\n");
                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
               
                
                fprintf(fp_data,"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                fprintf(fp_data,"!!!!!!!!!!!!!!!!!fall detected!!!!!!!!!!!!!!!!!!!!!!!\n");
                fprintf(fp_data,"!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                
                //send signal to draw scene program
                kill(pid,SIGUSR1);
                
                //appel program chute
                
                strcpy( command, "./chute" );
                system(command);
                
                
                //reinitialize 
                 a=0;
                 b=0;
                 old_a=0;
                 counter_same_a=0;
                 activityDetection=false;
                 inactivityDetection=false;
                 fallDetected=false;
                
            }
            
            
        }
        
    }
    g_scriptNode.Release();
    g_DepthGenerator.Release();
    g_UserGenerator.Release();
    g_Context.Release();

}
