/* 
* Copyright (c) Huawei Technologies Co., Ltd. 2021-2022. All rights reserved.        
* Description: compute the force for stance controller. 
* Author: Zang Yaohua
* Create: 2021-10-25
* Notes: xx
* Modify: init the file. @ Zang Yaohua
*/

#include "mpc_controller/qp_torque_optimizer.h"
#include "QuadProg++.hh"
#include "Array.hh"
namespace Quadruped {
    /** @brief
     * @param robotMass : float, ture mass of robot.
     * @param robotInertia : Eigen::Matrix<float, 3, 3>, should expressed in control frame.
     * @param footPositions : Eigen::Matrix<float, 4, 3>, should expressed in control frame.
     * @return massMat : Eigen::Matrix<float, 6, 12>, in control frame.
     */
    Eigen::Matrix<float, 6, 12> ComputeMassMatrix(float robotMass,
                                                  Eigen::Matrix<float, 3, 3> robotInertia,
                                                  Eigen::Matrix<float, 4, 3> footPositions)
    {
        Eigen::Matrix<float, 3, 3> I = Eigen::Matrix<float, 3, 3>::Identity(3, 3);
        Eigen::Matrix<float, 3, 3> invMass;
        Eigen::Matrix<float, 3, 3> invInertia; // in control frame
        Eigen::Matrix<float, 6, 12> massMat = Eigen::Matrix<float, 6, 12>::Zero();
        Eigen::Matrix<float, 1, 3> x;
        Eigen::Matrix<float, 3, 3> footPositionSkew;

        invMass = I / robotMass;
        invInertia = robotInertia.inverse();

        for (int legId = 0; legId < 4; ++legId) {
            massMat.block<3, 3>(0, legId * 3) = invMass;
            x = footPositions.row(legId);
            footPositionSkew << 0., -x[2], x[1],
                                x[2], 0., -x[0],
                                -x[1], x[0], 0.;
            massMat.block<3, 3>(3, legId * 3) = invInertia * footPositionSkew;
        }
        return massMat;
    }

    std::tuple<Eigen::Matrix<float, 12, 24>, Eigen::Matrix<float, 24, 1>> ComputeConstraintMatrix(
        float mpcBodyMass,
        Eigen::Matrix<bool, 4, 1> contacts,
        float frictionCoef,
        float fMinRatio,
        float fMaxRatio)
    {
        float fMin;
        float fMax;
        fMin = fMinRatio * mpcBodyMass * 9.8;
        fMax = fMaxRatio * mpcBodyMass * 9.8;
        Eigen::Matrix<float, 24, 12> A = Eigen::Matrix<float, 24, 12>::Zero();
        Eigen::Matrix<float, 24, 1> lb = Eigen::Matrix<float, 24, 1>::Zero();

        // constrains on normal component of friction
        for (int legId = 0; legId < 4; legId++) {
            A(legId * 2, legId * 3 + 2) = 1;
            A(legId * 2 + 1, legId * 3 + 2) = -1;
            if (contacts(legId)) {
                lb(legId * 2, 0) = fMin;
                lb(legId * 2 + 1, 0) = -fMax;
            } else {
                lb(legId * 2, 0) = 1e-7;
                lb(legId * 2 + 1, 0) = 1e-7;
            }
        }
        // Friction cone constraints
        int rowId;
        int colId;
        for (int legId = 0; legId < 4; ++legId) {
            rowId = 8 + legId * 4;
            colId = legId * 3;
            lb.block<4, 1>(rowId, 0) << 0., 0., 0., 0.;
            A.block<1, 3>(rowId,     colId) <<  1,  0, frictionCoef;
            A.block<1, 3>(rowId + 1, colId) << -1,  0, frictionCoef;
            A.block<1, 3>(rowId + 2, colId) <<  0,  1, frictionCoef;
            A.block<1, 3>(rowId + 3, colId) <<  0, -1, frictionCoef;
        }
        std::tuple<Eigen::Matrix<float, 12, 24>, Eigen::Matrix<float, 24, 1>> Alb(A.transpose(), lb);
        return Alb;
    }

    std::tuple<Eigen::Matrix<float, 12, 12>, Eigen::Matrix<float, 12, 1>> ComputeObjectiveMatrix(
        Eigen::Matrix<float, 6, 12> massMatrix,
        Eigen::Matrix<float, 6, 1> desiredAcc,
        Eigen::Matrix<float, 6, 1> accWeight,
        float regWeight,
        Eigen::Matrix<float, 6, 1> g)
    {
        Eigen::Matrix<float, 6, 6> Q = Eigen::Matrix<float, 6, 6>::Zero();
        for (int i = 0; i < 6; ++i) {
            Q(i, i) = accWeight(i, 0);
        }
        Eigen::Matrix<float, 12, 12> R = Eigen::Matrix<float, 12, 12>::Ones();
        R = R * regWeight;
        Eigen::Matrix<float, 12, 12> quadTerm;
        Eigen::Matrix<float, 12, 1> linearTerm;
        quadTerm = massMatrix.transpose() * Q * massMatrix + R;
        linearTerm = (g + desiredAcc).transpose() * Q * massMatrix;
        std::tuple<Eigen::Matrix<float, 12, 12>, Eigen::Matrix<float, 12, 1>> quadLinear(quadTerm, linearTerm);
        return quadLinear;
    }

    Eigen::Matrix<float,12,12> ComputeWeightMatrix(Robot *robot, const Eigen::Matrix<bool, 4, 1>& contacts)
    {
        Eigen::Matrix<float,12,12> W = 1e-4 * Eigen::Matrix<float,12,12>::Identity(); // 1e-4
        // regularize the joint torque
        // for(int i=0; i<4; ++i) {
        //     if(contacts[i]) {
        //         auto Jv = robot->ComputeJacobian(i);
        //         W.block<3,3>(3*i,3*i) = Jv * W.block<3,3>(3*i,3*i) * Jv.transpose();
        //     }
        // }
        return W;
    }

    Eigen::Matrix<float, 3, 4> ComputeContactForce(Robot *robot,
                                                   GroundSurfaceEstimator* groundEstimator,
                                                   Eigen::Matrix<float, 6, 1> desiredAcc,
                                                   Eigen::Matrix<bool, 4, 1> contacts,
                                                   Eigen::Matrix<float, 6, 1> accWeight,
                                                   float regWeight,
                                                   float frictionCoef,
                                                   float fMinRatio,
                                                   float fMaxRatio)
    {
        
        Quat<float> quat = robot->GetBaseOrientation();
        Vec3<float> controlFrameRPY = groundEstimator->GetControlFrameRPY();
        Mat3<float> rotMatControlFrame = groundEstimator->GetAlignedDirections();
        Eigen::Matrix<float, 6, 1> g = Eigen::Matrix<float, 6, 1>::Zero();
        g(2, 0) = 9.8;
        TerrainType& goundType = groundEstimator->terrain.terrainType;
        Mat3<float> rotMat;
        if (goundType == TerrainType::PLANE || goundType==TerrainType::PLUM_PILES) {
            rotMat = Mat3<float>::Identity(); // control in base frame
        } else {
            // convert inertia in base frame to confrol frame
            rotMat = rotMatControlFrame.transpose() * robotics::math::quaternionToRotationMatrix(quat).transpose();
            // convert g from world frame to control frame
            g.head(3) = rotMatControlFrame.transpose() * g.head(3);
            fMaxRatio = fMaxRatio * cos(-controlFrameRPY[1]);
        }
        Mat3<float> inertia = rotMat * robot->bodyInertia * rotMat.transpose();
        Eigen::Matrix<float,3,4> footPosition = rotMat * robot->GetFootPositionsInBaseFrame();                
        Eigen::Matrix<float, 6, 12> massMatrix = ComputeMassMatrix(robot->bodyMass, // compute in control frame or base frame, according to terrain.
                                                                    inertia,
                                                                    footPosition.transpose());
        
        std::tuple<Eigen::Matrix<float, 12, 12>, Eigen::Matrix<float, 12, 1>> Ga;
        Ga = ComputeObjectiveMatrix(massMatrix, desiredAcc, accWeight, regWeight, g);
        Eigen::Matrix<float, 12, 12> G = std::get<0>(Ga);
        Eigen::Matrix<float,12,12> W = ComputeWeightMatrix(robot, contacts);
        G = G + W;
        Eigen::Matrix<float, 12, 1> a = std::get<1>(Ga);
        std::tuple<Eigen::Matrix<float, 12, 24>, Eigen::Matrix<float, 24, 1>> CI;
        Eigen::Matrix<float, 12, 24> Ci;
        Eigen::Matrix<float, 24, 1> b;
        CI = ComputeConstraintMatrix(robot->bodyMass, contacts, frictionCoef, fMinRatio, fMaxRatio);
        Ci = std::get<0>(CI);
        b = std::get<1>(CI);

        quadprogpp::Matrix<double> GG(12, 12);
        for (int i = 0; i < 12; i++) {
            for (int j = 0; j < 12; j++) {
                GG[i][j] = double(G(j, i));
            }
        }
        quadprogpp::Vector<double> aa(12);
        for (int i = 0; i < 12; i++) {
            aa[i] = double(-a(i, 0));
        }
        quadprogpp::Matrix<double> CICI(12, 24);
        for (int i = 0; i < 12; i++) {
            for (int j = 0; j < 24; j++) {
                CICI[i][j] = double(Ci(i, j));
            }
        }
        quadprogpp::Vector<double> bb(24);
        for (int i = 0; i < 24; i++) {
            bb[i] = double(-b(i, 0));
        }
        quadprogpp::Matrix<double> CECE(12, 0);
        quadprogpp::Vector<double> ee(0);
        quadprogpp::Vector<double> x(12);
        quadprogpp::solve_quadprog(GG, aa, CECE, ee, CICI, bb, x);
        //reshape x from (12,) to (4,3)
        Eigen::Matrix<float, 4, 3> X;
        int invalidResNum = 0;
        for (int index = 0; index < x.size(); index++) {
            if (isnan(x[index])) {
                invalidResNum++;
            }
        }
        if (invalidResNum > 0) {
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 3; ++j) {
                    X(i, j) = 0.f;
                }
            }
            std::cout << "[QP solver] No solution :" << invalidResNum << std::endl;
        } else {
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 3; ++j) {
                    X(i, j) = -float(x[3 * i + j]);
                }
            }
        }
        
        return (X*rotMat).transpose(); // convert force to current base frame
    }

    /** @brief Writen by Zhu Yijie, in world frame. Used for climbing stairs or slopes. */ 
    Eigen::Matrix<float, 3, 4> ComputeContactForce(Robot *robot,
                                                    Eigen::Matrix<float, 6, 1> desiredAcc,
                                                    Eigen::Matrix<bool, 4, 1> contacts,
                                                    Eigen::Matrix<float, 6, 1> accWeight,
                                                    Vec3<float> normal,
                                                    Vec3<float> tangent1,
                                                    Vec3<float> tangent2,
                                                    Vec4<float> fMinRatio,
                                                    Vec4<float> fMaxRatio,
                                                    float regWeight,
                                                    float frictionCoef)
    {
        Quat<float> quat = robot->GetBaseOrientation();
        Eigen::Matrix<float,3,4> footPositionsInBaseFrame = robot->GetFootPositionsInBaseFrame();
        Mat3<float> rotMat = robotics::math::quaternionToRotationMatrix(quat).transpose();
        Eigen::Matrix<float, 3, 4> footPositionsInCOMWorldFrame = robotics::math::invertRigidTransform<float,4>({0.f,0.f,0.f},quat, footPositionsInBaseFrame);
        Eigen::Matrix<float, 6, 12> massMatrix = ComputeMassMatrix(robot->bodyMass,
                                                                    robot->bodyInertia,
                                                                    footPositionsInCOMWorldFrame.transpose(),
                                                                    rotMat);
        std::tuple<Eigen::Matrix<float, 12, 12>, Eigen::Matrix<float, 12, 1>> Ga;
        Eigen::Matrix<float, 6, 1> g = Eigen::Matrix<float, 6, 1>::Zero();
        g(2, 0) = 9.8;
        Ga = ComputeObjectiveMatrix(massMatrix, desiredAcc, accWeight, regWeight, g);
        Eigen::Matrix<float, 12, 12> G = std::get<0>(Ga);
        Eigen::Matrix<float,12,12> W = ComputeWeightMatrix(robot, contacts);
        G = G + W;
        Eigen::Matrix<float, 12, 1> a = std::get<1>(Ga);
        
        std::tuple<Eigen::Matrix<float, 12, 24>, Eigen::Matrix<float, 24, 1>> CI;
        Eigen::Matrix<float, 12, 24> Ci;
        Eigen::Matrix<float, 24, 1> b;
        CI = ComputeConstraintMatrix(robot->bodyMass, contacts, frictionCoef, fMinRatio, fMaxRatio, normal, tangent1, tangent2);
        Ci = std::get<0>(CI);
        b = std::get<1>(CI);

        quadprogpp::Matrix<double> GG(12, 12);
        for (int i = 0; i < 12; i++) {
            for (int j = 0; j < 12; j++) {
                GG[i][j] = double(G(j, i));
            }
        }
        quadprogpp::Vector<double> aa(12);
        for (int i = 0; i < 12; i++) {
            aa[i] = double(-a(i, 0));
        }
        quadprogpp::Matrix<double> CICI(12, 24);
        for (int i = 0; i < 12; i++) {
            for (int j = 0; j < 24; j++) {
                CICI[i][j] = double(Ci(i, j));
            }
        }
        quadprogpp::Vector<double> bb(24);
        for (int i = 0; i < 24; i++) {
            bb[i] = double(-b(i, 0));
        }
        quadprogpp::Matrix<double> CECE(12, 0);
        quadprogpp::Vector<double> ee(0);
        quadprogpp::Vector<double> x(12);
        quadprogpp::solve_quadprog(GG, aa, CECE, ee, CICI, bb, x);
        //reshape x from (12,) to (4,3)
        Eigen::Matrix<float, 4, 3> X;
        int invalidResNum = 0;
        for (int index = 0; index < x.size(); index++) {
            if (isnan(x[index])) {
                invalidResNum++;
            }
        }
        if (invalidResNum > 0) {
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 3; ++j) {
                    X(i, j) = 0.f;
                }
            }
            std::cerr << "[QP solver] No solution :" << invalidResNum << std::endl;
            throw std::domain_error("qp torque");
        } else {
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 3; ++j) {
                    X(i, j) = -float(x[3 * i + j]);
                }
            }
        }
        return robotics::math::RigidTransform<float,4>({0.f,0.f,0.f}, quat, X.transpose());
    }


    /** @brief Writen by Zhu Yijie, in world frame. Used for climbing stairs or slopes. */ 
    Eigen::Matrix<float, 6, 12> ComputeMassMatrix(float robotMass,
                                                  Eigen::Matrix<float, 3, 3> robotInertia,
                                                  Eigen::Matrix<float, 4, 3> footPositions,
                                                  Mat3<float> rotMat)
    {
        Eigen::Matrix<float, 3, 3> I = Eigen::Matrix<float, 3, 3>::Identity(3, 3);
        Eigen::Matrix<float, 3, 3> invMass;
        Eigen::Matrix<float, 3, 3> invInertiaInWorld;
        Eigen::Matrix<float, 6, 12> massMat = Eigen::Matrix<float, 6, 12>::Zero();
        Eigen::Matrix<float, 1, 3> x;
        Eigen::Matrix<float, 3, 3> footPositionSkew;

        invMass = I / robotMass;
        Mat3<float> robotInertiaInWorld = rotMat * robotInertia * rotMat.transpose();
        invInertiaInWorld = robotInertiaInWorld.inverse();

        for (int legId = 0; legId < 4; ++legId) {
            massMat.block<3, 3>(0, legId * 3) = invMass;
            x = footPositions.row(legId);
            footPositionSkew << 0., -x[2], x[1],
                                x[2], 0., -x[0],
                                -x[1], x[0], 0.;
            massMat.block<3, 3>(3, legId * 3) = invInertiaInWorld * footPositionSkew;
        }
        return massMat;
    }


    /** @brief Writen by Zhu Yijie, in world frame. Used for climbing stairs or slopes. */
    std::tuple<Eigen::Matrix<float, 12, 24>, Eigen::Matrix<float, 24, 1>> ComputeConstraintMatrix(
                                                                            float mpcBodyMass,
                                                                            Eigen::Matrix<bool, 4, 1> contacts,
                                                                            float frictionCoef,
                                                                            Vec4<float> fMinRatio,
                                                                            Vec4<float> fMaxRatio,
                                                                            Vec3<float> normal,
                                                                            Vec3<float> tangent1,
                                                                            Vec3<float> tangent2)
    { 
        Eigen::Matrix<float, 24, 12> A = Eigen::Matrix<float, 24, 12>::Zero();
        Eigen::Matrix<float, 24, 1> lb = Eigen::Matrix<float, 24, 1>::Zero();

        for (int legId = 0; legId < 4; legId++) {
            A.block<1,3>(legId*2, legId*3) = normal;
            A.block<1,3>(legId*2+1, legId*3) = -normal;
            if (contacts[legId] > 0) {
                lb(legId * 2, 0) = fMinRatio[legId] * mpcBodyMass * 9.8;
                lb(legId * 2 + 1, 0) = -fMaxRatio[legId] * mpcBodyMass * 9.8;
            } else {
                lb(legId * 2, 0) = 1e-7;
                lb(legId * 2 + 1, 0) = 1e-7;
            }
        }
        // Friction cone constraints not parallel with world frame.
        int rowId;
        int colId;
        for (int legId = 0; legId < 4; ++legId) {
            rowId = 8 + legId * 4;
            colId = legId * 3;
            lb.block<4, 1>(rowId, 0) << 0., 0., 0., 0.;
            A.block<1, 3>(rowId, colId) << (frictionCoef*normal + tangent1).transpose();
            A.block<1, 3>(rowId + 1, colId) << (frictionCoef*normal - tangent1).transpose();
            A.block<1, 3>(rowId + 2, colId) << (frictionCoef*normal + tangent2).transpose();
            A.block<1, 3>(rowId + 3, colId) << (frictionCoef*normal - tangent2).transpose();
        }
        std::tuple<Eigen::Matrix<float, 12, 24>, Eigen::Matrix<float, 24, 1>> Alb(A.transpose(), lb);
        return Alb;
    }
} // namespace Quadruped