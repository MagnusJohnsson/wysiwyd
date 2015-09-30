#!/usr/bin/python

# Simple module for reading and ploting a port's info


import sys
sys.path.append("/home/jy/robotology/yarp/bindings/build")
import yarp
import io


yarp.Network.init() #Initialize network. This is mandatory

input_port = yarp.BufferedPortBottle() # Create a buff.Port
input_port.open("/logger:i") # Open port (name)
print("connecting")
yarp.Network.connect("/icubSim/head/state:o", "/logger:i") #Connect the input port to this reader port

input_port2 = yarp.BufferedPortBottle() # Create a buff.Port
input_port2.open("/logger2:i") # Open port (name)
print("connecting")
yarp.Network.connect("/icubSim/head/state:o", "/logger2:i") #Connect the input port to this reader port

print("conected!!")

file = io.open('iCubhead.txt', 'w')
    
for _ in range(100):
	string = ""
	pos = input_port.read(False)
	pos2 = input_port2.read(False)
	if pos is not None:
		string += pos.toString()
		file.write(unicode(string + "\n"))
	yarp.Time.delay(0.1)

	

