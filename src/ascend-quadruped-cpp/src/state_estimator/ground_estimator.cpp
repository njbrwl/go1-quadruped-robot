/*
* Copyright (c) Huawei Technologies Co., Ltd. 2021-2022. All rights reserved.
* Description: estimate the ground of quadruped.
* Author: Zhu Yijie
* Create: 2021-11-25
* Notes: xx
* Modify: init the file. @ Zhu Yijie
*/
#include "state_estimator/ground_estimator.h"

namespace Quadruped {
  
    GroundSurfaceEstimator::GroundSurfaceEstimator(Robot *robotIn, std::string terrainConfigPath, unsigned int windowSize)
        : robot(robotIn)
    {
        // Loadterrain(terrainConfigPath);
        footStepperConfig = YAML::LoadFile(terrainConfigPath);
        Reset(0.f);
    }

    // void GroundSurfaceEstimator::Loadterrain(std::string& terrainConfigPath)
    // {
    //     // YAML::Node footStepperConfig = YAML::LoadFile(terrainConfigPath);
    //     terrain.terrainType = static_cast<TerrainType>(footStepperConfig["terrain_type"].as<int>());
    //     terrain.footHoldOffset = footStepperConfig["foothold_offset"].as<float>();
    //     robot->footHoldOffset = terrain.footHoldOffset;
    //     switch (terrain.terrainType) {
    //         case TerrainType::PLANE: {
    //             // terrain.costMap = terrain.block<N, N>(0, 0);
    //         } break;
    //         case TerrainType::PLUM_PILES: {
    //             float gapWidth = footStepperConfig["gap_width"].as<float>();
    //             std::vector<float> distanceOfGaps = footStepperConfig["gaps"].as<std::vector<float>>();
    //             for (float distance : distanceOfGaps) {
    //                 Gap* gap = new Gap(distance, gapWidth, {0, 0, 0});
    //                 terrain.gaps.push_back(gap);
    //             }
    //         } break;
    //         case TerrainType::STAIRS: {
    //             // todo
    //         } break;
    //         default : throw std::domain_error("no such terrain!");
    //     }
    // }

    void GroundSurfaceEstimator::Update(float currentTime)
    {   
        Eigen::Matrix<bool, 4, 1> contactState = robot->GetFootContacts();
        bool shouldUpdate = false;
        int N=0;
        int i=0;
        for(i=0; i < 4; ++i) {
            if (contactState[i]) {
                if ((!lastContactState[i])) {
                    shouldUpdate = true;
                }
                ++N;
            }
        }
        lastContactState = contactState;
        if (N <= 3 || !shouldUpdate) {
            return ;
        }
        // printf(" should Update ground!\n");
        Eigen::Matrix<double, 3, 4> footPositionsInBaseFrame = robot->GetFootPositionsInBaseFrame().cast<double>();
        // std::cout << "footPositionsInBaseFrame" << footPositionsInBaseFrame <<std::endl;
        pZ = footPositionsInBaseFrame.row(2);
        W.col(1) = footPositionsInBaseFrame.row(0);
        W.col(2) = footPositionsInBaseFrame.row(1);
        
        Mat3<double> ww = W.transpose()* W;
        a = ww.inverse()* W.transpose()*pZ;
        // std::cout << "a=" << a.transpose() << std::endl;
        GetNormalVector(true);
        ComputeControlFrame();
    }

    void GroundSurfaceEstimator::Reset(float currentTime) {
        terrain.terrainType = static_cast<TerrainType>(footStepperConfig["terrain_type"].as<int>());
        if (robot->controlParams["mode"] == LocomotionMode::POSITION_LOCOMOTION) {
            terrain.terrainType = TerrainType::PLUM_PILES;
        } else if (robot->controlParams["mode"] == LocomotionMode::VELOCITY_LOCOMOTION) {
            terrain.terrainType = TerrainType::PLANE;
        }

        terrain.footHoldOffset = footStepperConfig["foothold_offset"].as<float>();
        robot->footHoldOffset = terrain.footHoldOffset;
        switch (terrain.terrainType) {
            case TerrainType::PLANE: {
                // terrain.costMap = terrain.block<N, N>(0, 0);
            } break;
            case TerrainType::PLUM_PILES: {
                float gapWidth = footStepperConfig["gap_width"].as<float>();
                std::vector<float> distanceOfGaps = footStepperConfig["gaps"].as<std::vector<float>>();
                for (float distance : distanceOfGaps) {
                    Gap* gap = new Gap(distance, gapWidth, {0, 0, 0});
                    terrain.gaps.push_back(gap);
                }
            } break;
            case TerrainType::STAIRS: {
                // todo
            } break;
            default : throw std::domain_error("no such terrain!");
        }

        a = Eigen::Matrix<double, 3, 1>::Zero();
        W = Eigen::Matrix<double,4,3>::Ones();
        pZ = Vec4<double>::Zero();
        n << 0.f, 0.f, 1.f;
        controlFrameRPY << 0., 0., 0.;
        controlFrameOrientation << 1.0, 0, 0, 0;
        controlFrame << 1, 0, 0 ,0,
                        0, 1, 0, 0,
                        0, 0, 1, 0,
                        0, 0, 0, 1;
        lastContactState << 0, 0, 0, 0;
    }

    float GroundSurfaceEstimator::GetZ(float x, float y)
    {
        float z = a.transpose() * Vec3<double>(1, x, y);
        return z;
    }
  
    Eigen::Matrix<double, 3, 1> GroundSurfaceEstimator::GetNormalVector(bool update)
    {
        if (update){
            double factor = std::sqrt(a[1]*a[1] + a[2]*a[2] + 1);
            n << -a[1], -a[2], 1.0;
            n /= factor;
        }

        // std::cout << "n=" << n.transpose() << std::endl;
        return n; // in base frame
    }

    Eigen::Matrix<double, 4, 4> GroundSurfaceEstimator::ComputeControlFrame()
    {
        // Vec3<float> comRPY = robot->GetBaseRollPitchYaw();
        Quat<double> quat = robot->GetBaseOrientation().cast<double>();
        Vec3<double> nInWorldFrame = robotics::math::invertRigidTransform<double>({0,0,0},quat, n);
        Vec3<double> xAxis = robotics::math::quaternionToRotationMatrix(quat).transpose().col(0);
        Vec3<double> yAxis = nInWorldFrame.cross(xAxis);
        yAxis.normalize();
        xAxis = yAxis.cross(nInWorldFrame);
        xAxis.normalize();
        nInWorldFrame.normalize();
        Mat3<double> R;
        R.col(0) = xAxis;
        R.col(1) = yAxis;
        R.col(2) = nInWorldFrame;
        double ratio = 0.7; // todo, 0.7 for walk mode
        controlFrameRPY = (1-ratio) * controlFrameRPY + ratio * robotics::math::rotationMatrixToRPY(R.transpose());
        // std::cout << "[UPDATE] controlFrameRPY " << controlFrameRPY.transpose() << std::endl;
        // filtering the controlframe rpy angles.
        
        // float comYaw = comRPY[2];
        // float cy = cos(comYaw);
        // float sy = sin(comYaw);
        // float pitch = atan(n[0]/(n[1]+1e-7)*cy+n[1]/n[2]*sy);
        // float roll = atan((sy*sin(pitch)-n[1]/n[2]*cos(pitch))/cy);
    
        // // float pitch = atan(n[0]/n[2]);
        // // float roll = atan(-n[1]/n[2]);
        
        // controlFrameRPY << roll, pitch, comYaw;
        controlFrameOrientation = robotics::math::rpyToQuat(controlFrameRPY);
        // Mat3<float> R = robotics::math::rpyToRotMat(controlFrameRPY).transpose();
        controlFrame.block<3,3>(0,0) = R;
        controlFrame.block<3,1>(0,3) = robot->GetBasePosition().cast<double>();
        return controlFrame;
    }

    Eigen::Matrix<float, 3, 3> GroundSurfaceEstimator::GetAlignedDirections()
    {
        Mat3<float> R = controlFrame.block<3,3>(0,0).cast<float>();   
        return R;
    }
} // namespace Quadruped