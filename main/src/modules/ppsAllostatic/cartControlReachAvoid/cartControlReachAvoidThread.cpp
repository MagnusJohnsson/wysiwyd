/*
* Copyright (C) 2015 WYSIWYD
* Authors: Matej Hoffmann  and Ugo Pattacini
* email:   matej.hoffmann@iit.it, ugo.pattacini@itt.it
* Permission is granted to copy, distribute, and/or modify this program
* under the terms of the GNU General Public License, version 2 or any
* later version published by the Free Software Foundation.
*
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
* Public License for more details
*/

#include <stdlib.h>     /* srand, rand */
#include <time.h>       /* time */
#include <stdarg.h>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>

#include <vector>

#include <gsl/gsl_math.h>

#include <yarp/os/all.h>
#include <yarp/dev/all.h>

#include "cartControlReachAvoidThread.h"


void cartControlReachAvoidThread::getTorsoOptions(Bottle &b, const char *type, const int i, Vector &sw, Matrix &lim)
{
        if (b.check(type))
        {
            Bottle &grp=b.findGroup(type);
            sw[i]=grp.get(1).asString()=="on"?1.0:0.0;

            if (grp.check("min","Getting minimum value"))
            {
                lim(i,0)=1.0;
                lim(i,1)=grp.find("min").asDouble();
            }

            if (grp.check("max","Getting maximum value"))
            {
                lim(i,2)=1.0;
                lim(i,3)=grp.find("max").asDouble();
            }
        }
}


void cartControlReachAvoidThread::getTorsoHomeOptions(Bottle &b, Vector &poss, Vector &vels)
{
        if (b.check("poss","Getting torso home poss"))
        {
            Bottle &grp=b.findGroup("poss");
            int sz=grp.size()-1;
            int len=sz>3?3:sz;

            for (int i=0; i<len; i++){
                poss[i]=grp.get(1+i).asDouble();
            }
        }

        if (b.check("vels","Getting torso home vels"))
        {
            Bottle &grp=b.findGroup("vels");
            int sz=grp.size()-1;
            int len=sz>3?3:sz;

            for (int i=0; i<len; i++)
                vels[i]=grp.get(1+i).asDouble();
        }
}


void cartControlReachAvoidThread::getArmHomeOptions(Bottle &b, Vector &poss, Vector &vels)
{
        if (b.check("poss","Getting arm home poss"))
        {
            Bottle &grp=b.findGroup("poss");
            int sz=grp.size()-1;
            int len=sz>7?7:sz;

            for (int i=0; i<len; i++){
                poss[i]=grp.get(1+i).asDouble();
            }
        }

        if (b.check("vels","Getting arm home vels"))
        {
            Bottle &grp=b.findGroup("vels");
            int sz=grp.size()-1;
            int len=sz>7?7:sz;

            for (int i=0; i<len; i++)
                vels[i]=grp.get(1+i).asDouble();
        }
}

    
void cartControlReachAvoidThread::initCartesianCtrl(Vector &sw, Matrix &lim, const int sel)
{
        ICartesianControl *icart=cartArm;
        Vector dof;
        string type;

        if (sel==LEFTARM)
        {
            if (useLeftArm)
            {
                drvCartLeftArm->view(icart);
                icart->storeContext(&startup_context_id_left);
                icart->restoreContext(0);
            }
            else
                return;

            type="left_arm";
        }
        else if (sel==RIGHTARM)
        {
            if (useRightArm)
            {
                drvCartRightArm->view(icart);
                icart->storeContext(&startup_context_id_right);
                icart->restoreContext(0);
            }
            else
                return;

            type="right_arm";
        }
        else if (armSel!=NOARM)
            type=armSel==LEFTARM?"left_arm":"right_arm";
        else
            return;

        fprintf(stdout,"*** Initializing %s controller ...\n",type.c_str());

        icart->setTrackingMode(false);
        icart->setTrajTime(trajTime);
        icart->setInTargetTol(reachTol);
        icart->getDOF(dof);

        for (size_t j=0; j<sw.length(); j++)
        {
            dof[j]=sw[j];

            if (sw[j] && (lim(j,0) || lim(j,2)))
            {
                double min, max;
                icart->getLimits(j,&min,&max);

                if (lim(j,0))
                    min=lim(j,1);

                if (lim(j,2))
                    max=lim(j,3);

                icart->setLimits(j,min,max);
                fprintf(stdout,"jnt #%d in [%g, %g] deg\n",(int)j,min,max);
            }
        }

        icart->setDOF(dof,dof);

        fprintf(stdout,"DOF's=( ");
        for (size_t i=0; i<dof.length(); i++)
            fprintf(stdout,"%s ",dof[i]>0.0?"on":"off");
        fprintf(stdout,")\n");
}
    
 
bool cartControlReachAvoidThread::checkTargetFromPortInput(Vector &target_pos, double &target_radius)
{
        Bottle *targetBottle=inportTargetCoordinates.read(false);
        if(targetBottle != NULL)
        {
            target_pos(0)=targetBottle->get(0).asDouble();
            target_pos(1)=targetBottle->get(1).asDouble();
            target_pos(2)=targetBottle->get(2).asDouble();
            target_radius = targetBottle->get(3).asDouble();
            yDebug("checkTargetFromPortInput: getting target %f %f %f and radius %f from the port:",targetBottle->get(0).asDouble(),targetBottle->get(1).asDouble(),targetBottle->get(2).asDouble(),targetBottle->get(3).asDouble());
            yDebug("checkTargetFromPortInput: getting target %s and radius %f from the port:",target_pos.toString().c_str(),target_radius);
            return true;
        }
        else{
            return false;   
        }
}
        
bool cartControlReachAvoidThread::getAvoidanceVectorsFromPort()
{
    avoidanceStruct_t avoidanceStruct;
    avoidanceStruct.skin_part = SKIN_PART_UNKNOWN;
    avoidanceStruct.x.resize(3,0.0);
    avoidanceStruct.n.resize(3,0.0);
    
    Bottle* avoidanceMultiBottle = inportAvoidanceVectors.read(false);
    if(avoidanceMultiBottle != NULL){
         yDebug("getAvoidanceVectorsFromPort(): There were %d bottles on the port.\n",avoidanceMultiBottle->size());
         for(int i=0; i< avoidanceMultiBottle->size();i++){
             Bottle* avoidanceStructBottle = avoidanceMultiBottle->get(i).asList();
             yDebug("Bottle %d contains %s", i,avoidanceStructBottle->toString().c_str());
             avoidanceStruct.skin_part =  (SkinPart)(avoidanceStructBottle->get(0).asInt());
             avoidanceStruct.x(0) = avoidanceStructBottle->get(1).asDouble();
             avoidanceStruct.x(1) = avoidanceStructBottle->get(2).asDouble();
             avoidanceStruct.x(2) = avoidanceStructBottle->get(3).asDouble();
             avoidanceStruct.n(0) = avoidanceStructBottle->get(4).asDouble();
             avoidanceStruct.n(1) = avoidanceStructBottle->get(5).asDouble();
             avoidanceStruct.n(2) = avoidanceStructBottle->get(6).asDouble();
             avoidanceVectors.push_back(avoidanceStruct);
         }
        return true;
    }
    else{
       yDebug("getAvoidanceVectorsFromPort(): no avoidance vectors on the port.") ;  
       return false;
    };
}


bool cartControlReachAvoidThread::getReachingGainFromPort()
{
    Bottle* reachingGainBottle = inportReachingGain.read(false);
    if(reachingGainBottle != NULL){
        reachingGain = reachingGainBottle->get(0).asDouble();
        yDebug("getReachingGainFromPort(): Reading %f from port and setting to global reachingGain.\n",reachingGain);
        return true;        
    }
    else{
        return false;
    }
}

bool cartControlReachAvoidThread::getAvoidanceGainFromPort()
{
    Bottle* avoidanceGainBottle = inportAvoidanceGain.read(false);
    if(avoidanceGainBottle != NULL){
        avoidanceGain = avoidanceGainBottle->get(0).asDouble();
        yDebug("getAvoidanceGainFromPort(): Reading %f from port and setting to global avoidanceGain.\n",avoidanceGain);
        return true;        
    }
    else{
        return false;
    }
}
    


void cartControlReachAvoidThread::selectArm()
{
        if (useLeftArm && useRightArm)
        {
           if(targetPos[1]>0.0){
               armSel=RIGHTARM; 
               printf("Target in right space - selecting right arm.\n");
               drvRightArm->view(encArm);
               drvRightArm->view(posArm);
               drvRightArm->view(velArm);
               drvRightArm->view(modArm);
               drvCartRightArm->view(cartArm);
           }
           else{
               armSel=LEFTARM; 
               printf("Target in left space - selecting left arm.\n");
               drvLeftArm->view(encArm);
               drvLeftArm->view(posArm);
               drvLeftArm->view(velArm);
               drvLeftArm->view(modArm);
               drvCartLeftArm->view(cartArm);
           }
         }
}
    
void cartControlReachAvoidThread::doReach()
    {
        if (useLeftArm || useRightArm)
        {
            if (state==STATE_REACH)
            {
               
                Vector xdhat, odhat, qdhat;
                Vector torsoAndArmJointDeltas; //!torso already in motor control order 
                // iKin / cartControl pitch, roll, yaw;   motorInterface - yaw, roll, pitch
                Vector qdotReach, qdotAvoid, qdotReachAndAvoid; //3 torso joint velocities (but in motor interface order! yaw, roll, pitch) + 7 arm joints
                Vector qdotArm, qdotTorso; //joint velocities in motor interface format
                              
                /**** init ***************/
              
                qdotArm.resize(armIdx.size(),0.0); //size 7 
                qdotTorso.resize(torsoAxes,0.0);
                           
                torsoAndArmJointDeltas.resize(cartNrDOF,0.0);
                qdotReach.resize(cartNrDOF,0.0);
                qdotAvoid.resize(cartNrDOF,0.0); 
                qdotReachAndAvoid.resize(cartNrDOF,0.0); 
                     
                
                /******* solve for optimal reaching configuration  **********************************/  
                //optionally set up weights - setRestWeights - optionally to give more value current joint pos (10 values, torso + arm))
                Vector x=R.transposed()*(targetPos);
                limitRange(x);
                x=R*x;
                yDebug("doReach(): reach target x %s.\n",x.toString().c_str());
                cartArm->askForPosition(x,xdhat,odhat,qdhat); //7+3, including torso, probably even if torso is off
                //  qdhat now contains target joint pos
                yDebug("xdhat from cart solver %s\n",xdhat.toString().c_str());
                yDebug("qdhat from cart solver %s\n",qdhat.toString().c_str());
           
                
                //*********** now the controller...  ***************************/
                //use minJerkVelCtrl - instance of minJerkVelCtrlForIdealPlant
                                      
                bool ret=encArm->getEncoders(arm.data()); 
                bool ret2 = encTorso->getEncoders(torso.data());
                     
                if ((!ret) && (!ret2))
                {
                    yError("Error reading encoders, check connectivity with the robot\n");
                }
                else
                {  /* use encoders */
                    yDebug("There are %d arm encoder values: %s\n",armAxes,arm.toString().c_str());
                    yDebug("There are %d torso encoder values: %s\n",torsoAxes,torso.toString().c_str());
                   
                    torsoAndArmJointDeltas[0] = qdhat[2]-torso[0];
                    torsoAndArmJointDeltas[1] = qdhat[1]-torso[1];
                    torsoAndArmJointDeltas[2] = qdhat[0]-torso[2];
                    torsoAndArmJointDeltas[3]=qdhat[3]-arm[0]; // shoulder pitch
                    yDebug("Shoulder pitch difference torsoAndArmJointDeltas[3]=qdhat[3]-encodersArm[0], %f = %f - % f\n", torsoAndArmJointDeltas[3],qdhat[3],arm[0]);
                    torsoAndArmJointDeltas[4]=qdhat[4]-arm[1]; // shoulder roll
                    torsoAndArmJointDeltas[5]=qdhat[5]-arm[2]; // shoulder yaw
                    torsoAndArmJointDeltas[6]=qdhat[6]-arm[3]; // elbow
                    torsoAndArmJointDeltas[7]=qdhat[7]-arm[4]; // wrist pronosupination
                    torsoAndArmJointDeltas[8]=qdhat[8]-arm[5]; // wrist pitch
                    torsoAndArmJointDeltas[9]=qdhat[9]-arm[6]; // wrist yaw
                    
                    yDebug("torsoAndArmJointDeltas: %s\n",torsoAndArmJointDeltas.toString().c_str());
                    qdotReach = minJerkVelCtrl->computeCmd(2.0,torsoAndArmJointDeltas); //2 seconds to reach       
                    yDebug("qdotReach: %s\n",qdotReach.toString().c_str());
                    
                }
          
                //so far, qdot based only on reaching; now combine magically with avoidance  
                                    
               
                /*
                for each avoidance vector
                    skinPart gives FoR, which is link after the point - move to first joint/link before the point
                    express point in last proximal joint; create extra link - rototranslation    
               */
                if (!avoidanceVectors.empty()){            
                       
                    // Instantiate new chain iCub::iKin::iKinChain from iCub::iKin::iCubArm                
                    iCub::iKin::iKinChain *chain;
                    iCub::iKin::iCubArm   *icub_arm;

                    if (armSel==LEFTARM)
                    {
                        icub_arm   = new iCub::iKin::iCubArm("left");
                        icub_arm->setAng(arm*CTRL_DEG2RAD);
                        chain = icub_arm->asChain();
                    }
                    else if (armSel==RIGHTARM)
                    {
                        icub_arm   = new iCub::iKin::iCubArm("right");
                        icub_arm->setAng(arm*CTRL_DEG2RAD);
                        chain = icub_arm->asChain();
                    }
                    else {
                        yError() << "armSel neither left or right; abort";
                        return;
                    }

                    // Block all the more distal joints after the joint 
                    //unsigned int dof = chain -> getDOF(); //should be 10 by default (3 + 7, with torso)
                    // if the skin part is a hand, no need to block any joints
                    if ((avoidanceVectors[0].skin_part == SKIN_LEFT_FOREARM) ||  (avoidanceVectors[0].skin_part == SKIN_RIGHT_FOREARM)){
                        chain->blockLink(10); chain->blockLink(9);//wrist joints
                        yDebug("obstacle threatening skin part %s, blocking links 9 and 10 (wrist)",SkinPart_s[avoidanceVectors[0].skin_part].c_str());
                    }
                    else if  ((avoidanceVectors[0].skin_part == SKIN_LEFT_UPPER_ARM) ||  (avoidanceVectors[0].skin_part == SKIN_RIGHT_UPPER_ARM)){
                        chain->blockLink(10); chain->blockLink(9);chain->blockLink(8);chain->blockLink(7); //wrist joints + elbow joints
                        yDebug("obstacle threatening skin part %s, blocking links 7,8,9 and 10 (wrist+elbow)",SkinPart_s[avoidanceVectors[0].skin_part].c_str());
                    }
                    //or:
                    //for (int i = dof; i > Skin_2_Link(avoidanceVectors[0].skin_part)+torsoAxes+1; --i)
                    //that is, i from 10, block all joint up to link nr. of specific skin part, + 3 for the extra torso joints in the chain
                    //so, for forearm, this is blocking 10,9... up to 7, where nr. 7 = 4 (link nr. of forearm) + 3 will be first free joint),
                    //effectively blocking the wrist joints + elbow joints
                    
                    // SetHN to move the end effector toward the point to be controlled
                    Matrix HN = eye(4);
                    computeFoR(avoidanceVectors[0].x,avoidanceVectors[0].n,HN);
                    yDebug("HN matrix: %s",HN.toString().c_str());
                    chain -> setHN(HN);
                    //  GeoJacobian to get J,  then from x_dot = J * q_dot
                    //q_dot_avoidance = pimv(J) * x_dot
                    //(maybe also here the weights)
                    // Ask for geoJacobian
                    Matrix J = chain -> GeoJacobian(); //6 rows, n columns for every avctive DOF (excluding the blocked)
                    yDebug("GeoJacobian matrix: %s",J.toString().c_str());
                    Vector xdotAvoid(3,0.0);
                    // velocity vector = speed (10cm/s) * direction of motion (the unitary version of the normal vector)
                    xdotAvoid = 0.01 * (avoidanceVectors[0].n / yarp::math::norm(avoidanceVectors[0].n));
                    yDebug("xdotAvoid: %s",xdotAvoid.toString().c_str());
                    // Compute the q_dot_avoidance
                    qdotAvoid = yarp::math::pinv(J) * xdotAvoid;
                    yDebug("qdotAvoid: %s",qdotAvoid.toString().c_str());

                    delete icub_arm; 
                    //(optionally, if you want avoidance to act more locally, you can 0 some joints from q_dot_avoidance)
                }
                /* end of avoidance *****************************/  
 
               //  combine q_dot_reach with q_dot_avoidance; such as weighted sum using gains from allostasis
                
                for(int i=0;i<cartNrDOF;i++){
                    qdotReachAndAvoid[i] = reachingGain*qdotReach[i] + avoidanceGain*qdotAvoid[i];
                    yDebug("qdotReachAndAvoid[%d] = reachingGain*qdotReach[%d] + avoidanceGain*qdotAvoid[%d]",i,i,i);
                    yDebug("%f = %f * %f + %f * %f",qdotReachAndAvoid[i],reachingGain,qdotReach[i],avoidanceGain,qdotAvoid[i]);
                }
                yDebug("qdotReachAndAvoid: %s",qdotReachAndAvoid.toString().c_str());
                /**** do the movement - send velocities ************************************/
                //need to pass from the cartArm format (3 for torso, 7 for arm) to torso (3 joints) + arm motor control (16 joints)
                
                qdotTorso[0]=qdotReachAndAvoid[0];
                qdotTorso[1]=qdotReachAndAvoid[1];
                qdotTorso[2]=qdotReachAndAvoid[2];
                
                qdotArm[0]=qdotReachAndAvoid[3];
                qdotArm[1]=qdotReachAndAvoid[4];
                qdotArm[2]=qdotReachAndAvoid[5];
                qdotArm[3]=qdotReachAndAvoid[6];
                qdotArm[4]=qdotReachAndAvoid[7];
                qdotArm[5]=qdotReachAndAvoid[8];
                qdotArm[6]=qdotReachAndAvoid[9];     
                //for(int j=7;j<armAxes;j++){
                  //qdotArm[j]=0.0; //no movement for the rest of the joints   
                //}
                              
                yDebug("velocityMove(qdotArm.data()), qdotArm: %s\n", qdotArm.toString().c_str());
                velArm->velocityMove(armIdx.size(),armIdx.getFirst(),qdotArm.data());
                yDebug("velocityMove(qdotTorso.data()), qdotTorso: %s\n", qdotTorso.toString().c_str());
                velTorso->velocityMove(qdotTorso.data());
               
              
            }
        }
    }


    
   
        


bool cartControlReachAvoidThread::computeFoR(const Vector &pos, const Vector &norm, Matrix &FoR)
{
    if (norm == zeros(3))
    {
        FoR=eye(4);
        return false;
    }
    
    // Set the proper orientation for the touching end-effector
    Vector x(3,0.0), z(3,0.0), y(3,0.0);

    z = norm;
    if (z[0] == 0.0)
    {
        z[0] = 0.00000001;    // Avoid the division by 0
    }
    y[0] = -z[2]/z[0]; //y is in normal plane
    y[2] = 1; //this setting is arbitrary
    x = -1*(cross(z,y));

    // Let's make them unitary vectors:
    x = x / yarp::math::norm(x);
    y = y / yarp::math::norm(y);
    z = z / yarp::math::norm(z);

    FoR=eye(4);
    FoR.setSubcol(x,0,0);
    FoR.setSubcol(y,0,1);
    FoR.setSubcol(z,0,2);
    FoR.setSubcol(pos,0,3);

    return true;
}
         
void cartControlReachAvoidThread::doIdle()
    {
    }
    
void cartControlReachAvoidThread::steerTorsoToHome()
    {
        yInfo("*** Homing torso\n");
        //posTorso->setRefSpeeds(torsoHomePoss.data()); //has been set in threadInit
        posTorso->positionMove(torsoHomePoss.data());
    }
    
void cartControlReachAvoidThread::checkTorsoHome(const double timeout)
    {
        fprintf(stdout,"*** Checking torso home position... ");

        bool done=false;
        double t0=Time::now();
        while (!done && (Time::now()-t0<timeout))
        {
            posTorso->checkMotionDone(&done);
            Time::delay(0.1);
        }

        fprintf(stdout,"*** done\n");
    }

    
void cartControlReachAvoidThread::stopArmJoints(const int sel)
    {
        IEncoders        *ienc=encArm;
        IPositionControl *ipos=posArm;
        string type;

        if (sel==LEFTARM)
        {
            if (useLeftArm)
            {
                drvLeftArm->view(ienc);
                drvLeftArm->view(ipos);
            }
            else
                return;

            type="left_arm";
        }
        else if (sel==RIGHTARM)
        {
            if (useRightArm)
            {
                drvRightArm->view(ienc);
                drvRightArm->view(ipos);
            }
            else
                return;

            type="right_arm";
        }
        else if (armSel!=NOARM)
            type=armSel==LEFTARM?"left_arm":"right_arm";
        else
            return;

        fprintf(stdout,"*** Stopping %s joints\n",type.c_str());
        for (size_t j=0; j<armHomeVels.length(); j++)
        {
            double fb;

            ienc->getEncoder(j,&fb);
            ipos->positionMove(j,fb);
        }
    }
     
void cartControlReachAvoidThread::steerArmToHome(const int sel)
    {
        IPositionControl *ipos=posArm;
        string type;

        if (sel==LEFTARM)
        {
            if (useLeftArm)
                drvLeftArm->view(ipos);
            else
                return;

            type="left_arm";
        }
        else if (sel==RIGHTARM)
        {
            if (useRightArm)
                drvRightArm->view(ipos);
            else
                return;

            type="right_arm";
        }
        else if (armSel!=NOARM)
            type=armSel==LEFTARM?"left_arm":"right_arm";
        else
            return;

        fprintf(stdout,"*** Homing arm %s\n",type.c_str());
        for (size_t j=0; j<armHomeVels.length(); j++)
        {
            //ipos->setRefSpeed(j,armHomeVels[j]); //has been set in threadInit
            ipos->positionMove(j,armHomePoss[j]);
        }

       
    }
    
void cartControlReachAvoidThread::checkArmHome(const int sel, const double timeout)
    {
        IPositionControl *ipos=posArm;
        string type;

        if (sel==LEFTARM)
        {
            if (useLeftArm)
                drvLeftArm->view(ipos);
            else
                return;

            type="left_arm";
        }
        else if (sel==RIGHTARM)
        {
            if (useRightArm)
                drvRightArm->view(ipos);
            else
                return;

            type="right_arm";
        }
        else if (armSel!=NOARM)
            type=armSel==LEFTARM?"left_arm":"right_arm";
        else
            return;

        fprintf(stdout,"*** Checking %s home position... ",type.c_str());

        bool done=false;
        double t0=Time::now();
        while (!done && (Time::now()-t0<timeout))
        {
            ipos->checkMotionDone(&done);
            Time::delay(0.1);
        }

        fprintf(stdout,"*** done\n");
    }
    
void cartControlReachAvoidThread::setControlModeArmsAndTorso(const int mode)
{
        IControlMode2 *mod_;
        drvLeftArm->view(mod_);
        for(int i=0; i<7; i++){
          mod_->setControlMode(i,mode);   
        };
        drvRightArm->view(mod_);
        for(int i=0; i<7; i++){
          mod_->setControlMode(i,mode);   
        };
        drvTorso->view(mod_);
        for(int i=0; i<3; i++){
          mod_->setControlMode(i,mode);   
        };       
}

    
void cartControlReachAvoidThread::limitRange(Vector &x)
    {               
        x[0]=x[0]>-0.1 ? -0.1 : x[0];       
    }
    
Matrix & cartControlReachAvoidThread::rotx(const double theta)
{
        double t=CTRL_DEG2RAD*theta;
        double c=cos(t);
        double s=sin(t);

        Rx(1,1)=Rx(2,2)=c;
        Rx(1,2)=-s;
        Rx(2,1)=s;

        return Rx;
}

Matrix & cartControlReachAvoidThread::roty(const double theta)
{
        double t=CTRL_DEG2RAD*theta;
        double c=cos(t);
        double s=sin(t);

        Ry(0,0)=Ry(2,2)=c;
        Ry(0,2)=s;
        Ry(2,0)=-s;

        return Ry;
}

Matrix & cartControlReachAvoidThread::rotz(const double theta)
{
        double t=CTRL_DEG2RAD*theta;
        double c=cos(t);
        double s=sin(t);

        Rz(0,0)=Rz(1,1)=c;
        Rz(0,1)=-s;
        Rz(1,0)=s;

        return Rz;
}
    
bool cartControlReachAvoidThread::reachableTarget(const Vector target_pos){
        if( (target_pos[0] < reachableSpace.minX) || (target_pos[0] > reachableSpace.maxX) ||
            (target_pos[1] < reachableSpace.minY) || (target_pos[1] > reachableSpace.maxY) ||
            (target_pos[2] < reachableSpace.minZ) || (target_pos[2] > reachableSpace.maxZ)){
            printf("Warning: target %f %f %f is outside of reachable area.\n",target_pos[0],target_pos[1],target_pos[2]);
            return false;          
        }
        else{
                return true;
        }
}

bool cartControlReachAvoidThread::targetCloseEnough(){
    
    Vector x,o;
    double distanceFromTarget = 0.0;
    //get current end-eff position 
    //cartArm is already pointing to the right arm
    cartArm->getPose(x,o);
    distanceFromTarget = sqrt( pow(targetPos[0]-x[0],2) + pow(targetPos[1]-x[1],2) + pow(targetPos[2]-x[2],2)); 
    yDebug("targetCloseEnough(): distance: %f, requested target: %s, current end-eff pos: %s\n",distanceFromTarget,targetPos.toString().c_str(),x.toString().c_str());
    if(distanceFromTarget<reachTol){
        yDebug("targetCloseEnough(): returning true: distanceFromTarget: %f < reachTol: %f\n",distanceFromTarget,reachTol);
        return true;
    }
    else{
        return false;
    }
    
}
    
cartControlReachAvoidThread::cartControlReachAvoidThread(int _rate, const string &_name, const string &_robot, int _v,ResourceFinder &_rf): 
RateThread(_rate),verbosity(_v),name(_name),robot(_robot),rf(_rf)
{
   
  ;
  
}

bool cartControlReachAvoidThread::threadInit()
{
       
        ts.update();
    
        inportTargetCoordinates.open(("/"+name+"/reachingTarget:i").c_str());
        inportAvoidanceVectors.open(("/"+name+"/avoidanceVectors:i").c_str());
        inportReachingGain.open(("/"+name+"/reachingGain:i").c_str());
        inportAvoidanceGain.open(("/"+name+"/avoidanceGain:i").c_str());
    
        //getting values from config file
        // general part
        Bottle &bGeneral=rf.findGroup("general");
        useLeftArm=bGeneral.check("left_arm",Value("on"),"Getting left arm use flag").asString()=="on"?true:false;
        useRightArm=bGeneral.check("right_arm",Value("on"),"Getting right arm use flag").asString()=="on"?true:false;
        trajTime=bGeneral.check("traj_time",Value(3.0),"Getting trajectory time").asDouble();
        reachTol=bGeneral.check("reach_tol",Value(0.05),"Getting reaching tolerance").asDouble();
        reachTmo=bGeneral.check("reach_tmo",Value(10.0),"Getting reach timeout").asDouble();
              
        // torso part
        torsoSwitch.resize(3,0);
        torsoLimits.resize(4,3); torsoLimits.zero();
        torsoHomePoss.resize(3,0.0);
        torsoHomeVels.resize(3,10.0);
        
        Bottle &bTorso=rf.findGroup("torso");
        if(!bTorso.isNull()){ 
            yInfo("Setting torso options from config file:");
            bTorso.setMonitor(rf.getMonitor());
            getTorsoOptions(bTorso,"pitch",0,torsoSwitch,torsoLimits);
            getTorsoOptions(bTorso,"roll",1,torsoSwitch,torsoLimits);
            getTorsoOptions(bTorso,"yaw",2,torsoSwitch,torsoLimits);  
            getTorsoHomeOptions(bTorso,torsoHomePoss,torsoHomeVels);
        }
        else{
           yInfo("Setting default torso options ([torso] in .ini file not found)");
           torsoSwitch(0)=0; torsoSwitch(1)=0; torsoSwitch(2)=0;  //all off
           //torsoSwitch(0)=1; torsoSwitch(1)=0; torsoSwitch(2)=1;  //pitch on, roll off, yaw on
           //torsoLimits(0,2)=1.0; 
           //torsoLimits(1,0)=10.0; //pitch max limit
           //torsoHomePoss and torsoHomeVels was already intialized fine during definition
        }
        yInfo("torsoSwitch: %s",torsoSwitch.toString().c_str());
        yInfo("torsoLimits: %s",torsoLimits.toString().c_str());
        yInfo("torsoHomePoss: %s",torsoHomePoss.toString().c_str());
        yInfo("torsoHomeVels: %s",torsoHomeVels.toString().c_str());
   
            
        // arm home part
        armHomePoss.resize(7,0.0); armHomeVels.resize(7,0.0);
        Bottle &bHome=rf.findGroup("home_arm");
        if(!bHome.isNull()){  
            bHome.setMonitor(rf.getMonitor());
            getArmHomeOptions(bHome,armHomePoss,armHomeVels);
            yInfo("Setting arm home from config file:");           
        }
        else{
            yInfo("Setting default arm home values (home_arm in .ini file not found)");
            armHomePoss[0]=-30.0; armHomePoss[1]=30.0; armHomePoss[2]=0.0; armHomePoss[3]=45.0; armHomePoss[4]=0.0; armHomePoss[5]=0.0; armHomePoss[6]=0.0;
            armHomeVels[0]=10.0; armHomeVels[1]=10.0; armHomeVels[2]=10.0; armHomeVels[3]=10.0; armHomeVels[4]=10.0; armHomeVels[5]=10.0; armHomeVels[6]=10.0;
        }
        yInfo("armHomePoss: %s",armHomePoss.toString().c_str());
        yInfo("armHomeVels: %s",armHomeVels.toString().c_str());
      
        newTargetFromPort = false;
        newTargetFromRPC = false;
        
        targetPosFromPort.resize(3,0.0);
        targetPosFromRPC.resize(3,0.0);
        targetPos.resize(3,0.0);
       
        //TODO add to config file
        reachableSpace.minX=-0.4; reachableSpace.maxX=-0.2;
        reachableSpace.minY=-0.3; reachableSpace.maxY=0.3;
        reachableSpace.minZ=-0.1; reachableSpace.maxZ=0.1;
        
        string fwslash="/";

        // open remote_controlboard drivers
        Property optTorso("(device remote_controlboard)");
        Property optLeftArm("(device remote_controlboard)");
        Property optRightArm("(device remote_controlboard)");

        optTorso.put("remote",(fwslash+robot+"/torso").c_str());
        optTorso.put("local",(fwslash+name+"/torso").c_str());

        optLeftArm.put("remote",(fwslash+robot+"/left_arm").c_str());
        optLeftArm.put("local",(fwslash+name+"/left_arm").c_str());

        optRightArm.put("remote",(fwslash+robot+"/right_arm").c_str());
        optRightArm.put("local",(fwslash+name+"/right_arm").c_str());

        drvTorso=new PolyDriver;
        if (!drvTorso->open(optTorso))
        {
            threadRelease();
            return false;
        }

        if (useLeftArm)
        {
            drvLeftArm=new PolyDriver;
            if (!drvLeftArm->open(optLeftArm))
            {
                threadRelease();
                return false;
            }
        }

        if (useRightArm)
        {
            drvRightArm=new PolyDriver;
            if (!drvRightArm->open(optRightArm))
            {
                threadRelease();
                return false;
            }
        }

        // open cartesiancontrollerclient drivers
        Property optCartLeftArm("(device cartesiancontrollerclient)");
        Property optCartRightArm("(device cartesiancontrollerclient)");
       
        optCartLeftArm.put("remote",(fwslash+robot+"/cartesianController/left_arm").c_str());
        optCartLeftArm.put("local",(fwslash+name+"/left_arm/cartesian").c_str());
    
        optCartRightArm.put("remote",(fwslash+robot+"/cartesianController/right_arm").c_str());
        optCartRightArm.put("local",(fwslash+name+"/right_arm/cartesian").c_str());

      
        if (useLeftArm)
        {
            drvCartLeftArm=new PolyDriver;
            if (!drvCartLeftArm->open(optCartLeftArm))
            {
                threadRelease();
                return false;
            }
           
        }

        if (useRightArm)
        {
            drvCartRightArm=new PolyDriver;
            if (!drvCartRightArm->open(optCartRightArm))
            {
                threadRelease();
                return false;
            }

            
        }

        // open views
      
        drvTorso->view(encTorso);
        drvTorso->view(posTorso);
        drvTorso->view(velTorso);
        drvTorso->view(modTorso);
        
        if (useLeftArm)
        {
            drvLeftArm->view(encArm);
            drvLeftArm->view(posArm);
            drvLeftArm->view(velArm);
            drvLeftArm->view(modArm);
            drvCartLeftArm->view(cartArm);
            armSel=LEFTARM;
        }
        else if (useRightArm)
        {
            drvRightArm->view(encArm);
            drvRightArm->view(posArm);
            drvRightArm->view(velArm);
            drvRightArm->view(modArm);
            drvCartRightArm->view(cartArm);
            armSel=RIGHTARM;
        }
        else
        {
            encArm=NULL;
            posArm=NULL;
            velArm=NULL;
            cartArm=NULL;
            armSel=NOARM;
        }

        encArm->getAxes(&armAxes);
        arm.resize(armAxes,0.0);
        encTorso->getAxes(&torsoAxes);
        torso.resize(torsoAxes,0.0);

         
        /* set reference speeds for position controllers (used for homing) */ 
        posTorso->setRefSpeeds(torsoHomeVels.data());
        IPositionControl *posArm_;
        drvLeftArm->view(posArm_);
        posArm_->setRefSpeeds(armHomeVels.data());
        drvRightArm->view(posArm_);
        posArm_->setRefSpeeds(armHomeVels.data());
             
        
        /* we need to set reference accelerations for the velocityMove to be used later */
        /* profile, 50 degrees/sec^2 */
        int k;
        Vector tmp_acc_arm;
        tmp_acc_arm.resize(armAxes,0.0);
        for (k = 0; k < armAxes; k++) {
            tmp_acc_arm[k] = REF_ACC;
        }
        IVelocityControl2 *velArm_;
        drvLeftArm->view(velArm_);
        velArm_->setRefAccelerations(tmp_acc_arm.data());
        drvRightArm->view(velArm_);
        velArm_->setRefAccelerations(tmp_acc_arm.data());
         
        for(int j=0;j<7;j++){
            armIdx.push_back(j); 
        }
                
        int l;
        Vector tmp_acc_torso;
        tmp_acc_torso.resize(torsoAxes,0.0);
        for (l = 0; l < torsoAxes; l++) {
            tmp_acc_torso[l] = REF_ACC;
        }
        velTorso->setRefAccelerations(tmp_acc_torso.data());
        
        targetPos.resize(3,0.0);
        R=Rx=Ry=Rz=eye(3,3);

        initCartesianCtrl(torsoSwitch,torsoLimits,LEFTARM);
        initCartesianCtrl(torsoSwitch,torsoLimits,RIGHTARM);
        cartArm->getDOF(cartDOFconfig);
        cartNrDOF = cartDOFconfig.length();
        yInfo("cartArm DOFs [%s]\n",cartDOFconfig.toString().c_str());  // [0 0 0 1 1 1 1 1 1 1] will be printed out if torso is off 
        
        // steer the robot to the initial configuration
        setControlModeArmsAndTorso(VOCAB_CM_POSITION);
        steerTorsoToHome();
        steerArmToHome(LEFTARM);
        steerArmToHome(RIGHTARM);
     
        wentHome=false;
        state=STATE_IDLE;

        minJerkVelCtrl = new  minJerkVelCtrlForIdealPlant(threadPeriod,cartNrDOF); //3 torso + 7 arm
        
        //TODO can be  moved to config file, but will be eventually obtained from allostatic control
        reachingGain = 1.0; 
        avoidanceGain = 0.0; 
        
        return true;
      
  
}

void cartControlReachAvoidThread::run()
{
     yDebug("*************   run() ************************************");
     ts.update();
    
     bool newTarget = false;
     avoidanceVectors.clear();
     
     newTargetFromPort =  checkTargetFromPortInput(targetPosFromPort,targetRadius);
     if(newTargetFromPort || newTargetFromRPC){ //target from RPC is set asynchronously
            newTarget = true;
            if (newTargetFromRPC){ //RPC has priority
                printf("run: setting new target from rpc: %f %f %f \n",targetPos[0],targetPos[1],targetPos[2]);
                newTargetFromRPC = false;
            }
            else{
                targetPos = targetPosFromPort;
                printf("run: setting new target from port: %f %f %f \n",targetPos[0],targetPos[1],targetPos[2]);
                newTargetFromPort = false;
            }
     }
     
    getReachingGainFromPort();
    getAvoidanceGainFromPort();
    getAvoidanceVectorsFromPort(); //fills up global avoidanceVectors vector
    
    //debug code
    vector<avoidanceStruct_t>::const_iterator it;
    for(it=avoidanceVectors.begin(); it!=avoidanceVectors.end(); it++)
    {
         yDebug("run(): Avoidance struct read from port: SkinPart:%s, x,y,z: %s, n1,n2,n3: %s",SkinPart_s[it->skin_part].c_str(),it->x.toString().c_str(),it->n.toString().c_str());
    }
    
   
    
    if (state==STATE_IDLE){
        yDebug("run(): STATE_IDLE\n");
        if (newTarget){
            if (reachableTarget(targetPos)){
                printf("--- Got new target => REACHING\n"); 
                state = STATE_REACH;
                reachTimer = Time::now();
            }
            else{
                printf("Target (%f %f %f) is outside of reachable space - x:<%f %f>, y:<%f %f>, z:<%f %f>\n",
                    targetPos[0],targetPos[1],targetPos[2],reachableSpace.minX,reachableSpace.maxX,reachableSpace.minY,
                    reachableSpace.maxY,reachableSpace.minZ,reachableSpace.maxZ);
                }
            }
            //else - remain idle
        }
    else if(state==STATE_REACH){
        yDebug("run(): STATE_REACH\n");
        selectArm(); 
        setControlModeArmsAndTorso(VOCAB_CM_VELOCITY);
        doReach();
        
    }
          
    /*else if(state == STATE_CHECKMOTIONDONE)
    {
        yDebug("run(): STATE_CHECKMOTIONDONE\n");
        bool done = false;
        done = targetCloseEnough();
        //cartArm->checkMotionDone(&done); //cannot be used here - we are controlling manually with velocity
            if (!done) 
            {
                if( ((Time::now()-reachTimer)>reachTmo)){
                    fprintf(stdout,"--- Reach timeout => go home\n");
                    state = STATE_GO_HOME;
                }
            }
            else{ //done
                fprintf(stdout,"--- Reach done. => go IDLE\n");
                state = STATE_IDLE;
            }
    }*/
     
    else if(state==STATE_GO_HOME){
        yDebug("run(): STATE_GO_HOME\n");
        setControlModeArmsAndTorso(VOCAB_CM_POSITION);
        steerTorsoToHome();
        steerArmToHome(LEFTARM);
        steerArmToHome(RIGHTARM);
        checkTorsoHome(3.0);
        checkArmHome(LEFTARM,3.0);
        checkArmHome(RIGHTARM,3.0);
        yDebug("--- I'm home => go idle\n");
        state=STATE_IDLE;
    }
}

 

int cartControlReachAvoidThread::printMessage(const int l, const char *f, ...)
{
    if (verbosity>=l)
    {
        fprintf(stdout,"[%s] ",name.c_str());

        va_list ap;
        va_start(ap,f);
        int ret=vfprintf(stdout,f,ap);
        va_end(ap);
        return ret;
    }
    else
        return -1;
}

void cartControlReachAvoidThread::threadRelease()
{
     setControlModeArmsAndTorso(VOCAB_CM_POSITION);
     
     delete drvTorso;
     delete drvLeftArm;
     delete drvRightArm;
     delete drvCartLeftArm;
     delete drvCartRightArm;
     delete minJerkVelCtrl;
        
      
    printMessage(0,"Closing ports..\n");
    inportTargetCoordinates.interrupt();
    inportTargetCoordinates.close();
    printMessage(1,"inportTargetCoordinates successfully closed\n");
    inportAvoidanceVectors.interrupt();
    inportAvoidanceVectors .close();
    printMessage(1,"inportAvoidanceVectors successfully closed\n");
    inportReachingGain.interrupt();
    inportReachingGain .close();
    printMessage(1,"inportReachingGain successfully closed\n");
    inportAvoidanceGain.interrupt();
    inportAvoidanceGain.close();
    printMessage(1,"inportAvoidanceGain successfully closed\n");
    
}

bool cartControlReachAvoidThread::setTargetFromRPC(const Vector target_pos)
{
        for(unsigned int i=0;i<target_pos.size();i++)
        {
            targetPos[i]=target_pos[i];
        }
        //printf("reachingThread::setTargetFromRPC: Setting targetPos from RPC to %f %f %f.\n",targetPos[0],targetPos[1],targetPos[2]);
        newTargetFromRPC = true;
        return true;
}
    
bool cartControlReachAvoidThread::setHomeFromRPC()
{
    // steer the robot to the initial configuration
        state = STATE_GO_HOME;
        return true;
}
