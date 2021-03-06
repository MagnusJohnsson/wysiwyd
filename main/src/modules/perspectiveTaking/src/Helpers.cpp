/*
 *
 * Copyright (C) 2015 WYSIWYD Consortium, European Commission FP7 Project ICT-612139
 * Authors: Tobias Fischer
 * email:   t.fischer@imperial.ac.uk
 * Permission is granted to copy, distribute, and/or modify this program
 * under the terms of the GNU General Public License, version 2 or any
 * later version published by the Free Software Foundation.
 *
 * A copy of the license can be found at
 * wysiwyd/license/gpl.txt
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details
*/

#include <rtabmap/core/util3d.h>
#include <rtabmap/utilite/UEventsManager.h>

#include "PerspectiveTaking.h"
#include "CameraKinectWrapper.h"

using namespace std;
using namespace yarp::os;
using namespace wysiwyd::wrdac;

bool perspectiveTaking::setupThreads() {
    // Read parameters for rtabmap from rtabmap_config.ini
    ParametersMap parameters;
    Rtabmap::readParameters(resfind.findFileByName("rtabmap_config.ini"), parameters);

    // Here is the pipeline that we will use:
    // CameraOpenni -> "CameraEvent" -> OdometryThread -> "OdometryEvent" -> RtabmapThread -> "RtabmapEvent"

    // Create the OpenNI camera, it will send a CameraEvent at the rate specified.
    // Set transform to camera so z is up, y is left and x going forward (as in PCL)
    camera = new CameraKinectWrapper(client, 20, rtabmap::Transform(0,0,1,0, -1,0,0,0, 0,-1,0,0));

    cameraThread = new CameraThread(camera);

    if(!cameraThread->init()) {
        yError() << getName() << ":Camera init failed!";
        return false;
    }

    // Create an odometry thread to process camera events, it will send OdometryEvent
    odomThread = new OdometryThread(new OdometryBOW(parameters));

    // Create RTAB-Map to process OdometryEvent
    rtabmap = new Rtabmap();
    rtabmap->init(parameters, resfind.findFileByName("rtabmap.db"));
    rtabmapThread = new RtabmapThread(rtabmap);

    boost::this_thread::sleep_for (boost::chrono::milliseconds (100));

    // Setup handlers
    odomThread->registerToEventsManager();
    rtabmapThread->registerToEventsManager();
    mapBuilder->registerToEventsManager();

    // The RTAB-Map is subscribed by default to CameraEvent, but we want
    // RTAB-Map to process OdometryEvent instead, ignoring the CameraEvent.
    // We can do that by creating a "pipe" between the camera and odometry, then
    // only the odometry will receive CameraEvent from that camera. RTAB-Map is
    // also subscribed to OdometryEvent by default, so no need to create a pipe
    // between odometry and RTAB-Map.
    UEventsManager::createPipe(cameraThread, odomThread, "CameraEvent");

    // Let's start the threads
    rtabmapThread->start();
    odomThread->start();
    cameraThread->start();

    return true;
}

Eigen::Matrix4f perspectiveTaking::getManualTransMat(float camOffsetX,
                                                     float camOffsetY,
                                                     float camOffsetZ,
                                                     float camAngle) {
    // Estimate rotation+translation from kinect to robot head
    Eigen::Affine3f rot_trans = Eigen::Affine3f::Identity();

    // robot head is camOffsetZ below kinect, can camOffsetX behind kinect
    rot_trans.translation() << camOffsetX, camOffsetY, camOffsetZ;
    // robot head is tilted by camAngle degrees down
    float theta = camAngle/180.0*M_PI;
    rot_trans.rotate (Eigen::AngleAxisf (theta, Eigen::Vector3f::UnitY()));

    return rot_trans.matrix();
}

// TODO: Wrong matrix returned, needs to be fixed
Eigen::Matrix4f perspectiveTaking::getRFHTransMat() {
    yarp::sig::Matrix kinect2robot;
    yarp::sig::Matrix robot2kinect;

    while(!queryRFHTransMat("kinect", "icub", kinect2robot)) {
        yWarning() << "Kinect2iCub matrix not calibrated, please do so in agentDetector";
        Time::delay(1.0);
    }
    while(!queryRFHTransMat("icub", "kinect", robot2kinect)) {
        yWarning() << "iCub2Kinect matrix not calibrated, please do so in agentDetector";
        Time::delay(1.0);
    }

    return yarp2EigenM(kinect2robot);
}

bool perspectiveTaking::queryRFHTransMat(const string& from, const string& to, Matrix& m)
{
    if (rfh.getOutputCount() > 0) {
        Bottle bCmd;
        bCmd.clear();
        bCmd.addString("mat");
        bCmd.addString(from);
        bCmd.addString(to);

        Bottle reply;
        reply.clear();
        rfh.write(bCmd, reply);
        if (reply.get(0) == "nack") {
            return false;
        } else {
            Bottle* bMat = reply.get(1).asList();
            m.resize(4,4);
            for(int i=0; i<4; i++) {
                for(int j=0; j<4; j++) {
                    m(i,j)=bMat->get(4*i+j).asDouble();
                }
            }
            yDebug() << "Transformation matrix from " << from
                 << " to " << to << " retrieved:";
            yDebug() << m.toString(3,3).c_str();
            return true;
        }
    }
    return false;
}

void perspectiveTaking::setViewCameraReference(const Vector &p_pos, const Vector &p_view, const Vector &p_up, const string &viewport) {
    setViewCameraReference(yarp2EigenV(p_pos), yarp2EigenV(p_view), yarp2EigenV(p_up), viewport);
}

void perspectiveTaking::setViewCameraReference(const Eigen::Vector4f &p_pos, const Eigen::Vector4f &p_view, const Eigen::Vector4f &p_up, const string &viewport) {
    Eigen::Vector4f pos = yarp2pcl * p_pos;
    Eigen::Vector4f view = yarp2pcl * p_view;
    Eigen::Vector4f up = yarp2pcl * p_up;
    pos/=pos[3]; view/=view[3], up/=up[3];

    Eigen::Vector4f up_diff = up-pos;

    mapBuilder->setCameraPosition(
        pos[0],     pos[1],     pos[2],
        view[0],    view[1],    view[2],
        up_diff[0], up_diff[1], up_diff[2],
        viewport);
}

void perspectiveTaking::setViewRobotReference(const Vector &p_pos, const Vector &p_view, const Vector &p_up, const string &viewport) {
    setViewRobotReference(yarp2EigenV(p_pos), yarp2EigenV(p_view), yarp2EigenV(p_up), viewport);
}

void perspectiveTaking::setViewRobotReference(const Eigen::Vector4f &p_pos, const Eigen::Vector4f &p_view, const Eigen::Vector4f &p_up, const string &viewport) {
    Eigen::Vector4f pos = kin2head * yarp2pcl * p_pos;
    //Eigen::Vector4f view = kinect2robotpcl * yarp2pcl * Eigen::Vector4f(p_headPos[0]+1.0,p_headPos[1],p_headPos[2],1);
    Eigen::Vector4f view = kin2head * yarp2pcl * p_view;
    Eigen::Vector4f up = kin2head * yarp2pcl * p_up;

    pos/=pos[3]; view/=view[3], up/=up[3];

    Eigen::Vector4f up_diff = up-pos;

    mapBuilder->setCameraPosition(
        pos[0],     pos[1],     pos[2],
        view[0],    view[1],    view[2],
        up_diff[0], up_diff[1], up_diff[2],
        viewport);
}

Eigen::Matrix4f perspectiveTaking::yarp2EigenM(const yarp::sig::Matrix& yarpMatrix) {
    Eigen::Matrix4f pclmatrix = Eigen::Matrix4f::Zero();
    for(int i=0; i<4; i++) {
        for(int j=0; j<4; j++) {
            pclmatrix(i,j)=yarpMatrix(i,j);
        }
    }

    return pclmatrix;
}

Eigen::Vector4f perspectiveTaking::yarp2EigenV(const Vector& yarpVector) {
    return Eigen::Vector4f(yarpVector[0], yarpVector[1], yarpVector[2], 1);
}

pcl::PointXYZ perspectiveTaking::eigen2pclV(const Eigen::Vector4f& eigenVector) {
    return pcl::PointXYZ(eigenVector[0], eigenVector[1], eigenVector[2]);
}

bool perspectiveTaking::setUpdateTimer(const int32_t interval) {
    setCamPosTimer->setInterval(interval);
    return true;
}

bool perspectiveTaking::setDecimationOdometry(const int32_t decimation) {
    mapBuilder->setDecimationOdometry(decimation);
    return true;
}

bool perspectiveTaking::setDecimationStatistics(const int32_t decimation) {
    mapBuilder->setDecimationStatistics(decimation);
    return true;
}

bool perspectiveTaking::processStats(const bool enable) {
    mapBuilder->doProcessStats = enable;
    return true;
}

bool perspectiveTaking::kinectStereoCalibrate() {
    static int pointsCount=0;

    CvPoint clickKinect;
    CvPoint clickStereo;

    if (Bottle *bPos=getClickPortKinect.read(false)) {
        if (bPos->size()>=2) {
            clickKinect.x=bPos->get(0).asInt();
            clickKinect.y=bPos->get(1).asInt();
            yInfo("Received new click location for Kinect: (%d,%d)",
                  clickKinect.x,clickKinect.y);
        } else {
            yError("Click from Kinect has wrong format");
            return false;
        }
    } else {
        yError("Did not get click from Kinect");
        return false;
    }

    if (Bottle *bPos=getClickPortStereo.read(false)) {
        if (bPos->size()>=2) {
            clickStereo.x=bPos->get(0).asInt();
            clickStereo.y=bPos->get(1).asInt();
            yInfo("Received new click location for Stereo: (%d,%d)",
                  clickStereo.x,clickStereo.y);
        } else {
            yError("Click from Stereo has wrong format");
            return false;
        }
    } else {
        yError("Did not get click from Stereo");
        return false;
    }

    Vector pKinect(3); pKinect.resize(3,0.0);
    Vector pStereo(3); pStereo.resize(3,0.0);

    client.get3DPoint(clickKinect.x,clickKinect.y,pKinect);

    yInfo("3D Point for Kinect: (%lf,%lf,%lf)",
          pKinect[0],pKinect[1],pKinect[2]);

    Bottle bSFM, bSFMResp;
    bSFM.addString("Root");
    bSFM.addInt(clickStereo.x);
    bSFM.addInt(clickStereo.y);
    sfm.write(bSFM, bSFMResp);
    pStereo[0] = bSFMResp.get(0).asDouble();
    pStereo[1] = bSFMResp.get(1).asDouble();
    pStereo[2] = bSFMResp.get(2).asDouble();

    yInfo("3D Point for Stereo: (%lf,%lf,%lf)",
          pStereo[0],pStereo[1],pStereo[2]);

    //Prepare the bottle to be sent to RFH
    Bottle bFRH, bRFHResp;
    bFRH.addString("add");
    bFRH.addString("kinect");
    Bottle &cooKinect = bFRH.addList();
    cooKinect.addDouble(pKinect[0]);
    cooKinect.addDouble(pKinect[1]);
    cooKinect.addDouble(pKinect[2]);

    Bottle &cooiCub = bFRH.addList();
    cooiCub.addDouble(pStereo[0]);
    cooiCub.addDouble(pStereo[1]);
    cooiCub.addDouble(pStereo[2]);

    rfh.write(bFRH,bRFHResp);
    yInfo() << "Sent to RFH: "  <<bFRH.toString();
    yInfo() << "Got from RFH: " <<bRFHResp.toString();

    pointsCount++;
    if(pointsCount >= 3 )
    {
        Bottle calibBottle, calibReply;
        calibBottle.addString("cal");
        calibBottle.addString("kinect");
        rfh.write(calibBottle,calibReply);
        yInfo() << "Calibrated!" << calibReply.toString();

        /*
         * calibBottle.clear();
        calibBottle.addString("save");
        rfh.write(calibBottle,calibReply);
        yInfo() << "Saved to file ! " << calibReply.toString() << endl;
        */

        yarp::sig::Matrix kinect2robot;
        queryRFHTransMat("kinect", "icub", kinect2robot);
        cout << endl << yarp2EigenM(kinect2robot) << endl;
    }

    return true;
}
