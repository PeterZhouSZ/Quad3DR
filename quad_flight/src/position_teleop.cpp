// Adapted from hector_quadrotor package (https://github.com/tu-darmstadt-ros-pkg/hector_quadrotor).

// Original Copyright note:
// Copyright (c) 2012, Johannes Meyer, TU Darmstadt
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of the Flight Systems and Automatic Control group,
//       TU Darmstadt, nor the names of its contributors may be used to
//       endorse or promote products derived from this software without
//       specific prior written permission.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//=================================================================================================


#include <ros/ros.h>
#include <sensor_msgs/Joy.h>
#include <geometry_msgs/Pose.h>
#include <tf/tf.h>

namespace quad_flight
{

class PositionTeleop
{
private:
  ros::NodeHandle node_handle_;
  ros::Subscriber joy_subscriber_;

  ros::Publisher pose_publisher_;
  geometry_msgs::Pose pose_;
  tf::Quaternion orientation_;

  struct Axis
  {
    int axis;
    double max;
  };

  struct Button
  {
    int button;
  };

  struct
  {
    Axis x;
    Axis y;
    Axis z;
    Axis yaw;
  } axes_;

  struct
  {
    Button fast;
  } buttons_;

  double fast_factor_;
  double slow_factor_;
  double deadzone_;

public:
  PositionTeleop()
  {
    ros::NodeHandle params("~");

    params.param<int>("x_axis", axes_.x.axis, 4);
    params.param<int>("y_axis", axes_.y.axis, 3);
    params.param<int>("z_axis", axes_.z.axis, 2);
    params.param<int>("yaw_axis", axes_.yaw.axis, 1);

    params.param<double>("yaw_velocity_max", axes_.yaw.max, 90.0 * M_PI / 180.0);
    params.param<int>("fast_button", buttons_.fast.button, 1);
    params.param<double>("fast_factor", fast_factor_, 0.2);
    params.param<double>("slow_factor", slow_factor_, 0.05);
    params.param<double>("deadzone", deadzone_, 0.05);

    params.param<double>("x_velocity_max", axes_.x.max, 2.0);
    params.param<double>("y_velocity_max", axes_.y.max, 2.0);
    params.param<double>("z_velocity_max", axes_.z.max, 2.0);

    orientation_.setRPY(0, 0, 0);

    joy_subscriber_ = node_handle_.subscribe<sensor_msgs::Joy>("joy", 1, boost::bind(&PositionTeleop::joyPoseCallback, this, _1));
    pose_publisher_ = node_handle_.advertise<geometry_msgs::Pose>("cmd_pose", 10);
  }

  ~PositionTeleop()
  {
    stop();
  }

  void joyPoseCallback(const sensor_msgs::JoyConstPtr &joy)
  {
    double factor = slow_factor_;
    if (getButton(joy, buttons_.fast.button)) {
      factor = fast_factor_;
    }
    pose_.position.x += factor * getAxis(joy, axes_.x);
    pose_.position.y += factor * getAxis(joy, axes_.y);
    pose_.position.z += factor * getAxis(joy, axes_.z);
    double yaw = tf::getYaw(orientation_);
    yaw += factor * getAxis(joy, axes_.yaw);
    orientation_.setRPY(0, 0, yaw);
    tf::quaternionTFToMsg(orientation_, pose_.orientation);
    pose_publisher_.publish(pose_);
  }

  sensor_msgs::Joy::_axes_type::value_type getAxis(const sensor_msgs::JoyConstPtr &joy, Axis axis)
  {
    if (axis.axis == 0)
    {
      return 0;
    }
    sensor_msgs::Joy::_axes_type::value_type sign = 1.0;
    if (axis.axis < 0)
    {
      sign = -1.0;
      axis.axis = -axis.axis;
    }
    if ((size_t)axis.axis > joy->axes.size())
    {
      return 0;
    }
    float value = joy->axes[axis.axis - 1];
    if (std::fabs(value) < deadzone_)
    {
      return 0;
    }
    return sign * value * axis.max;
  }

  sensor_msgs::Joy::_buttons_type::value_type getButton(const sensor_msgs::JoyConstPtr &joy, int button)
  {
    if (button <= 0)
    {
      return 0;
    }
    if ((size_t) button > joy->buttons.size())
    {
      return 0;
    }
    return joy->buttons[button - 1];
  }

  void stop()
  {
    if(pose_publisher_.getNumSubscribers() > 0)
    {
      pose_ = geometry_msgs::Pose();
      pose_publisher_.publish(pose_);
    }
  }
};

}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "position_teleop");

  quad_flight::PositionTeleop teleop;
  ros::spin();

  return 0;
}

