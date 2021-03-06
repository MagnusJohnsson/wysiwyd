/*
 * Copyright (C) 2014 WYSIWYD Consortium, European Commission FP7 Project ICT-612139
 * Authors: Anne-Laure MEALIER
 * email:   anne-laure.mealier@inserm.fr
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

//#include "modules/reservoir/lrh/include/lrh.h"

#ifdef WIN32
#include <windows.h>
#define  BOOST_ALL_NO_LIB
#endif

#include "lrh.h"

bool LRH::configure(ResourceFinder &rf) {

    bool	bEveryThingisGood = true;
    moduleName = rf.check("name",
        Value("lrh"),
        "module name (string)").asString();

    sKeyWord = rf.check("keyword", Value("grammar")).toString().c_str();
    cout << "**************Context path for grammars: " << rf.getContextPath() << endl;

    /* Mode Action Performer => Meaning*/
    scorpusFileAP = rf.getContextPath().c_str();
    scorpusFileAP += rf.check("corpusFileAP", Value("/Corpus/corpus_AP.txt")).toString().c_str();
    scorpusFileSD = rf.getContextPath().c_str();
    scorpusFileSD += rf.check("corpusFileSD", Value("/Corpus/corpus_SD.txt")).toString().c_str();
    sfileResult = rf.getContextPath().c_str();
    sfileResult += rf.check("fileResult", Value("/Corpus/output.txt")).toString().c_str();
    stemporaryCorpus = rf.getContextPath().c_str();
    stemporaryCorpus += rf.check("temporaryCorpus", Value("/Corpus/temporaryCorpus.txt")).toString().c_str();
    sreservoirAP = rf.getContextPath().c_str();
    sreservoirAP += rf.check("fileAP", Value("/action_performer_PAOR.py")).toString().c_str();
    cout << "sreservoirAP : " << sreservoirAP << endl;
    sreservoirSD = rf.getContextPath().c_str();
    sreservoirSD += rf.check("fileAP", Value("/spatial_relation.py")).toString().c_str();
    sclosed_class_wordsAP = rf.check("closed_class_wordsSD", Value("after")).toString().c_str();
    sclosed_class_wordsSD = rf.check("closed_class_wordsSD", Value("after")).toString().c_str();
    smax_nr_ocw = rf.check("max_nr_ocw", Value("10")).toString().c_str();
    smax_nr_actionrelation = rf.check("max_nr_actionrelation", Value("4")).toString().c_str();
    selt_pred = rf.check("elt_pred", Value("P A O R V")).toString().c_str();

    sHand = rf.check("hand", Value("right")).toString().c_str();
    offsetGrasp = rf.check("offsetGrasp", Value("0.02")).asDouble();

    setName(moduleName.c_str());

    // Open handler port
    string sName = getName();
    handlerPortName = "/" + sName + "/rpc";

    if (!handlerPort.open(handlerPortName.c_str())) {
        cout << getName() << ": Unable to open port " << handlerPortName << endl;
        bEveryThingisGood = false;
    }

    attach(handlerPort);                  // attach to port

    //------------------------//
    //		iCub Client
    //------------------------//

    // string ttsSystem = SUBSYSTEM_SPEECH;
    iCub = new ICubClient(moduleName.c_str(), "lrh", "client.ini", true);
    iCub->opc->isVerbose = false;

    char rep = 'n';
    while (rep != 'y'&&!iCub->connect())
    {
        cout << "iCubClient : Some dependencies are not running..." << endl;
        break; //to debug
        Time::delay(1.0);
    }
    cout << "Connections done" << endl;
    iCub->opc->checkout();
    cout << "Checkout done" << endl;

    //populateOPC();

    return true;
}


bool LRH::interruptModule(){
    iCub->opc->interrupt();
    handlerPort.interrupt();

    return true;
}

bool LRH::close() {
    iCub->opc->close();
    iCub->close();
    handlerPort.close();

    return true;
}

bool LRH::respond(const Bottle& command, Bottle& reply) {
    string helpMessage = string(getName().c_str()) +
        " commands are: \n" +
        "help \n" +
        "production objectFocus \n" +
        "meaning sentence \n" +
        "quit \n";

    reply.clear();

    if (command.get(0).asString() == "quit") {
        reply.addString("quitting");
        return false;
    }
    else if (command.get(0).asString() == "help") {
        cout << helpMessage;
        reply.addString("ok");
    }
    else if (command.get(0).asString() == "production"  && command.size() == 2) {
        reply.addString("ack");
        reply.addString("OK, send the language production");
        cout << "command.get(1).asString() : " << command.get(1).asString() << endl;
        spatialRelation(command.get(1).asString());
    }
    else if (command.get(0).asString() == "meaning" && command.size() == 2) {
        reply.addString("ack");
        reply.addString("OK, send the language comprehension");
        sentenceToMeaning(command.get(1).asString());
    }

    yInfo("sending reply from rpc");
    handlerPort.reply(reply);
    return true;
}

/* Called periodically every getPeriod() seconds */
bool LRH::updateModule() {
    return true;
}

double LRH::getPeriod() {
    /* module periodicity (seconds), called implicitly by myModule */
    return 0.1;
}

bool LRH::populateOPC(){
    iCub->opc->update();
    iCub->opc->commit();
    Object* obj1 = iCub->opc->addOrRetrieveEntity<Object>("cube");

    Vector dimensionObject(3);
    dimensionObject[0] = 0.065;
    dimensionObject[1] = 0.065;
    dimensionObject[2] = 0.08;

    Vector color(3);
    color[0] = 50;
    color[1] = 100;
    color[2] = 50;

    Vector x(3);
    x[0] = -0.38;
    x[1] = 0.2;
    x[2] = 0.0016;

    //vGoal is : -0.350000	 0.200000	 0.001600
    obj1->m_ego_position = x;
    obj1->m_present = 1;
    obj1->m_dimensions = dimensionObject;
    obj1->m_color = color;

    color[0] = 0;
    color[1] = 100;
    color[2] = 200;
    Object* obj2 = iCub->opc->addOrRetrieveEntity<Object>("mouse");
    x[0] = -0.45;  //y position
    x[1] = 0.0;    //x position
    x[2] = 0.0016; //z position
    //vGoal is : -0.350000	 0.200000	 0.001600
    obj2->m_ego_position = x;
    obj2->m_present = 1;
    obj2->m_dimensions = dimensionObject;
    obj2->m_color = color;

    color[0] = 70;
    color[1] = 200;
    color[2] = 80;
    Object* obj3 = iCub->opc->addOrRetrieveEntity<Object>("croco");
    x[0] = -0.35;
    x[1] = -0.2;
    x[2] = 0.0016;
    //vGoal is : -0.350000	 0.200000	 0.001600
    obj3->m_ego_position = x;
    obj3->m_present = 1;
    obj3->m_dimensions = dimensionObject;
    obj3->m_color = color;

    color[0] = 70;
    color[1] = 700;
    color[2] = 70;
    Object* obj4 = iCub->opc->addOrRetrieveEntity<Object>("banana");
    x[0] = -0.1;
    x[1] = -0.8;
    x[2] = 0.0026;
    //vGoal is : -0.350000	 0.200000	 0.001600
    obj4->m_ego_position = x;
    obj4->m_present = 1;
    obj4->m_dimensions = dimensionObject;
    obj4->m_color = color;

    Agent* coco = iCub->opc->addOrRetrieveEntity<Agent>("Michel");
    coco->m_ego_position[0] = -1.2;
    coco->m_ego_position[2] = 0.60;
    coco->m_present = 1;
    iCub->opc->commit();

    return true;
}

// Understanding
bool LRH::sentenceToMeaning(string sentence){
    copyPastTrainFile(scorpusFileAP.c_str(), stemporaryCorpus.c_str());
    createTest(stemporaryCorpus.c_str(), sentence);
    callReservoir(sreservoirAP, sclosed_class_wordsAP);
    return true;
}

// Production
bool LRH::meaningToSentence(string meaning){
    copyPastTrainFile(scorpusFileSD.c_str(), stemporaryCorpus.c_str());
    createTest(stemporaryCorpus.c_str(), meaning);
    callReservoir(sreservoirSD, sclosed_class_wordsSD);
    return true;
}

bool LRH::callReservoir(string fPython, string closed_class_words)
{
    std::string l_pythonCmd("python " + fPython);
    std::string l_pythonCall = l_pythonCmd + " " + stemporaryCorpus + " " + sfileResult + " " + closed_class_words + " " + smax_nr_ocw + " " + smax_nr_actionrelation + " " + selt_pred;
    //std::cout << "l_pythonCall : " << l_pythonCall << std::endl;

    system(l_pythonCall.c_str());

    return true;
}


//
string LRH::openResult(const char* fileNameIn)
{
    ifstream in;
    in.open(fileNameIn);
    string str;
    getline(in, str);
    cout << str << endl;
    in.close();

    return str;
}

int LRH::createTest(const char* filename, string sMeaningSentence)
{
    cout << filename << "    " << "sMeaningSentence" << endl;
    ofstream file;
    file.open(filename, ios::app);
    file << "</train data>" << endl;
    file << "<test data>" << endl;
    file << sMeaningSentence << endl;
    file << "</test data>" << endl;
    file.close();

    return true;
}

int LRH::copyPastTrainFile(const char* fileNameIn, const char* fileNameOut)
{
    ifstream in;
    ofstream out;
    in.open(fileNameIn);
    out.open(fileNameOut, ios::out | ios::trunc);
    string str;
    while (getline(in, str))
    {
        out << str << endl;
    }
    in.close();
    out.close();

    return true;
}

bool LRH::AREactions(vector<string> seq)
{
    string sPredicat, sObject, sLocation, sadverbs;
    float ftime;
    sPredicat = seq[0];
    sObject = seq[1];
    sLocation = seq[2];
    sadverbs = seq[3];

    if (sadverbs == "slowly"){
        ftime = 4.0;
    }
    else if (sadverbs == "quickly"){
        ftime = 0.0;
    }
    else{
        ftime = 2.0;
    }

    if (sPredicat == "none")
    {
        cout << "Error in reservoirHandler::AREactions | sPredicat == none" << endl;
        return false;
    }

    // GET LOCATION OF THE OBJECT IN THE OPC + OFFSET IN Z
    iCub->opc->update();
    //RTObject *rtObject = dynamic_cast<RTObject*>(iCub->opc->getEntity(sObject));
    Object *rtObject = iCub->opc->addEntity<Object>(sObject);
    Vector value(4);
    value = rtObject->m_ego_position;
    value[2] += offsetGrasp;
    cout << sObject << " is at: " << value.toString() << endl;

    bool success = true;

    if (rtObject->m_present)
    {

        // GRASP BEGIN
        if (sPredicat == "put" || sPredicat == "take" || sPredicat == "grasp")
        {
            Bottle bHand(sHand);
            bHand.addString("still");
            cout << "sHand : " << sHand << endl;

            Object* Location = dynamic_cast<Object*>(iCub->opc->getEntity(sLocation));
            Vector vGoal = Location->m_ego_position;
            vGoal[2] += offsetGrasp;

            cout << "vGoal is : " << vGoal.toString() << endl;

            // DROP ON LOCATION
            if (sLocation != " ")
            {
                Time::delay(ftime);
                bool grasped = iCub->getARE()->take(value, bHand, false);
                cout << (grasped ? "grasped!" : "missed!") << endl;

                success &= grasped;
                Time::delay(ftime);

                if (grasped){
                    Bottle opts("over still " + sHand);
                    bool dropped = iCub->getARE()->dropOn(vGoal, opts, false);
                    cout << (dropped ? "dropped!" : "missed!") << endl;
                    Time::delay(ftime);
                    iCub->getARE()->home();
                    success &= dropped;
                }

            }
            // DROP WITHOUT LOCATION
            else
            {
                bool grasped = iCub->getARE()->take(value, bHand, false);
                cout << (grasped ? "grasped!" : "missed!") << endl;
                Time::delay(ftime);
                success &= grasped;

                if (grasped)
                {
                    iCub->release(value, bHand);
                }
                Time::delay(ftime);
                iCub->home(bHand.toString());
            }
            // END GRASP
        }

        // PUSH
        else if (sPredicat == "push")
        {
            sLocation == "right" ? sHand = "left" : sHand = "right";

            Bottle bHand(sHand);
            cout << "sHand : " << sHand << endl;
            Time::delay(ftime);
            bool pushed = iCub->getARE()->push(value, bHand, false);
            cout << (pushed ? "pushed!" : "missed!") << endl;
            Time::delay(ftime);
            success &= pushed;
            iCub->getARE()->home();
        }

        // POINT
        else if (sPredicat == "point")
        {
            Time::delay(ftime);
            value[1] < 0.0 ? sHand = "left" : sHand = "right";
            Bottle bHand(sHand);
            cout << "sHand : " << sHand << endl;

            bool pointed = iCub->getARE()->point(value, bHand);
            cout << (pointed ? "pointed!" : "missed!") << endl;

            success &= pointed;
            Time::delay(ftime);
            iCub->getARE()->home(sHand);
        }

    }
    else
    {
        //iCub->say(sObject + " is not present");
        cout << sObject << " is not present ! " << endl;
        success = false;
    }

    cout << "Result of the action: " << (success ? "success!" : "missed!") << endl;

    return success;
}



bool LRH::spatialRelation(string sObjectFocus)
{
    iCub->opc->update();
    iCub->opc->checkout();
    std::list<Entity*> PresentObjects = iCub->opc->EntitiesCache();
    std::vector<Object> PresentRtoBefore;

    yDebug() << "before loop into present object";
    for (std::list<Entity*>::iterator itE = PresentObjects.begin(); itE != PresentObjects.end(); itE++)
    {
        if ((*itE)->isType(EFAA_OPC_ENTITY_OBJECT))
        {
            Object rto;
            rto.fromBottle((*itE)->asBottle());
            if (rto.m_present)PresentRtoBefore.push_back(rto);
        }
    }


    yDebug() << "check if at least 2 obj present";
    if (PresentObjects.size() < 2)
    {
        iCub->say("Dude, I was expecting more than 3 objects... ");
        return false;
    }

    //get the focus object
    //string sObjectFocus = "circle";
    //double maxSalience = 0.0;
    //string sObjectFocus = "none";
    if (sobjectFocusChanged.empty())
    {
        double maxSalience = 0.4;
        yDebug() << "obj focus is empty";
        for (std::vector<Object>::iterator itRTO = PresentRtoBefore.begin(); itRTO != PresentRtoBefore.end(); itRTO++)
        {
            yDebug() << "loop in present RTO";
            if (itRTO->m_saliency > maxSalience)
            {
                yDebug() << "new max sliency";
                maxSalience = itRTO->m_saliency;
                sObjectFocus = itRTO->name();
            }
        }

        if (maxSalience == 0.)
        {
            return false;
        }
    }
    else{
        sObjectFocus = sobjectFocusChanged;
    }

    cout << "In spatialRelation" << endl;

    if (PresentRtoBefore.size() == 2)
    {

        yDebug() << "== than 2 obj present";
        double deltaX = 0.0;
        double deltaY = 0.0;
        int iFactor;
        (PresentRtoBefore[0].name() == sObjectFocus) ? iFactor = 1 : iFactor = -1;
        yDebug() << "weird if done";
        deltaX = iFactor*(PresentRtoBefore[1].m_ego_position[0] - PresentRtoBefore[0].m_ego_position[0]);
        deltaY = iFactor*(PresentRtoBefore[1].m_ego_position[1] - PresentRtoBefore[0].m_ego_position[1]);

        string sLocation;
        (deltaY > 0) ? sLocation = "right" : sLocation = "left";
        yDebug() << "second weird if done";
        string sRelative;
        (iFactor == 1) ? sRelative = (PresentRtoBefore[1].name()) : sRelative = (PresentRtoBefore[0].name());
        cout << "I understood :" << endl << sObjectFocus << "\t" << sLocation << "\t" << sRelative << endl;
        sdataTestSD = sLocation + " " + sObjectFocus + " " + sRelative;

    }



    else    // case of 3 objects
    {
        Object rtFocus,
            rtRelative1,
            rtRelative2;
        bool bFirstRelative = true;
        for (unsigned int i = 0; i < 3; i++)
        {
            yDebug() << "in loop because  more than 3 obj";
            if (PresentRtoBefore[i].name() != sObjectFocus)
            {
                yDebug() << "if 1";
                bFirstRelative ? rtRelative1 = PresentRtoBefore[i] : rtRelative2 = PresentRtoBefore[i];
                bFirstRelative = false;
            }
            else
            {
                yDebug() << "else 1";
                rtFocus = PresentRtoBefore[i];
            }
        }

        double deltaX1; // difference btw focus and relative1
        double deltaX2; // difference btw focus and relative2

        deltaX1 = rtRelative1.m_ego_position[1] - rtFocus.m_ego_position[1];
        deltaX2 = rtRelative2.m_ego_position[1] - rtFocus.m_ego_position[1];

        string sLocation1;
        string sLocation2;

        string sRelative1 = rtRelative1.name();
        string sRelative2 = rtRelative2.name();

        (deltaX1 > 0) ? sLocation1 = "right" : sLocation1 = "left";
        (deltaX2 > 0) ? sLocation2 = "right" : sLocation2 = "left";

        cout << "I understood : " << sLocation1 << "\t" << sObjectFocus << "\t" << sRelative1 << endl;
        cout << "and          : " << sLocation2 << "\t" << sObjectFocus << "\t" << sRelative2 << endl;

        string sfocusHierarchy = "<o> [A-P-O-_-_-_-_-_-_][A-_-_-P-O-_-_-_-_] <o>";
        sdataTestSD = sLocation1 + " " + sObjectFocus + " " + sRelative1 + "," + sLocation2 + " " + sObjectFocus + " " + sRelative2 + " " + sfocusHierarchy;

        // spatial location
        cout << "sdataTestSD : " << sdataTestSD << endl;
        meaningToSentence(sdataTestSD);
        string result = openResult(sfileResult.c_str());
        iCub->say("I have discoverd a new object " + sObjectFocus);
        iCub->say("And now I can say you that");
        iCub->say(result);

        // distance
        //the objectFocus is closer/further the object1 than the object2
        string sproximity;
        (deltaX1 > deltaX2) ? sproximity = "closer" : sproximity = "further";
        sfocusHierarchy = "<o> [A-P-O-R-_-_-_-_-_][_-_-_-_-_-_-_-_-_] <o>";
        sdataTestSD = sproximity + " " + sObjectFocus + " " + sRelative1 + " " + sRelative2 + " " + sfocusHierarchy;
        cout << "sdataTestSD : " << sdataTestSD << endl;
        meaningToSentence(sdataTestSD);
        result = openResult(sfileResult.c_str());
        iCub->say("I learned also that");
        iCub->say(result);
    }
    return true;
}
