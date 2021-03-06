# Copyright: (C) 2014 WYSIWYD Consortium
# Authors: Ugo Pattacini
# CopyPolicy: Released under the terms of the GNU GPL v2.0.

#Add here modules without specific dependency
add_subdirectory(abm)
add_subdirectory(adaptiveLayer)
add_subdirectory(attentionRelated)
add_subdirectory(behaviorRelated)
add_subdirectory(interactionEngine)
add_subdirectory(learnPrimitive)
add_subdirectory(navigationRelated)
add_subdirectory(opcRelated)
add_subdirectory(ppsAllostatic)
#add_subdirectory(reactiveLayer)
add_subdirectory(reservoir)
add_subdirectory(scenarioSpecific)
add_subdirectory(systemAuditory)
add_subdirectory(systemHaptic)
add_subdirectory(systemMotor)
add_subdirectory(tools)
add_subdirectory(samRelated)

#Add modules with dependencies here
if(WYSIWYD_USE_OpenCV)
    add_subdirectory(systemVisual)
    add_subdirectory(visionRelated)
endif()

if(WYSIWYD_USE_OpenCV)
    add_subdirectory(bodySchema)
endif()

if(WYSIWYD_USE_GTK2)
    add_subdirectory(cvz)
endif()

if(WYSIWYD_USE_kinectWrapper)
    add_subdirectory(humanUnderstanding)
endif()

if(WYSIWYD_USE_RTABMap AND WYSIWYD_USE_OpenCV AND WYSIWYD_USE_PCL AND WYSIWYD_USE_Qt5 AND WYSIWYD_USE_kinectWrapper)
    add_subdirectory(perspectiveTaking)
endif()

if(WYSIWYD_USE_TUIO)
    add_subdirectory(reactableRelated)
endif()
