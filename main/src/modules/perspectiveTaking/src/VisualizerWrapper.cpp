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

#include <vtkRenderWindow.h>

#include <rtabmap/core/util3d.h>

#include "VisualizerWrapper.h"
#include "PerspectiveTaking.h"

using namespace rtabmap;

VisualizerWrapper::VisualizerWrapper(QWidget *parent, Eigen::Matrix4f cloudTransform) :
    QVTKWidget(parent),
    _maxTrajectorySize(100),
    _trajectory(new pcl::PointCloud<pcl::PointXYZ>),
    _visualizer(new pcl::visualization::PCLVisualizer("PCLVisualizer", false)),
    _viewportTransform(cloudTransform) {

    this->setMinimumSize(200, 200);
    this->SetRenderWindow(_visualizer->getRenderWindow());
    _visualizer->setupInteractor(this->GetInteractor(), this->GetRenderWindow());

    _visualizer->addText ("View", 10, 10, "view text");

    _visualizer->setBackgroundColor(1.0, 1.0, 1.0);

    _visualizer->setCameraPosition(
        -1, 0, 0,
        0, 0, 0,
        0, 0, 1);
}

VisualizerWrapper::~VisualizerWrapper() {
    this->removeAllClouds();
    delete _visualizer;
}

Transform VisualizerWrapper::transformFromCloud(pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud) {
    Eigen::Vector4f origin = cloud->sensor_origin_;
    Eigen::Translation<float,3> translation_A(Eigen::Vector3f(origin[0], origin[1], origin[2]));
    Eigen::Affine3f m = translation_A * cloud->sensor_orientation_;
    return Transform::fromEigen3f(m);
}

bool VisualizerWrapper::getPose(const std::string & id, Transform & pose) {
    if(_addedClouds.count(id)) {
        pose = _addedClouds.at(id)->pose;
        return true;
    } else {
        return false;
    }
}

bool VisualizerWrapper::updateCloudPose(
        const std::string & id,
        const Transform & pose) {
    if(_addedClouds.count(id)) {
        //yDebug() << "Updating pose " << id << " to " << pose.prettyPrint();
        if(_addedClouds.at(id)->pose == pose ||
                _visualizer->updatePointCloudPose(id, pose.toEigen3f())) {
            _addedClouds.at(id)->pose = pose;
            return true;
        }
    }
    return false;
}

void VisualizerWrapper::setCloudVisibility(const std::string & id, bool isVisible) {
    pcl::visualization::CloudActorMapPtr cloudActorMap = _visualizer->getCloudActorMap();
    pcl::visualization::CloudActorMap::iterator iter = cloudActorMap->find(id);
    if(iter != cloudActorMap->end()) {
        iter->second.actor->SetVisibility(isVisible);
    } else {
        yWarning() << "Cannot find cloud actor named \"" << id << "\".";
    }
}

bool VisualizerWrapper::addOrUpdateCloud(
        const std::string & id,
        const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud,
        const Transform & pose) {
    if(updateCloud(id, cloud, pose)) {
        return true;
    } else {
        return addCloud(id, cloud, pose);
    }
}

bool VisualizerWrapper::addCloud(
        const std::string & id,
        const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud,
        const Transform & pose) {
    cloud->sensor_orientation_ = Eigen::Quaternionf(pose.toEigen3f().rotation());
    cloud->sensor_origin_ = Eigen::Vector4f(pose.x(), pose.y(), pose.z(), 0.0f);

    pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_transformed (new pcl::PointCloud<pcl::PointXYZRGB> ());
    pcl::transformPointCloud (*cloud, *cloud_transformed, _viewportTransform);

    pcl::visualization::PointCloudColorHandlerRGBField<pcl::PointXYZRGB> rgb(cloud_transformed);
    if(_visualizer->addPointCloud(cloud_transformed, rgb, id)) {
        _visualizer->setPointCloudRenderingProperties (pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 4, id);
        boost::shared_ptr<cloudWithPose> p1( new cloudWithPose(cloud, pose) );
        _addedClouds[id]=p1;
        return true;
    } else {
        return false;
    }
}

bool VisualizerWrapper::updateCloud(
        const std::string & id,
        const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud,
        const Transform & pose) {
    if(_addedClouds.count(id)) {
        //yDebug << "Updating " << id << " with " << cloud->size() << " points";
        if(!removeCloud(id)) {
            yError() << "Point cloud " << id << " could not be removed!";
        }
        return addCloud(id, cloud, pose);
    } else {
        return false;
    }
}

bool VisualizerWrapper::removeCloud(const std::string & id) {
    bool success = _visualizer->removePointCloud(id);
    _addedClouds.erase(id); // remove after visualizer
    return success;
}

void VisualizerWrapper::updateCameraPosition(const Transform & pose)
{
    if(!pose.isNull()) {
        Eigen::Affine3f m = pose.toEigen3f();
        Eigen::Vector3f pos = m.translation();
        Eigen::Vector3f lastPos(0,0,0);
        /*if(_trajectory->size())
        {
            lastPos[0]=_trajectory->back().x;
            lastPos[1]=_trajectory->back().y;
            lastPos[2]=_trajectory->back().z;
        }
        _trajectory->push_back(pcl::PointXYZ(pos[0], pos[1], pos[2]));
        if(_maxTrajectorySize>0) {
            while(_trajectory->size() > _maxTrajectorySize) {
                _trajectory->erase(_trajectory->begin());
            }
        }
        if(_aShowTrajectory->isChecked())
        {
            _visualizer->removeShape("trajectory");
            pcl::PolygonMesh mesh;
            pcl::Vertices vertices;
            vertices.vertices.resize(_trajectory->size());
            for(unsigned int i=0; i<vertices.vertices.size(); ++i)
            {
                vertices.vertices[i] = i;
            }
            pcl::toPCLPointCloud2(*_trajectory, mesh.cloud);
            mesh.polygons.push_back(vertices);
            _visualizer->addPolylineFromPolygonMesh(mesh, "trajectory");
        }*/

        if(pose != _lastPose || _lastPose.isNull()) {
            if(_lastPose.isNull()) {
                _lastPose.setIdentity();
            }

            std::vector<pcl::visualization::Camera> cameras;
            _visualizer->getCameras(cameras);

            /*if(_aLockCamera->isChecked()) {
                //update camera position
                Eigen::Vector3f diff = pos - Eigen::Vector3f(_lastPose.x(), _lastPose.y(), _lastPose.z());
                cameras.front().pos[0] += diff[0];
                cameras.front().pos[1] += diff[1];
                cameras.front().pos[2] += diff[2];
                cameras.front().focal[0] += diff[0];
                cameras.front().focal[1] += diff[1];
                cameras.front().focal[2] += diff[2];
            }*/
            //else if(_aFollowCamera->isChecked()) {
            Eigen::Vector3f vPosToFocal = Eigen::Vector3f(cameras.front().focal[0] - cameras.front().pos[0],
                    cameras.front().focal[1] - cameras.front().pos[1],
                    cameras.front().focal[2] - cameras.front().pos[2]).normalized();
            Eigen::Vector3f zAxis(cameras.front().view[0], cameras.front().view[1], cameras.front().view[2]);
            Eigen::Vector3f yAxis = zAxis.cross(vPosToFocal);
            Eigen::Vector3f xAxis = yAxis.cross(zAxis);
            Transform PR(xAxis[0], xAxis[1], xAxis[2],0,
                    yAxis[0], yAxis[1], yAxis[2],0,
                    zAxis[0], zAxis[1], zAxis[2],0);

            Transform P(PR[0], PR[1], PR[2], cameras.front().pos[0],
                    PR[4], PR[5], PR[6], cameras.front().pos[1],
                    PR[8], PR[9], PR[10], cameras.front().pos[2]);
            Transform F(PR[0], PR[1], PR[2], cameras.front().focal[0],
                    PR[4], PR[5], PR[6], cameras.front().focal[1],
                    PR[8], PR[9], PR[10], cameras.front().focal[2]);
            Transform N = pose;
            Transform O = _lastPose;
            Transform O2N = O.inverse()*N;
            Transform F2O = F.inverse()*O;
            Transform T = F2O * O2N * F2O.inverse();
            Transform Fp = F * T;
            Transform P2F = P.inverse()*F;
            Transform Pp = P * P2F * T * P2F.inverse();

            //_visualizer->removeCoordinateSystem("reference", 0);
            //_visualizer->addCoordinateSystem(0.2, m, "reference", 0);

            _visualizer->setCameraPosition(
                Pp.x(), Pp.y(), Pp.z(),
                Fp.x(), Fp.y(), Fp.z(),
                Fp[8], Fp[9], Fp[10]);
        }
    }

    _lastPose = pose;
}
