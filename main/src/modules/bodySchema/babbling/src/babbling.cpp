/*
 * Copyright (C) 2014 WYSIWYD Consortium, European Commission FP7 Project ICT-612139
 * Authors: Martina Zambelli
 * email:   m.zambelli13@imperial.ac.uk
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

#include "babbling.h"

#include <vector>
#include <iostream>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

using namespace std;
using namespace cv;
using namespace yarp::os;
using namespace yarp::sig;

bool Babbling::configure(yarp::os::ResourceFinder &rf) {
    bool bEveryThingisGood = true;

    moduleName = rf.check("name",Value("babbling"),"module name (string)").asString();

    part = rf.check("part",Value("left_arm")).asString();
    robot = rf.check("robot",Value("icubSim")).asString();
    fps = rf.check("fps",Value(30)).asInt(); //30;
    cmd_source = rf.check("cmd_source",Value("C")).asString();
    single_joint = rf.check("single_joint",Value(-1)).asInt();

    Bottle &start_pos = rf.findGroup("start_position");
    Bottle *b_start_commandHead = start_pos.find("head").asList();
    Bottle *b_start_command = start_pos.find("arm").asList();

    if ((b_start_commandHead->isNull()) | (b_start_commandHead->size()<5))
    {
        yWarning("Something is wrong in ini file. Default value is used");
        start_commandHead[0] = -25.0;
        start_commandHead[1]= -15.0;
        start_commandHead[2]= 0.0;
        start_commandHead[3]= 0.0;
        start_commandHead[4]= -20.0;
    }
    else
    {
        for(int i=0; i<b_start_commandHead->size(); i++)
            start_commandHead[i] = b_start_commandHead->get(i).asDouble();
    }

    if ((b_start_command->isNull()) | (b_start_command->size()<16))
    {
        yWarning("Something is wrong in ini file. Default value is used");
        start_command[0] = -40.0;
        start_command[1] = 25.0;
        start_command[2] = 20.0;
        start_command[3] = 85.0;
        start_command[4] = -50.0;
        start_command[5] = 0.0;
        start_command[6] = 8.0;
        start_command[7] = 15.0;
        start_command[8] = 30.0;
        start_command[9] = 4.0;
        start_command[10] = 2.0;
        start_command[11] = 0.0;
        start_command[12] = 7.0;
        start_command[13] = 14.0;
        start_command[14] = 5.0;
        start_command[15] = 14.0;
    }
    else
    {
        for(int i=0; i<b_start_command->size(); i++)
            start_command[i] = b_start_command->get(i).asDouble();
    }

    Bottle &babbl_par = rf.findGroup("babbling_param");
    freq = babbl_par.check("freq", Value(0.2)).asDouble();
    amp = babbl_par.check("amp", Value(5)).asDouble();
    train_duration = babbl_par.check("train_duration", Value(20.0)).asDouble();

    // keep this part if you want to use both cameras
    // if you only want to use want, delete accordingly
    if (robot=="icub")
    {
        leftCameraPort = "/"+robot+"/camcalib/left/out";
        rightCameraPort = "/"+robot+"/camcalib/right/out";
    }
    else //if (robot=='icubSim')
    {
        leftCameraPort = "/"+robot+"/cam/left";
        rightCameraPort = "/"+robot+"/cam/right";
    }

    setName(moduleName.c_str());

    // Open handler port
    if (!handlerPort.open("/" + getName() + "/rpc")) {
        cout << getName() << ": Unable to open port " << "/" << getName() << "/rpc" << endl;
        bEveryThingisGood = false;
    }

    if (!portVelocityOut.open("/" + getName() + "/" + part + "/velocity:o")) {
        cout << getName() << ": Unable to open port " << "/" << getName() << "/" << part << "/velocity:o" << endl;
        bEveryThingisGood = false;
    }


    // Initialize iCub and Vision
    while (!init_iCub(part)) {
        cout << getName() << ": initialising iCub... please wait... " << endl;
        bEveryThingisGood = false;
    }

    yDebug() << "End configuration...";

    attach(handlerPort);

    return bEveryThingisGood;
}

bool Babbling::interruptModule() {

    portVelocityOut.interrupt();
    handlerPort.interrupt();

    yInfo() << "Bye!";

    return true;

}

bool Babbling::close() {
    cout << "Closing module, please wait ... " <<endl;

    armDev->close();
    headDev->close();

    portVelocityOut.interrupt();
    portVelocityOut.close();

    handlerPort.interrupt();
    handlerPort.close();

    yInfo() << "Bye!";

    return true;
}

bool Babbling::respond(const Bottle& command, Bottle& reply) {
    yInfo() << "Babbling desired limb...";
    doBabbling();
    reply.addString("nack");

    return true;
}

bool Babbling::updateModule() {
    return true;
}

double Babbling::getPeriod() {
    return 0.1;
}


bool Babbling::doBabbling()
{
    // First go to home position
    bool homeStart = gotoStartPos();
    if(!homeStart) {
        cout << "I got lost going home!" << endl;
    }
    for(int i=0; i<16; i++)
    {
        ictrlArm->setControlMode(i,VOCAB_CM_VELOCITY);
    }

    if (cmd_source == "C")
    {
        double startTime = yarp::os::Time::now();
        while (Time::now() < startTime + train_duration){
            double t = Time::now() - startTime;
                babblingCommands(t,single_joint);
        }
    }
    else
    {
        babblingCommandsMatlab();
    }

    bool homeEnd = gotoStartPos();
    if(!homeEnd) {
        cout << "I got lost going home!" << endl;
    }

    return true;
}

yarp::sig::Vector Babbling::babblingCommands(double &t, int j_idx)
{
    double w1 = freq*t;
    double w2 = (freq+0.1)*t;
    double w3 = (freq+0.5)*t;
    double w4 = (freq+0.3)*t;

    for (unsigned int l=0; l<command.size(); l++) {
        command[l]=0;
    }

    if(j_idx != -1)
    {
        if(j_idx < 16 && j_idx>=0)
        {
            command[j_idx]=amp*cos(freq*t * 2 * M_PI);
        }
        else
        {
            yError("Invalid joint number.");
        }
    }
    else
    {
        if((part == "left_arm") | (part == "right_arm" ))
        {
            command[0]=cos(w1 * 2 * M_PI)+amp*cos(w4 * 2 * M_PI);
            command[1]=cos(w1 * 2 * M_PI)+amp*cos(w4 * 2 * M_PI);
            command[2]=cos(w1 * 2 * M_PI)+amp*cos(w4 * 2 * M_PI);
            command[3]=cos(w2 * 2 * M_PI)+amp*cos(w4 * 2 * M_PI);
            command[6]=cos(w3 * 2 * M_PI)+amp*cos(w4 * 2 * M_PI);
        }
        else if((part == "left_hand") | (part == "right_hand" ))
        {
            command[6]=amp*cos(w3 * 2 * M_PI)+amp*cos(w4 * 2 * M_PI);
            command[8]=amp*cos(w1 * 2 * M_PI);
            command[9]=amp*cos(w1 * 2 * M_PI);
            command[11]=amp*cos(w4 * 2 * M_PI);
            command[13]=amp*cos(w4 * 2 * M_PI);
            command[15]=amp*cos(w4 * 2 * M_PI);
            command[12]=amp*cos(w2 * 2 * M_PI);
            command[14]=amp*cos(w2 * 2 * M_PI);
        }
        else
        {
            yError("Can't babble the required body part.");
        }
    }

    Bottle& inDataB = portVelocityOut.prepare(); // Get the object
    inDataB.clear();
    for  (unsigned int l=0; l<command.size(); l++)
    {
        inDataB.addDouble(command[l]);
    }
    portVelocityOut.write();

    velArm->velocityMove(command.data());
    return command;
}


int Babbling::babblingCommandsMatlab()
{

    if (!portToMatlab.open("/portToMatlab:o")) {
        cout << ": Unable to open port " << "/portToMatlab:o" << endl;
    }
    if (!portReadMatlab.open("/portReadMatlab:i")) {
        cout << ": Unable to open port " << "/portReadMatlab:i" << endl;
    }

    while(!Network::isConnected(portToMatlab.getName(), "/matlab/read")) {
        Network::connect(portToMatlab.getName(), "/matlab/read");
        cout << "Waiting for port " << portToMatlab.getName() << " to connect to " << "/matlab/read" << endl;
        Time::delay(1.0);
    }

    while(!Network::isConnected("/matlab/write", portReadMatlab.getName())) {
        Network::connect("/matlab/write", portReadMatlab.getName());
        cout << "Waiting for port " << "/matlab/write" << " to connect to " << portReadMatlab.getName() << endl;
        Time::delay(1.0);
    }

    yInfo() << "Connections ok..." ;

    for(int i=0; i<=6; i++)
    {
        ictrlArm->setControlMode(i,VOCAB_CM_VELOCITY);
    }

    Bottle *endMatlab;
    Bottle *cmdMatlab;
    Bottle replyFromMatlab;
    Bottle bToMatlab;

    bool done = false;
    int nPr = 1;
    while(!done)
    {
        for (unsigned int l=1; l<command.size(); l++)
            command[l]=0;

        // get the commands from Matlab
        cmdMatlab = portReadMatlab.read(true) ;
        command[0] = cmdMatlab->get(0).asDouble();
        command[1] = cmdMatlab->get(1).asDouble();
        command[2] = cmdMatlab->get(2).asDouble();
        command[3] = cmdMatlab->get(3).asDouble();
        command[4] = cmdMatlab->get(4).asDouble();
        command[5] = cmdMatlab->get(5).asDouble();
        command[6] = cmdMatlab->get(6).asDouble();

        // Move
        int j=1;
        while(j--)
        {
            velArm->velocityMove(command.data());
            Time::delay(0.02);
        }yInfo() << "> Initialisation done.";


        // Tell Matlab that motion is done
        bToMatlab.clear();replyFromMatlab.clear();
        bToMatlab.addInt(1);
        portToMatlab.write(bToMatlab,replyFromMatlab);

        endMatlab = portReadMatlab.read(true) ;
        done = endMatlab->get(0).asInt();

        nPr = nPr +1;
    }

    yInfo() << "Going to tell Matlab that ports will be closed here.";
    // Tell Matlab that ports will be closed here
    bToMatlab.clear();replyFromMatlab.clear();
    bToMatlab.addInt(1);
    portToMatlab.write(bToMatlab,replyFromMatlab);

    portToMatlab.close();
    portReadMatlab.close();

    yInfo() << "Finished and ports to/from Matlab closed.";

    return 0;
}


bool Babbling::gotoStartPos()
{
    /* Move head to start position */
    commandHead = encodersHead;
    commandHead[0]=start_commandHead[0];
    commandHead[1]=start_commandHead[1];
    commandHead[2]=start_commandHead[2];
    commandHead[3]=start_commandHead[3];
    commandHead[4]=start_commandHead[4];
    posHead->positionMove(commandHead.data());

    /* Move arm to start position */
    command = encodersArm;
    for(int i=0; i<16; i++)
    {
        ictrlArm->setControlMode(i,VOCAB_CM_POSITION);
        command[i]=start_command[i];
    }
    posArm->positionMove(command.data());

    bool done_head=false;
    bool done_arm=false;
    while (!done_head || !done_arm) {
        yInfo() << "Wait for position moves to finish" ;
        posHead->checkMotionDone(&done_head);
        posArm->checkMotionDone(&done_arm);
        Time::delay(0.04);
    }

    Time::delay(1.0);

    return true;
}

bool Babbling::init_iCub(string &part)
{
    /* Create PolyDriver for left arm */
    Property option;

    string portnameArm = part;//"left_arm";
    cout << part << endl;
    option.put("robot", robot.c_str());
    option.put("device", "remote_controlboard");
    Value& robotnameArm = option.find("robot");

    string sA("/");
    sA += robotnameArm.asString();
    sA += "/";
    sA += portnameArm.c_str();
    sA += "/control";
    option.put("local", sA.c_str());

    sA.clear();
    sA += "/";
    sA += robotnameArm.asString();
    sA += "/";
    sA += portnameArm.c_str();
    cout << sA << endl;
    option.put("remote", sA.c_str());

    yDebug() << option.toString().c_str() ;

    armDev = new yarp::dev::PolyDriver(option);
    if (!armDev->isValid()) {
        yError() << "Device not available.  Here are the known devices:";
        yError() << yarp::dev::Drivers::factory().toString();
        Network::fini();
        return false;
    }

    armDev->view(posArm);
    armDev->view(velArm);
    armDev->view(encsArm);
    armDev->view(ictrlArm);

    if (posArm==NULL || encsArm==NULL || velArm==NULL || ictrlArm==NULL ){
        cout << "Cannot get interface to robot device" << endl;
        armDev->close();
    }

    if (encsArm==NULL){
        yError() << "Cannot get interface to robot device";
        armDev->close();
    }

    int nj = 0;
    posArm->getAxes(&nj);
    velArm->getAxes(&nj);
    encsArm->getAxes(&nj);
    encodersArm.resize(nj);

    yInfo() << "Wait for arm encoders";
    while (!encsArm->getEncoders(encodersArm.data()))
    {
        Time::delay(0.1);
        yInfo() << "Wait for arm encoders";
    }

    yInfo() << "Arm initialized.";

    /* Init. head */
    string portnameHead = "head";
    option.put("robot", robot.c_str()); //"icub"); // typically from the command line.
    option.put("device", "remote_controlboard");
    Value& robotnameHead = option.find("robot");

    string sH("/");
    sH += robotnameHead.asString();
    sH += "/";
    sH += portnameHead.c_str();
    sH += "/control";
    option.put("local", sH.c_str());

    sH.clear();
    sH += "/";
    sH += robotnameHead.asString();
    sH += "/";
    sH += portnameHead.c_str();
    option.put("remote", sH.c_str());

    headDev = new yarp::dev::PolyDriver(option);
    if (!headDev->isValid()) {
        yError() << "Device not available.  Here are the known devices:";
        yError() << yarp::dev::Drivers::factory().toString();
        Network::fini();
        return false;
    }

    headDev->view(posHead);
    headDev->view(velHead);
    headDev->view(encsHead);
    if (posHead==NULL || encsHead==NULL || velHead==NULL ){
        cout << "Cannot get interface to robot head" << endl;
        headDev->close();
    }
    int jnts = 0;

    posHead->getAxes(&jnts);
    velHead->getAxes(&jnts);
    encodersHead.resize(jnts);
    commandHead.resize(jnts);

    yInfo() << "Wait for encoders (HEAD)" ;
    while (!encsHead->getEncoders(encodersHead.data()))
    {
        Time::delay(0.1);
        yInfo() << "Wait for head encoders";
    }

    yInfo() << "Head initialized." ;

    /* Set velocity control for arm */
    yInfo() << "Set velocity control mode";
    for(int i=0; i<=6; i++)
    {
        ictrlArm->setControlMode(i,VOCAB_CM_VELOCITY);
    }

    yInfo() << "> Initialisation done.";

    return true;
}
