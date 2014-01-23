/**
 * \file SWTeleoperation_iCub.cpp
 * \author Florian Lance, Guillaume Gibert, Emmanuel Body
 * \date 05-12-2013
 * \brief ...
 */

#include "icub/SWTeleoperation_iCub.h"
#include "icub/SWiCubFaceMotion.h"

#include "SWTrackingDevice.h"

#include <sstream>


SWTeleoperation_iCub::SWTeleoperation_iCub() : m_pIHeadVelocity(NULL), m_pIHeadEncoders(NULL), m_pIHeadPosition(NULL), m_bIsRunning(false),
            m_i32HeadTimeLastBottle(0), m_i32FaceTimeLastBottle(0), m_i32ArmsTimeLastBottle(0), m_i32GazeTimeLastBottle(0)
{}

SWTeleoperation_iCub::~SWTeleoperation_iCub()
{}

bool SWTeleoperation_iCub::configure(ResourceFinder &rf)
{
    // gets the module name which will form the stem of all module port names
        m_sModuleName   = rf.check("name", Value("teleoperation_iCub"), "Teleoperation/iCub Module name (string)").asString();
        m_sRobotName    = rf.check("robot",  Value("icubSim"),  "Robot name (string)").asString();
        setName(m_sModuleName.c_str());

    // acceleration/speeds values for iCub
        m_i32JointVelocityValue     = rf.check("jointVelocityValue",      Value(600),  "Joint Velocity Value (int)").asInt();
        m_i32JointAccelerationValue = rf.check("jointAccelerationValue",  Value(600),  "Joint Acceleration Value (int)").asInt();

    // min / max values for iCub joints
        m_i32HeadMinValueJoint0 = rf.check("minValueJoint0",  Value(-40), "Minimum Joint0 Value (int)").asInt();
        m_i32HeadMaxValueJoint0 = rf.check("maxValueJoint0",  Value( 30), "Maximum Joint0 Value (int)").asInt();

        m_i32HeadMinValueJoint1 = rf.check("minValueJoint1",  Value(-70), "Minimum Joint1 Value (int)").asInt();
        m_i32HeadMaxValueJoint1 = rf.check("maxValueJoint1",  Value( 60), "Maximum Joint1 Value (int)").asInt();

        m_i32HeadMinValueJoint2 = rf.check("minValueJoint2",  Value(-55), "Minimum Joint2 Value (int)").asInt();
        m_i32HeadMaxValueJoint2 = rf.check("maxValueJoint2",  Value( 55), "Maximum Joint2 Value (int)").asInt();


    // min / max values for iCub eyelids
        if(m_sRobotName == "icubSim")
        {
            m_i32MinValueEyelids = rf.check("minValueEyelidsSim",  Value( 24 ),  "Min value eyelidSim (int)").asInt();
            m_i32MaxValueEyelids = rf.check("maxValueEyelidsSim",  Value( 48 ),  "Max value eyelidSim (int)").asInt();
        }
        else
        {
            m_i32MinValueEyelids = rf.check("minValueEyelids",  Value( 30 ),  "Min value eyelid (int)").asInt();
            m_i32MaxValueEyelids = rf.check("maxValueEyelids",  Value( 70 ),  "Max value eyelid (int)").asInt();
        }

    // miscellaneous
        m_i32Fps                    = rf.check("fps",                   Value(100),  "Frame per second (int)").asInt();
        m_i32HeadTimeoutReset       = rf.check("headTimeoutReset",      Value(3000), "Head gaze timeout reset iCub (int)").asInt();
        m_i32FaceTimeoutReset       = rf.check("faceTimeoutReset",      Value(3000), "Face gaze timeout reset iCub (int)").asInt();
        m_i32ArmsTimeoutReset       = rf.check("armsTimeoutReset",      Value(3000), "Arms timeout reset iCub (int)").asInt();
        m_i32GazeTimeoutReset       = rf.check("gazeTimeoutReset",      Value(3000), "Gaze timeout reset iCub (int)").asInt();

    // set polydriver options
        m_oHeadOptions.put("robot",     m_sRobotName.c_str());
        m_oHeadOptions.put("device",    "remote_controlboard");
        m_oHeadOptions.put("local",    ("/local/" + m_sRobotName + "/head").c_str());
        m_oHeadOptions.put("name",     ("/" + m_sRobotName + "/head").c_str());
        m_oHeadOptions.put("remote",   ("/" + m_sRobotName + "/head").c_str());

    // init polydriver
        m_oRobotHead.open(m_oHeadOptions);
        if(!m_oRobotHead.isValid())
        {
            std::cerr << "-ERROR: robotHead is not valid" << std::endl;
            return false;
        }

    // initializing controllers
        if (!m_oRobotHead.view(m_pIHeadVelocity) || !m_oRobotHead.view(m_pIHeadPosition) || !m_oRobotHead.view(m_pIHeadEncoders))
        {
            std::cerr << "-ERROR: while getting required robot head interfaces." << std::endl;
            m_oRobotHead.close();
            return false;
        }

    // init ports
        m_sHeadTrackerPortName       = "/teleoperation/" + m_sRobotName + "/head";
        m_sGazeTrackerPortName       = "/teleoperation/" + m_sRobotName + "/gaze";
        m_sFaceTrackerPortName       = "/teleoperation/" + m_sRobotName + "/face";
        m_sEyelidInputPortName       = "/" + m_sRobotName + "/face/raw/in";
        m_sEyelidOutputPortName      = "/teleoperation/" + m_sRobotName + "/eyelids/out";

        if(!m_oHeadTrackerPort.open(m_sHeadTrackerPortName.c_str()) ||
           !m_oGazeTrackerPort.open(m_sGazeTrackerPortName.c_str()) ||
           !m_oFaceTrackerPort.open(m_sFaceTrackerPortName.c_str()) ||
           !m_oFaceHandlerPort.open(m_sEyelidOutputPortName.c_str()))
	{
		std::cerr << "-ERROR: Unable to open ports." << std::endl;
		m_oRobotHead.close();
		interruptModule();
		return false;
	}
    //    attach(m_oFaceHandlerPort);  // attach to port
        Network::connect(m_sEyelidOutputPortName.c_str(), m_sEyelidInputPortName.c_str());

    // move to rest position
        int l_i32JointsNb = 0;
        m_pIHeadPosition->getAxes(&l_i32JointsNb );

    resetHeadPosition();

    // set accelerations
        Vector l_aHeadTmp;
        l_aHeadTmp.resize(l_i32JointsNb );
        for (int i = 0; i < l_i32JointsNb ; i++)
        {
            l_aHeadTmp[i] = m_i32JointAccelerationValue;            
        }
        m_pIHeadPosition->setRefAccelerations(l_aHeadTmp.data());

    // set speed
        for (int i = 0; i < l_i32JointsNb ; i++)
        {
            l_aHeadTmp[i] = m_i32JointVelocityValue;
            m_pIHeadPosition->setRefSpeed(i, l_aHeadTmp[i]);
        }
        m_pIHeadPosition->setRefSpeeds(l_aHeadTmp.data());

    return (m_bIsRunning = true); // let the RFModule know everything went well
}


bool SWTeleoperation_iCub::interruptModule()
{
    m_bIsRunning = false;

    m_oHeadTrackerPort.interrupt();
    m_oGazeTrackerPort.interrupt();
    m_oFaceTrackerPort.interrupt();
    m_oFaceHandlerPort.interrupt();

    std::cout << "--Interrupting the iCub Teleoperation module..." << std::endl;
    return true;
}

void SWTeleoperation_iCub::resetHeadPosition()
{
    // reset head position
        m_pIHeadPosition->positionMove(0,0);
        m_pIHeadPosition->positionMove(1,0);
        m_pIHeadPosition->positionMove(2,0);
}


bool SWTeleoperation_iCub::close()
{
    resetHeadPosition();

    // close ports
        m_oHeadTrackerPort.close();
        m_oFaceTrackerPort.close();
        m_oFaceHandlerPort.close();
        m_oGazeTrackerPort.close();

    // close polydrivers
        m_oRobotHead.close();
        // ...

    return true;
}

bool SWTeleoperation_iCub::updateModule()
{
    if(!m_bIsRunning)
    {
        return false;
    }

    // head / gaze
        int l_i32HeadJointsNb = 0;
        Vector l_vHeadEncoders, l_vHeadCommand;
        m_pIHeadPosition->getAxes(&l_i32HeadJointsNb);
        l_vHeadCommand.resize(l_i32HeadJointsNb);
        l_vHeadEncoders.resize(l_i32HeadJointsNb );
        //read encoders
            m_pIHeadEncoders->getEncoders(l_vHeadEncoders.data());
        // init command vlaues
            std::vector<double> l_vHeadJoints(6,0.);

    // left / right arm
        // ...

    // face
        // ...

    Bottle *l_pHeadTarget = NULL, *l_pFaceTarget = NULL, *l_pGazeTarget = NULL; //, *l_pLeftArmTarget = NULL, *l_pRightArmTarget = NULL;

    // read head commands
    {
        l_pHeadTarget = m_oHeadTrackerPort.read(false);

        if(l_pHeadTarget)
        {
            int l_deviceId = l_pHeadTarget->get(0).asInt();

//            std::cout << "ID : " << l_deviceId << std::endl;
            switch(l_deviceId)
            {
                case swTracking::FOREST_LIB :
                    {
                        l_vHeadJoints[0] = -l_pHeadTarget->get(1).asDouble(); //head rotation "yes" [-40 30]
                        l_vHeadJoints[1] = -l_pHeadTarget->get(3).asDouble(); //head rotation [-70 60]
                        l_vHeadJoints[2] = -l_pHeadTarget->get(2).asDouble(); //head rotation "no" [-55 55]
                    }
                break;
                case swTracking::COREDATA_LIB :
                    {
                        l_vHeadJoints[0] =  l_pHeadTarget->get(4).asDouble()*180./3.1416; // up/down head
                        l_vHeadJoints[1] =  l_pHeadTarget->get(6).asDouble()*180./3.1416; // left/right head
                        l_vHeadJoints[2] = -l_pHeadTarget->get(5).asDouble()*180./3.1416; // head
                    }
                break;
                case swTracking::EMICP_LIB :
                    {
                        l_vHeadJoints[0] = -l_pHeadTarget->get(4).asDouble(); // up/down head
                        l_vHeadJoints[1] = -l_pHeadTarget->get(6).asDouble();  // left/right head
                        l_vHeadJoints[2] = -l_pHeadTarget->get(5).asDouble(); // head
                    }
                break;
                case swTracking::FACESHIFT_LIB :
                    {
                        l_vHeadJoints[0] = -l_pHeadTarget->get(4).asDouble()*180./3.1416; // up/down head
                        l_vHeadJoints[1] = -l_pHeadTarget->get(6).asDouble()*180./3.1416; // left/right head
                        l_vHeadJoints[2] = -l_pHeadTarget->get(5).asDouble()*180./3.1416; // head
                    }
                break;
            }

            m_i32HeadTimeLastBottle = 0;
        }
        else
        {
            m_i32HeadTimeLastBottle += 1000/m_i32Fps;

            if(m_i32HeadTimeLastBottle > m_i32HeadTimeoutReset)
            {
                m_i32HeadTimeLastBottle = 0;
                resetHeadPosition();
            }
        }
    }

    // read face commands
    {
        l_pFaceTarget = m_oFaceTrackerPort.read(false);

        if(l_pFaceTarget)
        {
            int l_deviceId = l_pFaceTarget->get(0).asInt();

            switch(l_deviceId)
            {
                case swTracking::COREDATA_LIB :
                    {
                        Bottle &l_oFaceMotionBottle = m_oFaceHandlerPort.prepare();

                        // mouth
                            l_oFaceMotionBottle.clear();
                            std::vector<double> l_vInnerLip2, l_vInnerLip6;
                            for(int ii = 0; ii < 3; ++ii)
                            {
                                l_vInnerLip2.push_back(l_pFaceTarget->get(25+ii).asDouble());
                                l_vInnerLip6.push_back(l_pFaceTarget->get(37+ii).asDouble());
                            }
                            l_oFaceMotionBottle.addString(swIcub::lipCommand(l_vInnerLip2, l_vInnerLip6).c_str());
                            m_oFaceHandlerPort.write();
                            Time::delay(0.001);

                        // left eyeBrow
                            l_oFaceMotionBottle.clear();
                            std::vector<double> l_vLeftEyeBrow, l_vRightEyeBrow, l_vNoseCenter;
                            for(int ii = 0; ii < 3; ++ii)
                            {
                                l_vLeftEyeBrow.push_back(l_pFaceTarget->get(52+ii).asDouble());
                                l_vRightEyeBrow.push_back(l_pFaceTarget->get(43+ii).asDouble());
                                l_vNoseCenter.push_back(l_pFaceTarget->get(61+ii).asDouble());
                            }
                            l_oFaceMotionBottle.addString(swIcub::leftEyeBrowCommand(l_vLeftEyeBrow, l_vNoseCenter).c_str());
                            m_oFaceHandlerPort.write();
                            Time::delay(0.001);

                            // right eyeBrow
                            l_oFaceMotionBottle.clear();
                            l_oFaceMotionBottle.addString(swIcub::rightEyeBrowCommand(l_vRightEyeBrow, l_vNoseCenter).c_str());
                            m_oFaceHandlerPort.write();
                            Time::delay(0.001);
                    }
                break;
                case swTracking::STASM_LIB :
                    {
                        // ...
                    }
                break;
                case swTracking::FOREST_LIB :
                    {
                        // ...
                    }
                break;
                case swTracking::FACESHIFT_LIB :
                    {
                        // ...
                    }
                break;
            }

            m_i32FaceTimeLastBottle = 0;
        }
        else
        {
            m_i32FaceTimeLastBottle += 1000/m_i32Fps;

            if(m_i32FaceTimeLastBottle > m_i32FaceTimeoutReset)
            {
                m_i32FaceTimeLastBottle = 0;
                // reset face ... TODO
            }
        }
    }

    // read gaze commands
    {
        l_pGazeTarget = m_oGazeTrackerPort.read(false);

        if(l_pGazeTarget)
        {
            int l_deviceId = l_pGazeTarget->get(0).asInt();

            switch(l_deviceId)
            {
                case swTracking::COREDATA_LIB :
                    {
                        l_vHeadJoints[3] =  ((l_pGazeTarget->get(9).asDouble()  + l_pGazeTarget->get(14).asDouble())/2)*180/3.1416; // up/down eye [-35; +15]
                        l_vHeadJoints[4] = -((l_pGazeTarget->get(10).asDouble() + l_pGazeTarget->get(15).asDouble())/2)*180/3.1416; // version angle [-50; 52] = (L+R)/2
                        l_vHeadJoints[5] =  (-l_pGazeTarget->get(10).asDouble() + l_pGazeTarget->get(15).asDouble())   *180/3.1416; // vergence angle [0 90] = R-L

                        Bottle &l_oFaceMotionBottle = m_oFaceHandlerPort.prepare();
                        // eye closure
                            l_oFaceMotionBottle.clear();
                            double l_dLeftEyeClosure = l_pGazeTarget->get(8).asDouble(), l_dRightEyeClosure = l_pGazeTarget->get(13).asDouble();
                            l_oFaceMotionBottle.addString(
                            eyesOpeningCode((1.0-(l_dLeftEyeClosure + l_dRightEyeClosure)/2.0), m_i32MinValueEyelids, m_i32MaxValueEyelids).c_str());
                            m_oFaceHandlerPort.write();
                            Time::delay(0.001);

                            std::cout << "eyesOpeningCode " << std::endl;
                    }
                break;
                case swTracking::FACESHIFT_LIB :
                    {
                        // ...
                    }
                break;
                case swTracking::TOBII_LIB :
                    {
                        // ...
                    }
                break;
            }

            m_i32GazeTimeLastBottle = 0;
        }
        else
        {
            m_i32GazeTimeLastBottle += 1000/m_i32Fps;

            if(m_i32GazeTimeLastBottle > m_i32GazeTimeoutReset)
            {
                m_i32GazeTimeLastBottle = 0;
                // reset gaze ... TODO
            }
        }


    }
    // read left arm commands
//    {
//        l_pLeftArmTarget = m_oLeftArmTrackerPort.read(false);

//        if(l_pLeftArmTarget)
//        {
//            int l_deviceId = l_pLeftArmTarget->get(0).asInt();

//            switch(l_deviceId)
//            {
//                case swTracking::FORTH_LIB :
//                    {
//                        // ...
//                    }
//                break;
//                case swTracking::OPENNI_LIB :
//                    {
//                        // ...
//                    }
//                break;
//            }
//        }
//    }
//    // read right arm commands
//    {
//        l_pRightArmTarget = m_oRightArmTrackerPort.read();

//        if(l_pRightArmTarget)
//        {
//            int l_deviceId = l_pRightArmTarget->get(0).asInt();

//            switch(l_deviceId)
//            {
//                case swTracking::FORTH_LIB :
//                    {
//                        // ...
//                    }
//                break;
//                case swTracking::OPENNI_LIB :
//                    {
//                        // ...
//                    }
//                break;
//            }
//        }
//    }

    //check each joint value to ensure it is in the right range, if not crop to the max/min values
    if (l_vHeadJoints[0] > m_i32HeadMaxValueJoint0)
            l_vHeadJoints[0] = m_i32HeadMaxValueJoint0;
    if (l_vHeadJoints[0] < m_i32HeadMinValueJoint0)
            l_vHeadJoints[0] = m_i32HeadMinValueJoint0;

    if (l_vHeadJoints[1] > m_i32HeadMaxValueJoint1)
            l_vHeadJoints[1] = m_i32HeadMaxValueJoint1;
    if (l_vHeadJoints[1] < m_i32HeadMinValueJoint1)
            l_vHeadJoints[1] = m_i32HeadMinValueJoint1;

    if (l_vHeadJoints[2] > m_i32HeadMaxValueJoint2)
            l_vHeadJoints[2] = m_i32HeadMaxValueJoint2;
    if (l_vHeadJoints[2] < m_i32HeadMinValueJoint2)
            l_vHeadJoints[2] = m_i32HeadMinValueJoint2;

    if(l_pHeadTarget || l_pGazeTarget)
    {
        l_vHeadCommand    = 0.0;

        // head rotation
        l_vHeadCommand[0] = 0.9*(l_vHeadJoints[0] - l_vHeadEncoders[0]);
        l_vHeadCommand[1] = 0.9*(l_vHeadJoints[1] - l_vHeadEncoders[1]);
        l_vHeadCommand[2] = 0.9*(l_vHeadJoints[2] - l_vHeadEncoders[2]);

        // gaze
        l_vHeadCommand[3] = 1.0 * (l_vHeadJoints[3] - l_vHeadEncoders[3]);
        l_vHeadCommand[4] = 1.0 * (l_vHeadJoints[4] - l_vHeadEncoders[4]);
        l_vHeadCommand[5] = 1.0 * (l_vHeadJoints[5] - l_vHeadEncoders[5]);

        // move
        m_pIHeadVelocity->velocityMove(l_vHeadCommand.data());
    }


//    if(l_pFaceTarget)
//    {

//    }
//    if(l_pLeftArmTarget)
//    {

//    }
//    if(l_pRightArmTarget)
//    {

//    }

    return true;
}


double SWTeleoperation_iCub::getPeriod()
{
    return 1./m_i32Fps;
}


std::string SWTeleoperation_iCub::eyesOpeningCode(cdouble dEyeLids, cint i32MinValue, cint i32MaxValue)
{
    int l_i32ScaledValue = (int)(i32MinValue + (i32MaxValue - i32MinValue) * dEyeLids);

    std::ostringstream l_osCodeValue;
    l_osCodeValue << 'S';
    l_osCodeValue << l_i32ScaledValue;

    return l_osCodeValue.str();
}

int main(int argc, char* argv[])
{
    /* initialize yarp network */
    Network yarp;
    if (!yarp.checkNetwork())
    {
        std::cerr << "-ERROR: Problem connecting to YARP server" << std::endl;
        return -1;
    }

    /* create your module */
    SWTeleoperation_iCub l_oTeleoperation_iCub;

    /* prepare and configure the resource finder */
    ResourceFinder rf;
    rf.setVerbose(true);
    rf.setDefaultConfigFile("teleoperation_iCub.ini"); //overridden by --from parameter
    rf.setDefaultContext("swooz-teleoperation/conf");   //overridden by --context parameter
    rf.configure("ICUB_ROOT", argc, argv);

    /* configure the module */
    std::cout << "Configuring the iCub Teleoperation module..."<< std::endl;
    if (l_oTeleoperation_iCub.configure(rf))
    {
        /* run the module */
        std::cout << "Starting the iCub Teleoperation module..." << std::endl;
        l_oTeleoperation_iCub.runModule();
    }
    else
    {
        std::cerr << "Failed to configure the iCub Teleoperation module!"<< std::endl;
    }

    return 0;
}
