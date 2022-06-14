/*
* Copyright (c) Huawei Technologies Co., Ltd. 2021-2022. All rights reserved.
* Description: Publishing controller information over ROS to gazebo for vis.
* Author: Zhu Yijie
* Create: 2022-1-05
* Notes: None.
* Modify: init the file. @ Zhu Yijie
*/

#include "ros/control2gazebo_msg.h"

namespace Quadruped {

    Controller2GazeboMsg::Controller2GazeboMsg(Robot *robotIn,
                                                   LocomotionController *locomotionControllerIn,
                                                   ros::NodeHandle &nhIn)
        : robot(robotIn), nh(nhIn)
    {
        locomotionController = nullptr;
        posePlanner = locomotionControllerIn->GetPosePlanner();
        // locomotionControllerIn->GetRobotEstimator();
        // posePublish = nh.advertise<geometry_msgs::Pose>("/visual/pose_planner/xyz_line", 20);
        gazeboStatePublish = nh.advertise<xpp_msgs::RobotStateCartesian>(xpp_msgs::robot_state_current, 10);
        gazeboParamPublish = nh.advertise<xpp_msgs::RobotParameters>(xpp_msgs::robot_parameters, 10);
        posePlannerPublish = nh.advertise<geometry_msgs::Pose>(xpp_msgs::robot_state_desired,10);
        baseStateClient = nh.serviceClient<gazebo_msgs::GetLinkState>("/gazebo/get_link_state");
    
        lastTime = ros::Time::now();
        ROS_INFO("Controller2GazeboMsg init success...");
    }

    void Controller2GazeboMsg::PublishGazeboStateCallback()
    {
        // auto cart = Convert::ToXpp(cart_msg);

        // // transform feet from world -> base frame
        // Eigen::Matrix3d B_R_W = cart.base_.ang.q.normalized().toRotationMatrix().inverse();
        // EndeffectorsPos ee_B(cart.ee_motion_.GetEECount());
        // for (auto ee : ee_B.GetEEsOrdered())
        //     ee_B.at(ee) = B_R_W * (cart.ee_motion_.at(ee).p_ - cart.base_.lin.p_);

        // Eigen::VectorXd q =  inverse_kinematics_->GetAllJointAngles(ee_B).ToVec();

        // xpp_msgs::RobotStateJoint joint_msg;
        // joint_msg.base            = cart_msg.base;
        // joint_msg.ee_contact      = cart_msg.ee_contact;
        // joint_msg.time_from_start = cart_msg.time_from_start;
        // joint_msg.joint_state.position = std::vector<double>(q.data(), q.data()+q.size());
        // // Attention: Not filling joint velocities or torques

        // base and foot follow half a sine motion up and down
        // Vec6<float> pose = posePlanner->GetBasePose();
        // const auto & q = posePlanner->quat;
        auto curTime = ros::Time::now();
                
        const auto & q = robot->GetBaseOrientation();
        const auto & pose = robot->GetBasePosition();
        const auto & jointAngles = robot->GetMotorAngles();
        const auto& forces = robot->GetFootForce();
        const auto& contacts = robot->GetFootContacts();
        const auto & footPos = robot->GetFootPositionsInWorldFrame();
        
        xpp::RobotStateCartesian robotstate(4);
        
        robotstate.t_global_ = robot->GetTimeSinceReset();
        if (true) {
            robotstate.base_.lin.p_.x() = pose[0];
            robotstate.base_.lin.p_.y() = pose[1];
            robotstate.base_.lin.p_.z() = pose[2];
            robotstate.base_.ang.q = Eigen::Quaterniond(q[0],q[1],q[2],q[3]);
        } else {
            gazebo_msgs::GetLinkState gls_request;
            if (baseStateClient.exists()) { 
                gls_request.request.link_name = std::string("a1_gazebo::base");
                gls_request.request.reference_frame=std::string("world"); 
                // ros::service::waitForService("/gazebo/get_link_state", -1);
                baseStateClient.call(gls_request);
                if (!gls_request.response.success) {
                     ROS_INFO("Get Gazebo link state not success!\n");      
                }
            } else { 
                ROS_INFO("Get Gazebo link state goes wrong!\n"); 
            }
            
            const auto & pose_ = gls_request.response.link_state.pose; 
            std::cout<< "pose = " << pose_ <<std::endl;
            robotstate.base_.lin.p_.x() = pose_.position.x;
            robotstate.base_.lin.p_.y() = pose_.position.y;
            robotstate.base_.lin.p_.z() = pose_.position.z;
            robotstate.base_.ang.q = Eigen::Quaterniond(pose_.orientation.w,
                                                        pose_.orientation.x,
                                                        pose_.orientation.y,
                                                        pose_.orientation.z);
        }
        auto& joint_states = robotstate.joint_states;
        
        for (int legId =0; legId<4; ++legId) {
            robotstate.ee_motion_.at(legId).p_.x() = footPos(0, legId);  // foot pose in world frame
            robotstate.ee_motion_.at(legId).p_.y() = footPos(1, legId);
            robotstate.ee_motion_.at(legId).p_.z() = footPos(2, legId);
            robotstate.ee_forces_.at(legId).z() = forces[legId]; // N
            robotstate.ee_contact_.at(legId) = contacts[legId];
            for(int j=0;j<3;++j) {
                joint_states.at(3*legId + j) = double(jointAngles[3*legId + j]);
            }
        }

        xpp_msgs::RobotParameters workspace;
        workspace.base_mass = 10.0;
        workspace.ee_names  = {"RF"}; // swing foot names
        geometry_msgs::Point p;
        p.x = 0;
        p.y = 0;
        p.z = -1;
        workspace.nominal_ee_pos = {p};

        if ((rIB - posePlanner->rIB).norm()>1e-3) {        
            // ROS_INFO("SEND POSE PLANNER'S POSE");
            rIB = posePlanner->rIB;
            quat = posePlanner->quat;
            geometry_msgs::Pose poseMsg;
            poseMsg.position.x = rIB[0];
            poseMsg.position.y = rIB[1];
            poseMsg.position.z = rIB[2];
            poseMsg.orientation.w = quat[0];
            poseMsg.orientation.x = quat[1];
            poseMsg.orientation.y = quat[2];
            poseMsg.orientation.z = quat[3];
        
            posePlannerPublish.publish(poseMsg);
        }
        gazeboParamPublish.publish(workspace);
        gazeboStatePublish.publish(xpp::Convert::ToRos(robotstate));
        
        transform.setOrigin( tf::Vector3(rIB[0], rIB[1],rIB[2]));
        transform.setRotation( tf::Quaternion(quat[0], quat[1], quat[2], quat[3]) );
        desiredPoseFramebr.sendTransform(tf::StampedTransform(transform, ros::Time::now(), "world", "desiredPoseFrame"));

    }
}