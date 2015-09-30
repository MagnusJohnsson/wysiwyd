#include <string>
#include <iostream>
#include <iomanip>
#include <yarp/os/all.h>
#include <yarp/sig/all.h>
#include <yarp/math/SVD.h>
#include <yarp/math/Rand.h>
#include "wrdac/clients/icubClient.h"
#include <map>
#include "internalVariablesDecay.h"

using namespace std;
using namespace yarp::os;
using namespace yarp::sig;
using namespace yarp::math;
using namespace wysiwyd::wrdac;

enum OutCZ {UNDER, OVER};

struct DriveOutCZ {
    int idx;
    OutCZ level;

};

struct StimulusEmotionalResponse
{
    bool active;
    Port *output_port;
    Bottle rpc_command;
    vector<string> m_sentences;
    vector<string> m_choregraphies;
    map<string, double> m_emotionalEffect;
    string getRandomSentence() { if (m_sentences.size()==0) return ""; return m_sentences[yarp::os::Random::uniform(0,m_sentences.size()-1)];}
    string getRandomChoregraphy() { if(m_choregraphies.size()==0) return "default"; return m_choregraphies[yarp::os::Random::uniform(0,m_choregraphies.size()-1)];}
};

class ReactiveLayer: public RFModule
{
private:
    ICubClient *iCub;

    Bottle drivesList;
    
    string currentPartner;
    Port to_homeo_rpc;
    Port ears_port;
    string moduleName;
    string homeo_name;
    int n_drives;
    Bottle drive_names;

    double period;
    double salutationLifetime, lastResponseTime, last_time;
    double faceUpdatePeriod, lastFaceUpdate;

	InternalVariablesDecay* decayThread;

	//Drive triggers
	bool physicalInteraction;
	bool someonePresent;
    bool confusion;
    bool learning;
    bool finding;
    bool pointing;
	//Reflexes
	map<string, StimulusEmotionalResponse> salutationEffects;
	map<string, StimulusEmotionalResponse> tactileEffects;
	map<string, StimulusEmotionalResponse> homeostaticOverEffects;
	map<string, StimulusEmotionalResponse> homeostaticUnderEffects;


    vector<double> drivePriorities;
    vector<string> temporalDrivesList;
    vector<string> searchList;
    vector<string> pointList;
    double priority_sum;

    Port rpc;

    vector< yarp::os::Port* > rpc_ports;
    vector< yarp::os::BufferedPort<Bottle>* > outputM_ports;
    vector< yarp::os::BufferedPort<Bottle>* > outputm_ports;

    //Configuration
	void configureOPC(yarp::os::ResourceFinder &rf);
	void configureAllostatic(yarp::os::ResourceFinder &rf);
	void configureTactile(yarp::os::ResourceFinder &rf);
	void configureSalutation(yarp::os::ResourceFinder &rf);

    int openPorts(string driveName,int d);
public:
    bool configure(yarp::os::ResourceFinder &rf);

    bool interruptModule()
    {
        return true;
    }

    bool close();

    double getPeriod()
    {
        return period;
    }

    bool updateModule();

    //Check for unknown tags in the opc
    bool handleTagging();

    //Check for objects to point to
    bool handlePointing();

	//Check for newcomers and salute them if required
	bool handleSalutation(bool& someoneIsPresent);

    //Retrieve and treat the tactile information input
    bool handleTactile();

    //Retrieve and treat the gesture information input
    bool handleGesture();

    //Handle a search command: look for object in opc or ask for it
    bool handleSearch();
    bool handlePoint();

	//Update the drives accordingly to the stimuli
	bool updateAllostatic();

	//Express emotions
	bool updateEmotions();

	//RPC & scenarios
	bool respond(const Bottle& cmd, Bottle& reply);

    bool Normalize(vector<double>& vec);

    bool createTemporalDrive(string name, double prior);

    // Choose a drive out of CZ, according to drive priorities
    DriveOutCZ chooseDrive();
};
