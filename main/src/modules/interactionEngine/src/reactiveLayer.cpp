#include <algorithm>    // std::random_shuffle
#include "reactiveLayer.h"

bool ReactiveLayer::close()
{
    //iCub->getReactableClient()->SendOSC(yarp::os::Bottle("/event reactable pong stop"));
    iCub->close();
    delete iCub;

    return true;
}

int ReactiveLayer::openPorts(string driveName,int d)
{
    rpc_ports.resize(rpc_ports.size()+1);
    outputM_ports.resize(outputM_ports.size()+1);
    outputm_ports.resize(outputm_ports.size()+1);

    //Create Ports
    string portName = "/" + moduleName + "/" + driveName;
    
    rpc_ports[d] = new Port;
    outputm_ports[d] = new BufferedPort<Bottle>;
    outputM_ports[d] = new BufferedPort<Bottle>;
    
    string pn = portName + "/rpc:o";
    string targetPortName = "/" + homeo_name + "/" + driveName + "/min:o";
    /*
    cout << "Configuring port " <<d<< " : "<< pn << " ..." << endl;
    if (!rpc_ports[d]->open((pn)))
    {
        cout << getName() << ": Unable to open port " << pn << endl;
    }
    

    while(!Network::connect(targetPortName,portName))
        {
            cout<<"Setting up homeostatic connections... "<< targetPortName << " " << portName <<endl;
            yarp::os::Time::delay(0.5);
        }

    */
    pn = portName + "/min:i";
    cout << "Configuring port " <<d<< " : "<< pn << " ..." << endl;
    if (!outputm_ports[d]->open((pn)))
    {
        cout << getName() << ": Unable to open port " << pn << endl;
    }
    targetPortName = "/" + homeo_name + "/" + driveName + "/min:o";
    yarp::os::Time::delay(0.1);
    while(!Network::connect(targetPortName,pn)){
        cout<<"Setting up homeostatic connections... "<< targetPortName << " " << pn <<endl;
        yarp::os::Time::delay(0.5);
    }
    pn = portName + "/max:i";
    cout << "Configuring port " <<d<< " : "<< pn << " ..." << endl;
    yarp::os::Time::delay(0.1);
    if (!outputM_ports[d]->open((pn)))
    {
        cout << getName() << ": Unable to open port " << pn << endl;
    }
    yarp::os::Time::delay(0.1);
    targetPortName = "/" + homeo_name + "/" + driveName + "/max:o";
    while(!Network::connect(targetPortName,pn))
    {cout<<"Setting up homeostatic connections... "<< targetPortName << " " << pn <<endl;yarp::os::Time::delay(0.5);}

    return 42;
}

bool ReactiveLayer::configure(yarp::os::ResourceFinder &rf)
{
    moduleName = rf.check("name",Value("ReactiveLayer")).asString();
    setName(moduleName.c_str());

    cout<<moduleName<<": finding configuration files..."<<endl;
    period = rf.check("period",Value(0.1)).asDouble();

    //Create an iCub Client and check that all dependencies are here before starting
    bool isRFVerbose = false;
    iCub = new ICubClient(moduleName,"reactiveLayer","client.ini",isRFVerbose);
    iCub->opc->isVerbose = false;
    char rep = 'n';
    while (rep!='y'&&!iCub->connect())
    {
        cout<<"iCubClient : Some dependencies are not running..."<<endl;
        //cout<<"Continue? y,n"<<endl;
        //cin>>rep;
        break; //to debug
        Time::delay(1.0);
    }

    finding = false;
    pointing = false;

    //Set the voice

    std::string ttsOptions = rf.check("ttsOptions", yarp::os::Value("iCubina 85.0")).asString();
    //if (iCub->getSpeechClient())
    //iCub->getSpeechClient()->SetOptions(ttsOptions);

    //Configure the various components
    configureOPC(rf);
    configureAllostatic(rf);
    //configureTactile(rf);
    configureSalutation(rf);

    //Temporal ears configuration
    string p_name = "/" + moduleName + "/ears:o";
    ears_port.open(p_name);
    while (!Network::connect(p_name,"/ears/rpc"))
        {cout<<"Setting up ears connection... "<< "/ears/rpc" <<endl;yarp::os::Time::delay(0.5);}

    last_time = yarp::os::Time::now();

    cout<<"Configuration done."<<endl;

    rpc.open ( ("/"+moduleName+"/rpc"));
    attach(rpc);
    while (!Network::connect("/ears/reactive:o","/"+moduleName +"/rpc"))
        {cout<<"Setting up ears connection... "<< "/ears/reactive:o" <<endl;yarp::os::Time::delay(0.5);}

    //Initialise timers
    lastFaceUpdate = Time::now();
    physicalInteraction = false;
    someonePresent = false;

    Rand::init();

    //iCub->getReactableClient()->SendOSC(yarp::os::Bottle("/event reactable pong start"));

    yInfo("Init done");

    
    Bottle cmd;
    cmd.clear();
    cmd.addString("par");
    cmd.addString("tagging");
    cmd.addString("val");
    cmd.addDouble(0.5);
    Bottle rpl;
    //rpc_ports[0]->write(cmd,rpl);

    cout << rpl.toString()<<endl;

    iCub->home();

    return true;
}

void ReactiveLayer::configureSalutation(yarp::os::ResourceFinder &rf)
{
    ;
    //Initialise the gestures response
    Bottle grpSocial = rf.findGroup("SOCIAL");
    salutationLifetime = grpSocial.check("salutationLifetime", Value(15.0)).asDouble();
    Bottle *socialStimulus = grpSocial.find("stimuli").asList();

    if (socialStimulus)
    {
        for (int d = 0; d<socialStimulus->size(); d++)
        {
            string socialStimulusName = socialStimulus->get(d).asString();
            StimulusEmotionalResponse response;
            Bottle * bSentences = grpSocial.find((socialStimulusName + "-sentence")).asList();
            for (int s = 0; s<bSentences->size(); s++)
            {
                response.m_sentences.push_back(bSentences->get(s).asString());
            }
            std::string sGroupTemp = socialStimulusName;
            sGroupTemp += "-effect";
            Bottle *bEffects = grpSocial.find(sGroupTemp).asList();
            for (int i = 0; bEffects && i<bEffects->size(); i += 2)
            {
                response.m_emotionalEffect[bEffects->get(i).asString()] = bEffects->get(i + 1).asDouble();
            }
            salutationEffects[socialStimulusName] = response;
        }
    }

    //Add the relevant Entities for handling salutation
    iCub->opc->checkout();
    Action* actIs = iCub->opc->addOrRetrieveEntity<Action>("is");
    iCub->opc->commit(actIs);
    Adjective* adjSal = iCub->opc->addOrRetrieveEntity<Adjective>("saluted");
    iCub->opc->commit(adjSal);
}

void ReactiveLayer::configureAllostatic(yarp::os::ResourceFinder &rf)
{
    //The homeostatic module should be running in parallel, independent from this, so the objective of
    //this config would be to have a proper list  and connect to each port

    homeo_name = "homeostasis";
    string homeo_rpc_name = "/" + homeo_name + "/rpc";
    string to_h_rpc_name="/"+moduleName+"/toHomeoRPC:o";
    to_homeo_rpc.open(to_h_rpc_name);

    while(!Network::connect(to_h_rpc_name,homeo_rpc_name))
    {
        cout<<"Trying to connect to homeostasis..."<<endl;
        cout << "from " << to_h_rpc_name << " to " << homeo_rpc_name << endl;
        yarp::os::Time::delay(0.2);
    }

    /*//Query drive names
    Bottle cmd;
    Bottle rpl;
    rpl.clear();
    cmd.clear();
    cmd.addString("names");
    to_homeo_rpc.write(cmd,rpl);
    if (rpl.get(0).asString()!="nack")
    {
        n_drives = rpl.size();
        drive_names = rpl;
    }
    */

    //Initialise the iCub allostatic model. Drives for interaction engine will be read from IE default.ini file
    cout << "Initializing drives..."<<endl;
    Bottle grpAllostatic = rf.findGroup("ALLOSTATIC");
    cout << "HERREEE" << grpAllostatic.toString() << endl;
    drivesList = *grpAllostatic.find("drives").asList();
    //iCub->icubAgent->m_drives.clear();
    Bottle cmd;

    priority_sum = 0.;
    double priority;
    for (int d = 0; d<drivesList.size(); d++)
    {
        cmd.clear();
        string driveName = drivesList.get(d).asString();

        cmd.addString("add");
        cmd.addString("conf");
        Bottle drv;
        drv.clear();
        Bottle aux;
        aux.clear();
        aux.addString("name");
        aux.addString(driveName);
        drv.addList()=aux;
        aux.clear();
        drv.addList()=grpAllostatic;
        cmd.addList()=drv;
        Bottle rply;
        rply.clear();
        rply.get(0).asString();
        cout << cmd.toString() << endl;

        // cmd.addString("add");
        // cmd.addString("conf");
        // Bottle drv;
        // drv.clear();
        // Bottle aux;
        // aux.clear();
        // aux.addString("name");
        // aux.addString(driveName);
        // drv.addList()=aux;
        // aux.clear();
        // drv.append(grpAllostatic);
        // cmd.append(drv);//addList()=drv;
        // Bottle rply;
        // rply.clear();
        // rply.get(0).asString();
        // cout << cmd.toString() << endl;


        // while(rply.get(0).asString()!="ack")
        // {
            to_homeo_rpc.write(cmd,rply);
            cout << rply.toString()<<endl;
            // cout<<"cannot create drive "<< driveName << "..."<<endl;
        // }


        int answer = openPorts(driveName,d);
        cout << "The answer is " << answer <<endl;


        //Under effects
        StimulusEmotionalResponse responseUnder;
        Bottle * bSentences = grpAllostatic.find((driveName + "-under-sentences")).asList();
        for (int s = 0; bSentences && s<bSentences->size(); s++)
        {
            responseUnder.m_sentences.push_back(bSentences->get(s).asString());
        }
        Bottle *bChore = grpAllostatic.find((driveName + "-under-chore")).asList();
        for (int sC = 0; bChore && sC<bChore->size(); sC++)
        {
            responseUnder.m_choregraphies.push_back(bChore->get(sC).asString());
        }
        string under_port_name = grpAllostatic.check((driveName + "-under-behavior-port"), Value("None")).asString();

        cout << under_port_name << endl;

        // set drive priorities. Default to 1.
        priority = grpAllostatic.check((driveName + "-priority"), Value(1.)).asDouble();
        priority_sum += priority;
        drivePriorities.push_back(priority);

        
        if (under_port_name != "None")
        {
            responseUnder.active = true;
            string out_port_name = "/" + moduleName + "/" + driveName + "/under_action:o";
            responseUnder.output_port = new Port();
            responseUnder.output_port->open(out_port_name);
            cout << "trying to connect to " << under_port_name << endl;
            while(!Network::connect(out_port_name,under_port_name))
            {
                cout << "." << endl;
                yarp::os::Time::delay(0.5);
            }
        }else{
            responseUnder.active = false;
        }

        homeostaticUnderEffects[driveName] = responseUnder;

        //Over effects
        StimulusEmotionalResponse responseOver;
        bSentences = grpAllostatic.find((driveName + "-over-sentences")).asList();
        for (int s = 0; bSentences&& s<bSentences->size(); s++)
        {
            responseOver.m_sentences.push_back(bSentences->get(s).asString());
        }
        bChore = grpAllostatic.find((driveName + "-over-chore")).asList();
        for (int sC = 0; bChore && sC<bChore->size(); sC++)
        {
            responseOver.m_choregraphies.push_back(bChore->get(sC).asString());
        }
        string over_port_name = grpAllostatic.check((driveName + "-over-behavior-port"), Value("None")).asString();
        if (over_port_name != "None")
        {
            responseOver.active=true;
            string out_port_name = "/" + moduleName + "/" + driveName + "/over_action:o";
            responseOver.output_port = new Port();
            responseOver.output_port->open(out_port_name);
            cout << "trying to connect to " << over_port_name << endl;
            while(!Network::connect(out_port_name,over_port_name))
            {
                cout << "." << endl;
                yarp::os::Time::delay(0.5);
            }
        }else{
            responseOver.active = false;
        }

        homeostaticOverEffects[driveName] = responseOver;
    }
    cout << "done" << endl;

    // Normalize drive priorities
    if ( ! Normalize(drivePriorities))
        cout << "Error: Drive priorities sum up to 0." << endl;

    cout << "Drive priorities: " << drivePriorities << endl;

    //Initialise the iCub emotional model
    cout << "Initializing emotions...";
    Bottle grpEmotions = rf.findGroup("EMOTIONS");
    Bottle *emotionsList = grpEmotions.find("emotions").asList();
    double emotionalDecay = grpEmotions.check("emotionalDecay", Value(0.1)).asDouble();

    //iCub->icubAgent->m_emotions_intrinsic.clear();
    if (emotionsList)
    {
        for (int d = 0; d<emotionsList->size(); d++)
        {
            string emoName = emotionsList->get(d).asString();
            //iCub->icubAgent->m_emotions_intrinsic[emoName] = 0.0;
        }
    }
    cout << "done" << endl;

    faceUpdatePeriod = grpEmotions.check("expressionUpdatePeriod", Value(0.5)).asDouble();

    cout << "Commiting iCubAgent...";
    iCub->commitAgent();
    cout << "done." << endl;

    InternalVariablesDecay* decayThread;
    decayThread = new InternalVariablesDecay(500, emotionalDecay);
    decayThread->start();
}

bool ReactiveLayer::createTemporalDrive(string name, double prior)
{
    //Add new drive with default configuration
    cout<<"!!!adding drive"<<endl;
    Bottle cmd,rpl;
    cmd.clear();
    cmd.addString("add");
    cmd.addString("new");
    cmd.addString(name);
    to_homeo_rpc.write(cmd);
    yarp::os::Time::delay(2.0);
    drivesList.addString(name);
    cout << "!!!making drive boolean"<<endl;
    //Remove decay: it will always be either needed or not
    cmd.clear();
    cmd.addString("par");
    cmd.addString(name);
    cmd.addString("dec");
    cmd.addDouble(0.0);
    cout << cmd.toString()<<endl;
    to_homeo_rpc.write(cmd);
    cout << "!!!sending drive out of the CZ"<<endl;
    yarp::os::Time::delay(2.0);
    
    //Set drive out of the boundary
    cmd.clear();
    cmd.addString("par");
    cmd.addString(name);
    cmd.addString("val");
    cmd.addDouble(0.01);
    to_homeo_rpc.write(cmd);
    yarp::os::Time::delay(2.0);

    //Change priorities
    cout << "!!!changing priorities"<<endl;
    double priority = priority_sum*prior;
    priority_sum += priority;
    drivePriorities.push_back(priority);
    Normalize(drivePriorities);

    //temporal drives are only on this vector
    temporalDrivesList.push_back(name);


    //Is everything alright¿?
    return true;

}

bool ReactiveLayer::Normalize(vector<double>& vec) {
    double sum = 0.;
    for (unsigned int i = 0; i<vec.size(); i++)
        sum += vec[i];
    if (sum == 0.)
        return false;
    for (unsigned int i = 0; i<vec.size(); i++)
        vec[i] /= sum;
    return true;
}

bool ReactiveLayer::updateModule()
{
    cout<<".";
    cout << yarp::os::Time::now()<<endl;
    //handleSalutation(someonePresent);
    //physicalInteraction = handleTactile();
    confusion = handleTagging();
    cout << "confusion handled "<< endl;
    cout << confusion << endl;

    if (searchList.size()>0){
        cout << "there are elements to search!!!"<<endl;
        finding = handleSearch();
    }
        
    if (pointList.size()>0){
        if (finding)
            pointing = handlePoint();
        cout << "there are elements to point!!!"<<endl;
    }
    //learning = handlePointing();
    updateAllostatic();
    //updateEmotions();


    return true;
}

bool ReactiveLayer::handlePoint()
{
    iCub->opc->checkout();
    yInfo() << " [handlePoint] : opc checkout";
    list<Entity*> lEntities = iCub->opc->EntitiesCache();
    string e_name = pointList[pointList.size()-1];
    bool pointRPC = false;
    for (list<Entity*>::iterator itEnt = lEntities.begin(); itEnt != lEntities.end(); itEnt++)
    {
        string sName = (*itEnt)->name();
        

        cout << "Checking entity: " << e_name << " to " << sName<<endl;
        if (sName == e_name) {
            if ((*itEnt)->entity_type() == "object")//|| (*itEnt)->entity_type() == "agent" || (*itEnt)->entity_type() == "rtobject")
            {
                yInfo() << "I already knew that the object was in the opc: " << sName;
                Object* o = dynamic_cast<Object*>(*itEnt);
                if(o && o->m_present) {
                    //pointRPC=true;
                    cout << "I'd like to point " << e_name <<endl;
                    Object* obj1 = iCub->opc->addOrRetrieveEntity<Object>(e_name);
                    string sHand = "right";
                    if (obj1->m_ego_position[1]<0) sHand = "left";
                        Bottle bHand(sHand);
                    iCub->point(e_name, bHand);
                    iCub->say("oh! this is a " + e_name);
                    yarp::os::Time::delay(2.0);
                    iCub->home();
                    pointList.pop_back();
                    return true;
                }

            }
            //pointRPC=true;
        }
    }/*else{
            bool s_found = false;
            for ( int i = 0; i<searchList.size();i++)
            {
                string s_name = searchList[i];
                if (sName == s_name)
                {
                    yInfo() << "I should first find: " << sName;
                    s_found=true;
                    string d_name = "point";
                    d_name += "_";
                    d_name += e_name;
                    for ( int j = 0; j<drivesList.size();j++)
                    {
                        if (drivesList.get(j).asString()==d_name)
                        {
                            drivePriorities[j] = 1/drivePriorities.size();
                            return false;
                        }
                    }

                }
            }
            if (!s_found)
            {
                yInfo() << "I don't know the name. I'll create a new drive: " << sName;
                
                double p = pow(1/1 ,5 ) * 100;
                string d_name = "search";
                d_name += "_";
                d_name += e_name;
                //createTemporalDrive(d_name, p);
                searchList.push_back(e_name);

            }
        }*/

    if (pointRPC)
    {
        //cout << "I'd like to point " << e_name <<endl;
        //If there is an unknown object (to see with agents and rtobjects), add it to the rpc_command bottle, and return true
        /*homeostaticUnderEffects["tagging"].rpc_command.clear();
        homeostaticUnderEffects["tagging"].rpc_command.addString("point");
        //homeostaticUnderEffects["pointing"].rpc_command.addString((*itEnt)->entity_type());
        homeostaticUnderEffects["tagging"].rpc_command.addString(e_name);
        Bottle rply;
        rply.clear();
        homeostaticUnderEffects["tagging"].output_port->write(homeostaticUnderEffects["pointing"].rpc_command,rply);
        cout << rply.toString() << endl;*/
        
    }else{
        cout << "Could not find the object to point.... "<<endl;
    }

    return false;
    
}

bool ReactiveLayer::handleSearch()
{
    iCub->opc->checkout();
    yInfo() << " [handleSearch] : opc checkout";
    list<Entity*> lEntities = iCub->opc->EntitiesCache();
    bool tagRPC = false;

        string e_name = searchList[searchList.size()-1];

    for (list<Entity*>::iterator itEnt = lEntities.begin(); itEnt != lEntities.end(); itEnt++)
    {
        string sName = (*itEnt)->name();
        
        //bool pointRPC = false;
        
        cout << "comparing entity: "<<e_name<<" to " << sName<<endl;

        if (sName == e_name) {
            cout << "Entity found: "<<e_name<<endl;
            if ((*itEnt)->entity_type() == "object")//|| (*itEnt)->entity_type() == "agent" || (*itEnt)->entity_type() == "rtobject")
            {
                yInfo() << "I found the entity in the opc: " << sName;
                Object* o = dynamic_cast<Object*>(*itEnt);
                if(o && o->m_present) {
                    searchList.pop_back();
                    return true;
                }
            }else{
                tagRPC = true;
            }
        }
    }
    cout << "I need to explore by name!" << endl;
                tagRPC = true;
        if (tagRPC)
        {
            // ask for the object
            cout << "send rpc to proactiveTagging"<<endl;
            Bottle rply;
            rply.clear();

            //If there is an unknown object (to see with agents and rtobjects), add it to the rpc_command bottle, and return true
            homeostaticUnderEffects["tagging"].rpc_command.clear();
            homeostaticUnderEffects["tagging"].rpc_command.addString("searchingEntity");
            //homeostaticUnderEffects["tagging"].rpc_command.addString((*itEnt)->entity_type());
            homeostaticUnderEffects["tagging"].rpc_command.addString(e_name);
            homeostaticUnderEffects["tagging"].output_port->write(homeostaticUnderEffects["tagging"].rpc_command,rply);
            cout << rply.toString() << endl;
            searchList.pop_back();
            return true;
        }
    
    //if no unknown object was found, return false
    return false;
}


bool ReactiveLayer::handlePointing()
{
    iCub->opc->checkout();
    list<Entity*> lEntities = iCub->opc->EntitiesCache();

    int counter = 0;
    for (list<Entity*>::iterator itEnt = lEntities.begin(); itEnt != lEntities.end(); itEnt++)
    {
        string sName = (*itEnt)->name();
        string sNameCut = sName;
        string delimiter = "_";
        size_t pos = 0;
        string token;
        if ((pos = sName.find(delimiter)) != string::npos) {
            token = sName.substr(0, pos);
            sName.erase(0, pos + delimiter.length());
            sNameCut = token;
        }
        // check is label is known

        if (sNameCut != "unknown") {

            if ((*itEnt)->entity_type() == "object" )//|| (*itEnt)->entity_type() == "bodypart")//|| (*itEnt)->entity_type() == "agent" || (*itEnt)->entity_type() == "rtobject")
            {
                cout << "I'd like to point " << (*itEnt)->name() <<endl;
                //If there is an unknown object (to see with agents and rtobjects), add it to the rpc_command bottle, and return true
                homeostaticUnderEffects["pointing"].rpc_command.clear();
                homeostaticUnderEffects["pointing"].rpc_command.addString("point");
                //homeostaticUnderEffects["pointing"].rpc_command.addString((*itEnt)->entity_type());
                homeostaticUnderEffects["pointing"].rpc_command.addString((*itEnt)->name());
                return true;
                /*
                Object* temp = dynamic_cast<Object*>(*itEnt);
                if (temp->m_saliency > highestSaliency)
                {
                    if (secondSaliency != 0.0)
                    {
                        secondSaliency = highestSaliency;
                    }
                    highestSaliency = temp->m_saliency;
                    sNameBestEntity = temp->name();
                    sTypeBestEntity = temp->entity_type();
                }
                else
                {
                    if (temp->m_saliency > secondSaliency)
                    {
                        secondSaliency = temp->m_saliency;
                    }
                }
                counter++;
                */
            }
        }
    }
    //if no unknown object was found, return false
    return counter > 0;
}
bool ReactiveLayer::handleTagging()
{
    iCub->opc->checkout();
    yInfo() << " [handleTagging] : opc checkout";
    list<Entity*> lEntities = iCub->opc->EntitiesCache();
    //vector<Entity*> vEntities;
    //copy(lEntities.begin(), lEntities.end(), vEntities.begin());
    //std::random_shuffle ( vEntities.begin(), vEntities.end() );
    //list<Entity*> lEntitiesShuffled(vEntities.begin(), vEntities.end());

    for (list<Entity*>::iterator itEnt = lEntities.begin(); itEnt != lEntities.end(); itEnt++)
    {
        string sName = (*itEnt)->name();
        string sNameCut = sName;
        string delimiter = "_";
        size_t pos = 0;
        string token;
        if ((pos = sName.find(delimiter)) != string::npos) {
            token = sName.substr(0, pos);
            sName.erase(0, pos + delimiter.length());
            sNameCut = token;
        }
        // check is label is known

        bool sendRPC = false;

        if (sNameCut == "unknown") {
            if ((*itEnt)->entity_type() == "object")//|| (*itEnt)->entity_type() == "agent" || (*itEnt)->entity_type() == "rtobject")
            {
                yInfo() << "I found an unknown entity: " << sName;
                Object* o = dynamic_cast<Object*>(*itEnt);
                if(o && o->m_present) {
                    sendRPC = true;
                }
            } else if((*itEnt)->entity_type() == "bodypart") {
                sendRPC = true;
            }
        } else {
            if ((*itEnt)->entity_type() == "bodypart" && (dynamic_cast<Bodypart*>(*itEnt)->m_tactile_number == -1 || dynamic_cast<Bodypart*>(*itEnt)->m_kinStruct_instance == -1))
                sendRPC = true;
        }

        if (sendRPC) {
            cout << "send rpc to proactiveTagging"<<endl;
            //If there is an unknown object (to see with agents and rtobjects), add it to the rpc_command bottle, and return true
            homeostaticUnderEffects["tagging"].rpc_command.clear();
            homeostaticUnderEffects["tagging"].rpc_command.addString("exploreUnknownEntity");
            homeostaticUnderEffects["tagging"].rpc_command.addString((*itEnt)->entity_type());
            homeostaticUnderEffects["tagging"].rpc_command.addString((*itEnt)->name());
            //And make a boost to the drive
            
            return true;
            /*
            Object* temp = dynamic_cast<Object*>(*itEnt);
            if (temp->m_saliency > highestSaliency)
            {
                if (secondSaliency != 0.0)
                {
                    secondSaliency = highestSaliency;
                }
                highestSaliency = temp->m_saliency;
                sNameBestEntity = temp->name();
                sTypeBestEntity = temp->entity_type();
            }
            else
            {
                if (temp->m_saliency > secondSaliency)
                {
                    secondSaliency = temp->m_saliency;
                }
            }
            counter++;
            */
        }
    }
    //if no unknown object was found, return false
    return false;
}


bool ReactiveLayer::handleSalutation(bool& someoneIsPresent)
{
    someoneIsPresent = false;
    //Handle the salutation of newcomers
    iCub->opc->checkout();
    list<Entity*> allAgents = iCub->opc->Entities(EFAA_OPC_ENTITY_TAG, "==", EFAA_OPC_ENTITY_AGENT);
    list<Relation> salutedAgents = iCub->opc->getRelations("saluted");
    list<Relation> identity = iCub->opc->getRelationsMatching("partner", "named");
    string identityName = "unknown";
    if (identity.size() > 0)
        identityName = identity.front().object();

    for (list<Entity*>::iterator currentAgentIt = allAgents.begin(); currentAgentIt != allAgents.end(); currentAgentIt++)
    {
        Agent* currentAgent = (Agent*)(*currentAgentIt);


        if (currentAgent->name() != "icub" && currentAgent->m_present)
        {
            someoneIsPresent = true;
            currentPartner = currentAgent->name();
            //cout<<"Testing salutation for "<<currentPartner<<" with name "<<identityName<<endl;

            bool saluted = false;
            for (list<Relation>::iterator it = salutedAgents.begin(); it != salutedAgents.end(); it++)
            {
                //cout<< it->toString()<<endl;
                if (it->subject() == identityName)
                {
                    //cout<<"Same agent detected... Reseting salutation lifetime"<<endl;
                    //This guy has already been saluted, we reset the lifetime
                    iCub->opc->setLifeTime(it->ID(), salutationLifetime);
                    saluted = true;
                }
            }

            if (!saluted)
            {
                iCub->look(currentPartner);
                iCub->say(salutationEffects["humanEnter"].getRandomSentence(), false);
                iCub->playChoregraphy("wave");
                if (identityName != "unknown")
                    iCub->say(identityName + "! nice to see you again!", false);
                iCub->opc->addRelation(Relation(identityName, "is", "saluted"), salutationLifetime);
                iCub->opc->commit();
                return true;
            }
        }
    }
    return false;
}

// Return the index of a drive to solve according to priorities and homeostatis levels
// Return -1 if no drive to be solved
DriveOutCZ ReactiveLayer::chooseDrive() {

    DriveOutCZ result;
    bool inCZ;
    int numOutCz = 0;
    double random, cum;
    vector<double> outOfCzPriorities(drivePriorities);

    for ( int i =0;i<drivesList.size();i++) {
        inCZ = outputm_ports[i]->read()->get(0).asDouble() <= 0 && outputM_ports[i]->read()->get(0).asDouble() <= 0;
        if (inCZ) {
            outOfCzPriorities[i] = 0.1;
        }
        else {
            numOutCz ++;
        }
        cout << "Drive " << i << ", " << drivesList.get(i).asString() << ". Priority: " << outOfCzPriorities[i] << "." << endl;
    }
    if (! numOutCz) {
        result.idx = -1;
        return result;
    }
    if ( ! Normalize(outOfCzPriorities)) {
        result.idx = -1;
        return result;
    }
    random = Rand::scalar();
    cum = outOfCzPriorities[0];
    int idx = 0;
    while (cum < random) {
        cum += outOfCzPriorities[idx + 1];
        idx++;
    }
    result.idx = idx;
    if (outputm_ports[idx]->read()->get(0).asDouble() > 0)
        result.level = UNDER;
    if (outputM_ports[idx]->read()->get(0).asDouble() > 0)
        result.level = OVER;
    return result;
}


bool ReactiveLayer::updateAllostatic()
{

    //iCub->updateAgent();

    //Update some specific drives based on the previous stimuli encountered
    if (physicalInteraction)
    {
        Bottle cmd;
        cmd.clear();
        cmd.addString("delta");
        cmd.addString("physicalInteraction");
        cmd.addString("val");
        cmd.addDouble(0.1);

        to_homeo_rpc.write(cmd);
    }


    //iCub->icubAgent->m_drives["physicalInteraction"].value += 0.1;
    if (someonePresent)
    {
        Bottle cmd;
        cmd.clear();
        cmd.addString("par");
        cmd.addString("socialInteraction");
        cmd.addString("dec");
        cmd.addDouble(-0.002);

        to_homeo_rpc.write(cmd);
    }
    //iCub->icubAgent->m_drives["socialInteraction"].value += iCub->icubAgent->m_drives["socialInteraction"].decay * 2;

    if (confusion)
    {
        Bottle cmd;
        cmd.clear();
        cmd.addString("par");
        cmd.addString("tagging");
        cmd.addString("dec");
        cmd.addDouble(0.004);
        cout << cmd.toString()<<endl;
        Bottle rply;
        rply.clear();
        to_homeo_rpc.write(cmd,rply);
        cout<<rply.toString()<<endl;

    }else{
        Bottle cmd;
        cmd.clear();
        cmd.addString("par");
        cmd.addString("tagging");
        cmd.addString("dec");
        cmd.addDouble(-0.0);
        cout << cmd.toString()<<endl;
        Bottle rply;
        rply.clear();
        to_homeo_rpc.write(cmd,rply);
        cout<<rply.toString()<<endl;

        yarp::os::Time::delay(0.2);
        cmd.clear();
        cmd.addString("par");
        cmd.addString("tagging");
        cmd.addString("val");
        cmd.addDouble(0.5);
        cout << cmd.toString()<<endl;        
        rply.clear();
        to_homeo_rpc.write(cmd,rply);
        cout<<rply.toString()<<endl;       
    }
    if (learning)
    {
        Bottle cmd;
        cmd.clear();
        cmd.addString("par");
        cmd.addString("pointing");
        cmd.addString("dec");
        cmd.addDouble(0.021);
        cout << cmd.toString()<<endl;
        Bottle rply;
        rply.clear();
        to_homeo_rpc.write(cmd,rply);
        cout<<rply.toString()<<endl;

    }else{
        Bottle cmd;
        cmd.clear();
        cmd.addString("par");
        cmd.addString("pointing");
        cmd.addString("dec");
        cmd.addDouble(0.0);
        cout << cmd.toString()<<endl;

        to_homeo_rpc.write(cmd);
    }
    //cout <<drivesList.size()<<endl;

    //TODO: Add temporal drive handling

    DriveOutCZ activeDrive = chooseDrive();

    //Maybe remove here
    //int i; // the chosen drive
    
    if (activeDrive.idx == -1) {
        cout << "No drive out of CZ." << endl;
        if ((yarp::os::Time::now()-last_time)>2.0)
                {
                last_time = yarp::os::Time::now();
                Bottle cmd;
                cmd.clear();
                cmd.addString("listen");
                cmd.addString("on");
                ears_port.write(cmd);
            }
        return true;
    }
    else
    {
        //i = activeDrive.idx;
        
                Bottle cmd;
                cmd.clear();
                cmd.addString("listen");
                cmd.addString("off");
                ears_port.write(cmd);
    }
    bool temporal = false;
    for (int j = 0;j<temporalDrivesList.size();j++)
    {
        if (drivesList.get(activeDrive.idx).asString()==temporalDrivesList[j])
            {
                temporal = true;
            }
    }
    /*if (temporal)
    {
        if (finding)
        {
            drivePriorities[i]=0;
            string s_name = searchList[searchList.size()-1];
            searchList.pop_back();
            for (int n = 0; n< pointList.size();n++)
            {
                if (pointList[n]==s_name)
                {
                    string d_name = "point";
                    d_name += "_";
                    d_name += s_name;
                    for (int m = 0; m<drivesList.size();m++)
                    {
                        if (d_name == drivesList.get(m).asString())
                        {
                            drivePriorities[m]=100;
                        }
                    }
                }
            }
            Normalize(drivePriorities);
        }
        if (pointing)
        {
            drivePriorities[i]=0;
            pointList.pop_back();
        }
    }else{*/


    cout << "Things are happening"<<endl;
    //Under homeostasis


    if (activeDrive.level == UNDER)
    {
        yInfo() << " [updateAllostatic] Drive " << activeDrive.idx << " chosen. Under level.";
        yarp::os::Time::delay(1.0);
        iCub->look("partner");
        // yarp::os::Time::delay(1.0);
        iCub->say(homeostaticUnderEffects[drivesList.get(activeDrive.idx).asString()].getRandomSentence());
        Bottle cmd;
        cmd.clear();
        cmd.addString("par");
        cmd.addString("tagging");
        cmd.addString("val");
        cmd.addDouble(0.5);
        //rpc_ports[activeDrive.idx]->write(cmd);
        to_homeo_rpc.write(cmd);

        cmd.clear();
        cmd.addString("par");
        cmd.addString("tagging");
        cmd.addString("dec");
        cmd.addDouble(0.0);
        //rpc_ports[activeDrive.idx]->write(cmd);
        to_homeo_rpc.write(cmd);

        if (homeostaticUnderEffects[drivesList.get(activeDrive.idx).asString()].active)
        {
            yInfo() << " [updateAllostatic] Command will be send";
            yInfo() <<homeostaticUnderEffects[drivesList.get(activeDrive.idx).asString()].active << homeostaticUnderEffects[drivesList.get(activeDrive.idx).asString()].rpc_command.toString();
            Bottle rply;
            rply.clear();
            homeostaticUnderEffects[drivesList.get(activeDrive.idx).asString()].output_port->write(homeostaticUnderEffects[drivesList.get(activeDrive.idx).asString()].rpc_command,rply);
            yarp::os::Time::delay(0.1);
            yInfo() << "[updateAllostatic] reply from homeostatis : " << rply.toString();

            //clear as soon as sent
            homeostaticUnderEffects[drivesList.get(activeDrive.idx).asString()].rpc_command.clear();
            yInfo() << "check rpc command is empty : " << homeostaticUnderEffects[drivesList.get(activeDrive.idx).asString()].rpc_command.toString();
        }
        

        //d->second.value += (d->second.homeoStasisMax - d->second.homeoStasisMin) / 3.0;
    }

    if (activeDrive.level == OVER)
    {
        cout << "Drive " << activeDrive.idx << " chosen. Over level." << endl;
//        iCub->look("partner");
        iCub->say(homeostaticOverEffects[drivesList.get(activeDrive.idx).asString()].getRandomSentence());
        Bottle cmd;
        cmd.clear();
        cmd.addString("par");
        cmd.addString(drivesList.get(activeDrive.idx).asString());
        cmd.addString("val");
        cmd.addDouble(0.5);

        to_homeo_rpc.write(cmd);
        //d->second.value -= (d->second.homeoStasisMax - d->second.homeoStasisMin) / 3.0;;
    }/*else
    {
        cout << "Goooood things"<< endl ;
        if ((yarp::os::Time::now()-last_time)>5)
            {
            last_time = yarp::os::Time::now();
            Bottle cmd;
            cmd.clear();
            cmd.addString("listen");
            cmd.addString("on");
            ears_port.write(cmd);
        }
    }*/
    
    //cout<<"come on..."<<endl;
    //iCub->commitAgent();
    //cout<<"commited"<<endl;
    return true;
}

void ReactiveLayer::configureOPC(yarp::os::ResourceFinder &rf)
{
    //Populate the OPC if required
    cout<<"Populating OPC";
    Bottle grpOPC = rf.findGroup("OPC");
    bool shouldPopulate = grpOPC.find("populateOPC").asInt() == 1;
    if (shouldPopulate)
    {
        iCub->opc->checkout();
        Bottle *agentList = grpOPC.find("agent").asList();
        if (agentList)
        {
            for(int d=0; d<agentList->size(); d++)
            {
                string name = agentList->get(d).asString();
                Agent* agent = iCub->opc->addOrRetrieveEntity<Agent>(name);
                agent->m_present = false;
                iCub->opc->commit(agent);
            }
        }

        Bottle *objectList = grpOPC.find("object").asList();
        if (objectList)
        {
            for(int d=0; d<objectList->size(); d++)
            {
                string name = objectList->get(d).asString();
                Object* o = iCub->opc->addOrRetrieveEntity<Object>(name);
                o->m_present = false;
                iCub->opc->commit(o);
            }
        }

        Bottle *rtobjectList = grpOPC.find("rtobject").asList();
        if (rtobjectList)
        {
            for(int d=0; d<rtobjectList->size(); d++)
            {
                string name = rtobjectList->get(d).asString();
                RTObject* o = iCub->opc->addOrRetrieveEntity<RTObject>(name);
                o->m_present = false;
                iCub->opc->commit(o);
            }
        }

        Bottle *adjectiveList = grpOPC.find("adjective").asList();
        if (adjectiveList)
        {
            for(int d=0; d<adjectiveList->size(); d++)
            {
                string name = adjectiveList->get(d).asString();
                Adjective* a = iCub->opc->addOrRetrieveEntity<Adjective>(name);
                iCub->opc->commit(a);
            }
        }

        Bottle *actionList = grpOPC.find("action").asList();
        if (actionList)
        {
            for(int d=0; d<actionList->size(); d++)
            {
                string name = actionList->get(d).asString();
                Action* a = iCub->opc->addOrRetrieveEntity<Action>(name);
                iCub->opc->commit(a);
            }
        }
    }
    cout<<"done"<<endl;
}


bool ReactiveLayer::respond(const Bottle& cmd, Bottle& reply)
{
    if (cmd.get(0).asString() == "help" )
    {   string help = "\n";
        help += " ['need'] ['macro'] [name] [arguments]   : Assigns a value to a specific parameter \n";
        reply.addString(help);
        /*cout << " [par] [drive] [val/min/max/dec] [value]   : Assigns a value to a specific parameter \n"<<
                " [delta] [drive] [val/min/max/dec] [value] : Adds a value to a specific parameter  \n"<<
                " [add] [conf] [drive Bottle]               : Adds a drive to the manager as a drive directly read from conf-file  \n"<<
                " [add] [botl] [drive Bottle]                : Adds a drive to the manager as a Bottle of values of shape \n"<<
                "                                           : (string name, double value, double homeo_min, double homeo_max, double decay = 0.05, bool gradient = true) \n"<<endl;
    */
    }else if (cmd.get(0).asString() == "need")
    {
        if (cmd.get(1).asString() == "macro") //macro is for subgoals or general actions
        {
            if (cmd.get(2).asString() == "search")
            {
                cout << yarp::os::Time::now()<<endl;
                cout<< "haha! time to work!"<<endl;
                string o_name = cmd.get(3).asString();
                if (o_name != "none"){
                double p = pow(1/cmd.get(4).asDouble() ,5 ) * 100;
                string d_name = "search";
                d_name += "_";
                d_name += o_name;
                //createTemporalDrive(d_name, p);
                cout << "searchList size:"<<endl;

                searchList.push_back(o_name);
                cout << searchList.size() << endl;
                cout << searchList[0]<<endl;
                }
                reply.addString("ack");

            }
        }else if (cmd.get(1).asString() == "primitive")
        {
            if (cmd.get(2).asString() == "point")
            {
                cout << yarp::os::Time::now()<<endl;
                string o_name = cmd.get(3).asString();
                if (o_name != "none"){
                double p = pow(1/cmd.get(4).asDouble() ,5 ) * 100;
                string d_name = "point";
                d_name += "_";
                d_name += o_name;
                //createTemporalDrive(d_name, p);
                //searchList.push_back(o_name);
                //createTemporalDrive(o_name, p);
                pointList.push_back(o_name);
            }
                reply.addString("ack");

            }
        }
    }
    reply.addString("nack");

    return true;
}
