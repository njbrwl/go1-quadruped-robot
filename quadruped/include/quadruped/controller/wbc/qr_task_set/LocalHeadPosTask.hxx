// The MIT License

// Copyright (c) 2022 
// Robot Motion and Vision Laboratory at East China Normal University
// Contact:  tophill.robotics@gmail.com

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "LocalHeadPosTask.hpp"

#include <Configuration.h>
#include <Dynamics/FloatingBaseModel.h>
#include <Dynamics/Quadruped.h>
#include <Math/orientation_tools.h>
#include <Utilities/Utilities_print.h>

template <typename T>
LocalHeadPosTask<T>::LocalHeadPosTask(const FloatingBaseModel<T>* robot)
    : qrTask<T>(3), _robot_sys(robot) {
  TK::Jt_ = DMat<T>::Zero(TK::dim_task_, cheetah::dim_config);
  TK::JtDotQdot_ = DVec<T>::Zero(TK::dim_task_);

  _Kp_kin = DVec<T>::Constant(TK::dim_task_, 1.);
  _Kp = DVec<T>::Constant(TK::dim_task_, 50.);
  _Kd = DVec<T>::Constant(TK::dim_task_, 3.);
}

template <typename T>
LocalHeadPosTask<T>::~LocalHeadPosTask() {}

template <typename T>
bool LocalHeadPosTask<T>::_UpdateCommand(const void* pos_des, const DVec<T>& vel_des,
                                         const DVec<T>& acc_des) {
  Vec3<T>* pos_cmd = (Vec3<T>*)pos_des;
  Vec3<T> link_pos, link_vel, local_pos, local_vel;
  link_pos = 0.5 * _robot_sys->_pGC[linkID::FR_abd] +
             0.5 * _robot_sys->_pGC[linkID::FL_abd];
  link_vel = 0.5 * _robot_sys->_vGC[linkID::FR_abd] +
             0.5 * _robot_sys->_vGC[linkID::FL_abd];

  local_pos =
      0.5 * _robot_sys->_pGC[linkID::FR] + 0.5 * _robot_sys->_pGC[linkID::FL];
  local_vel =
      0.5 * _robot_sys->_vGC[linkID::FR] + 0.5 * _robot_sys->_vGC[linkID::FL];

  // X, Y, Z
  for (size_t i(0); i < TK::dim_task_; ++i) {
    TK::pos_err_[i] =
        _Kp_kin[i] * ((*pos_cmd)[i] - (link_pos[i] - local_pos[i]));
    TK::vel_des_[i] = vel_des[i];
    TK::acc_des_[i] = acc_des[i];

    TK::op_cmd_[i] = _Kp[i] * ((*pos_cmd)[i] - (link_pos[i] - local_pos[i])) +
                     _Kd[i] * (link_vel[i] - local_vel[i]) + TK::acc_des_[i];
  }
  // printf("[Body Ori Pitch Yaw Task]\n");
  // pretty_print(TK::pos_err_, std::cout, "pos_err_");
  // pretty_print(*ori_cmd, std::cout, "des_ori");
  // pretty_print(link_ori, std::cout, "curr_ori");
  // pretty_print(ori_err, std::cout, "quat_err");

  // pretty_print(link_ori_inv, std::cout, "ori_inv");
  // pretty_print(ori_err, std::cout, "ori_err");
  // pretty_print(*ori_cmd, std::cout, "cmd");
  // pretty_print(acc_des, std::cout, "acc_des");
  // pretty_print(TK::Jt_, std::cout, "Jt");

  return true;
}

template <typename T>
bool LocalHeadPosTask<T>::_UpdateTaskJacobian() {
  TK::Jt_ = 0.5 * _robot_sys->_Jc[linkID::FR_abd] +
            0.5 * _robot_sys->_Jc[linkID::FL_abd] -
            0.5 * _robot_sys->_Jc[linkID::FR] -
            0.5 * _robot_sys->_Jc[linkID::FL];

  return true;
}

template <typename T>
bool LocalHeadPosTask<T>::_UpdateTaskJDotQdot() {
  return true;
}

template class LocalHeadPosTask<double>;
template class LocalHeadPosTask<float>;
