/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2011, Southwest Research Institute
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *  notice, this list of conditions and the following disclaimer in the
 *  documentation and/or other materials provided with the distribution.
 *  * Neither the name of the Southwest Research Institute, nor the names
 *  of its contributors may be used to endorse or promote products derived
 *  from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "motoman_driver/industrial_robot_client/robot_state_interface.h"
#include "motoman_driver/industrial_robot_client/motoman_utils.h"
#include "industrial_utils/param_utils.h"
#include <map>
#include <string>
#include <vector>

using industrial::smpl_msg_connection::SmplMsgConnection;
using industrial_utils::param::getJointNames;

namespace industrial_robot_client
{
namespace robot_state_interface
{

RobotStateInterface::RobotStateInterface()
{
  this->connection_ = NULL;
  this->add_handler(&default_joint_handler_);
  this->add_handler(&default_joint_feedback_handler_);
  this->add_handler(&default_joint_feedback_ex_handler_);
  this->add_handler(&default_robot_status_handler_);
}

bool RobotStateInterface::init(std::string default_ip, int default_port, bool version_0)
{
  std::string ip;
  int port;

  // override IP/port with ROS params, if available
  ros::param::param<std::string>("robot_ip_address", ip, default_ip);
  ros::param::param<int>("~port", port, default_port);
  // check for valid parameter values
  if (ip.empty())
  {
    ROS_ERROR("No valid robot IP address found.  Please set ROS 'robot_ip_address' param");
    return false;
  }
  if (port <= 0)
  {
    ROS_ERROR("No valid robot IP port found.  Please set ROS '~port' param");
    return false;
  }

  char* ip_addr = strdup(ip.c_str());  // connection.init() requires "char*", not "const char*"
  ROS_INFO("Robot state connecting to IP address: '%s:%d'", ip_addr, port);
  default_tcp_connection_.init(ip_addr, port);
  free(ip_addr);

  return init(&default_tcp_connection_);
}

bool RobotStateInterface::init(SmplMsgConnection* connection)
{
  std::map<int, RobotGroup> robot_groups;

  std::string value;
  if(!ros::param::search("topic_list", value))
  {
    ROS_WARN_STREAM(industrial_robot_client::motoman_utils::TOPIC_LIST_ERROR_MSG);
    this->version_0_ = true;
    std::vector<std::string> joint_names;
    if (!getJointNames("controller_joint_names", "robot_description", joint_names))
      ROS_WARN("Unable to read 'controller_joint_names' param.  Using standard 6-DOF joint names.");

    return init(connection, joint_names);
  }
  this->version_0_ = false;
  XmlRpc::XmlRpcValue topics_list_rpc;
  ros::param::get(value, topics_list_rpc);


  std::vector<XmlRpc::XmlRpcValue> topics_list;

  ROS_INFO_STREAM("Loading topic lists");
  ROS_INFO_STREAM("Found " << topics_list_rpc.size() << " topics");
  for (int i = 0; i < topics_list_rpc.size(); i++)
  {
    XmlRpc::XmlRpcValue state_value;
    state_value = topics_list_rpc[i];
    ROS_INFO_STREAM("Topic(state_value): " << state_value);
    topics_list.push_back(state_value);
  }

  /*
  std::vector<XmlRpc::XmlRpcValue> groups_list;
  // TODO(thiagodefreitas): check the consistency of the group numbers
  for (int i = 0; i < topics_list[0]["state"].size(); i++)
  {
    XmlRpc::XmlRpcValue group_value;
    group_value = topics_list[0]["state"][i];
    groups_list.push_back(group_value);
  }
  */

  for (int i = 0; i < topics_list.size(); i++)
  {
    ROS_INFO_STREAM("Loading group: " << topics_list[i]);
    RobotGroup rg;
    std::vector<std::string> rg_joint_names;

    XmlRpc::XmlRpcValue joints;

    joints = topics_list[i]["joints"];
    for (int jt = 0; jt < joints.size(); jt++)
    {
      rg_joint_names.push_back(static_cast<std::string>(joints[jt]));
    }

    XmlRpc::XmlRpcValue group_number;


    group_number = topics_list[i]["group"];
    int group_number_int = static_cast<int>(group_number);

    XmlRpc::XmlRpcValue name;
    std::string name_string;

    name = topics_list[i]["name"];
    name_string = static_cast<std::string>(name);

    XmlRpc::XmlRpcValue ns;
    std::string ns_string;

    ns = topics_list[i]["ns"];

    ns_string = static_cast<std::string>(ns);

    ROS_INFO_STREAM("Setting group: " );
    ROS_INFO_STREAM("  group number: " << group_number  );
    ROS_INFO_STREAM("  group number(int): " << group_number_int  );
    ROS_INFO_STREAM("  joints_names(size): " << rg_joint_names.size()  );
    ROS_INFO_STREAM("  name: " << name_string  );
    ROS_INFO_STREAM("  ns: " << ns_string );
    rg.set_group_id(group_number_int);
    rg.set_joint_names(rg_joint_names);
    rg.set_name(name_string);
    rg.set_ns(ns_string);

    robot_groups[group_number] = rg;
  }

  ROS_INFO_STREAM("Loaded " << robot_groups.size() << " groups");

  return init(connection, robot_groups);
}

bool RobotStateInterface::init(SmplMsgConnection* connection, std::map<int, RobotGroup> robot_groups)
{
  ROS_INFO_STREAM(" Initializing robot state " << robot_groups.size() << " groups");
  this->robot_groups_ = robot_groups;
  this->connection_ = connection;

  // initialize message-manager
  if (!manager_.init(connection_))
  {
    ROS_ERROR("Failed to initialize message manager");
    return false;
  }

  // initialize default handlers
  if (!default_joint_handler_.init(connection_, robot_groups_))
  {
    ROS_ERROR("Failed to initialialze joint handler");
    return false;
  }
  this->add_handler(&default_joint_handler_);

  if (!default_joint_feedback_handler_.init(connection_, robot_groups_))
  {
    ROS_ERROR("Failed to initialize joint feedback handler");
    return false;
  }
  this->add_handler(&default_joint_feedback_handler_);

  if (!default_joint_feedback_ex_handler_.init(connection_, robot_groups_))
  {
    ROS_ERROR("Failed to initialize joint(extended) feedback handler");
    return false;
  }
  this->add_handler(&default_joint_feedback_ex_handler_);

  if (!default_robot_status_handler_.init(connection_))
  {
    ROS_ERROR("Failed to initialize robot status handler");
    return false;
  }
  this->add_handler(&default_robot_status_handler_);

  connection_->makeConnect();

  ROS_INFO("Successfully initialized robot state interface");
  return true;
}

bool RobotStateInterface::init(SmplMsgConnection* connection, std::vector<std::string>& joint_names)
{
  this->joint_names_ = joint_names;
  this->connection_ = connection;
  connection_->makeConnect();

  // initialize message-manager
  if (!manager_.init(connection_))
    return false;

  // initialize default handlers
  if (!default_joint_handler_.init(connection_, joint_names_))
    return false;
  this->add_handler(&default_joint_handler_);

  if (!default_joint_feedback_handler_.init(connection_, joint_names_))
    return false;
  this->add_handler(&default_joint_feedback_handler_);

  if (!default_robot_status_handler_.init(connection_))
    return false;
  this->add_handler(&default_robot_status_handler_);

  return true;
}

void RobotStateInterface::run()
{
  manager_.spin();
}

}  // namespace robot_state_interface
}  // namespace industrial_robot_client
