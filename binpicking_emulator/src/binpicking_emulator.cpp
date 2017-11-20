/*********************************************************************
Copyright [2017] [Frantisek Durovsky]

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
 *********************************************************************/

#include "binpicking_emulator/binpicking_emulator.h"

using namespace pho_robot_loader;

BinpickingEmulator::BinpickingEmulator(ros::NodeHandle* nh) : trajectory_marker_index_(0)
{
  // Initialize Moveit group
  group_.reset(new moveit::planning_interface::MoveGroupInterface("manipulator"));

  // Load robot description
  robot_model_loader_.reset(new robot_model_loader::RobotModelLoader("robot_description"));

  // Load num of joints
  bool num_of_joints_success = nh->getParam("photoneo_module/num_of_joints", num_of_joints_);
  if (num_of_joints_success)
  {
    ROS_WARN("Not able to load ""num_of_joints"" from param server, using default value 6");
    num_of_joints_ = 6;
  }

  // Resize start and end pose arrays
  start_pose_from_robot_.resize(num_of_joints_);
  end_pose_from_robot_.resize(num_of_joints_);

  // Configure bin pose client
  bin_pose_client_ = nh->serviceClient<bin_pose_msgs::bin_pose>("bin_pose");

  // Set trajectory visualization publisher
  trajectory_pub_ = nh->advertise<visualization_msgs::Marker>("trajectory", 1);

  // Set move group params
  group_->setPlannerId("RRTConnectkConfigDefault");
  group_->setGoalTolerance(0.001);
}

BinpickingEmulator::~BinpickingEmulator()
{
}

bool BinpickingEmulator::binPickingInitCallback(photoneo_msgs::initialize_pose::Request& req,
                                                photoneo_msgs::initialize_pose::Response& res)
{
  ROS_INFO("BP EMULATOR: Binpicking Init Service called");

  std::stringstream start_pose_string, end_pose_string;

  for(int i = 0; i < num_of_joints_; i++)
  {
    start_pose_from_robot_[i] = req.startPose.position[i];
    end_pose_from_robot_[i] = req.endPose.position[i];

    start_pose_string << req.startPose.position[i] << " ";
    end_pose_string << req.endPose.position[i] << " ";
  }

  ROS_INFO("BP EMULATOR: START POSE: [%s] ", start_pose_string.str().c_str());
  ROS_INFO("BP EMULATOR: END POSE: [%s]", end_pose_string.str().c_str());

  res.success = true;
  res.result = 0;
  return true;
}

bool BinpickingEmulator::binPickingScanCallback(std_srvs::Trigger::Request& req, std_srvs::Trigger::Response& res)
{
  ROS_INFO("BP EMULATOR: Binpicking Scan Service called");
  res.success = true;
  return true;
}

bool BinpickingEmulator::binPickingTrajCallback(photoneo_msgs::operations::Request& req,
                                                photoneo_msgs::operations::Response& res)
{
  ROS_INFO("BP EMULATOR: Binpicking Trajectory Service called");

  int start_traj_size, approach_traj_size, grasp_traj_size, deapproach_traj_size, end_traj_size;
  moveit::planning_interface::MoveGroupInterface::Plan to_start_pose;
  moveit::planning_interface::MoveGroupInterface::Plan to_approach_pose;
  moveit::planning_interface::MoveGroupInterface::Plan to_grasp_pose;
  moveit::planning_interface::MoveGroupInterface::Plan to_deapproach_pose;
  moveit::planning_interface::MoveGroupInterface::Plan to_end_pose;

  // Get current state
  robot_state::RobotState current_state(*group_->getCurrentState());

  //---------------------------------------------------
  // Set Start state
  //---------------------------------------------------
  group_->setJointValueTarget(start_pose_from_robot_);
  group_->plan(to_start_pose);
  start_traj_size = to_start_pose.trajectory_.joint_trajectory.points.size();
  current_state.setJointGroupPositions(
      "manipulator", to_start_pose.trajectory_.joint_trajectory.points[start_traj_size - 1].positions);
  group_->setStartState(current_state);

  // Get random bin picking pose from emulator
  bin_pose_msgs::bin_pose srv;
  geometry_msgs::Pose approach_pose, grasp_pose, deapproach_pose;

  if (bin_pose_client_.call(srv))
  {
    grasp_pose = srv.response.grasp_pose;
    approach_pose = srv.response.approach_pose;
    deapproach_pose = srv.response.deapproach_pose;
  }

  //---------------------------------------------------
  // Plan trajectory from current to approach pose
  //---------------------------------------------------
  group_->setPoseTarget(approach_pose);
  bool success_approach = group_->plan(to_approach_pose);
  if (success_approach)
  {
    // Get trajectory size from plan
    approach_traj_size = to_approach_pose.trajectory_.joint_trajectory.points.size();

    // SetStartState instead of trajectory execution
    current_state.setJointGroupPositions(
        "manipulator", to_approach_pose.trajectory_.joint_trajectory.points[approach_traj_size - 1].positions);
    group_->setStartState(current_state);

    // Visualize trajectory in RViz
    visualizeTrajectory(to_approach_pose.trajectory_.joint_trajectory);
  }

  //---------------------------------------------------
  // Plan trajectory from approach to grasp pose
  //---------------------------------------------------
  group_->setPoseTarget(grasp_pose);
  bool success_grasp = group_->plan(to_grasp_pose);
  if (success_grasp)
  {
    // Get trajectory size from plan
    grasp_traj_size = to_grasp_pose.trajectory_.joint_trajectory.points.size();

    // SetStartState instead of trajectory execution
    current_state.setJointGroupPositions(
        "manipulator", to_grasp_pose.trajectory_.joint_trajectory.points[grasp_traj_size - 1].positions);
    group_->setStartState(current_state);

    // Visualize trajectory in RViz
    visualizeTrajectory(to_grasp_pose.trajectory_.joint_trajectory);
  }

  //---------------------------------------------------
  // Plan trajectory from grasp to deapproach pose
  //---------------------------------------------------
  group_->setPoseTarget(deapproach_pose);
  bool success_deapproach = group_->plan(to_deapproach_pose);
  if (success_deapproach)
  {
    // Get trajectory size from plan
    deapproach_traj_size = to_deapproach_pose.trajectory_.joint_trajectory.points.size();

    // SetStartState instead of trajectory execution
    current_state.setJointGroupPositions(
        "manipulator", to_deapproach_pose.trajectory_.joint_trajectory.points[deapproach_traj_size - 1].positions);
    group_->setStartState(current_state);

    // Visualize trajectory in RViz
    visualizeTrajectory(to_deapproach_pose.trajectory_.joint_trajectory);
  }

  //---------------------------------------------------
  // Plan trajectory from deapproach to end pose
  //---------------------------------------------------
  group_->setJointValueTarget(end_pose_from_robot_);
  bool success_end = group_->plan(to_end_pose);
  if (success_end)
  {
    // Get trajectory size from plan
    end_traj_size = to_end_pose.trajectory_.joint_trajectory.points.size();

    // SetStartState instead of trajectory execution
    current_state.setJointGroupPositions("manipulator",
                                         to_end_pose.trajectory_.joint_trajectory.points[end_traj_size - 1].positions);
    group_->setStartState(current_state);

    // Visualize trajectory in RViz
    visualizeTrajectory(to_end_pose.trajectory_.joint_trajectory);
  }

  //---------------------------------------------------
  // Compose binpicking as a sequence of operations
  //---------------------------------------------------

  // Check planning result
  if ((success_approach) && (success_grasp) && (success_deapproach) && (success_end))
  {
    photoneo_msgs::operation binpicking_operation;

    // Operation 1 - Approach Trajectory
    binpicking_operation.operation_type = OPERATION_TYPE::TRAJECTORY_FINE_TYPE;

    binpicking_operation.points.clear();
    for (int i = 0; i < approach_traj_size; i++)
      binpicking_operation.points.push_back(to_approach_pose.trajectory_.joint_trajectory.points[i]);

    binpicking_operation.gripper = 0;
    binpicking_operation.error = 0;
    binpicking_operation.info = 0;

    res.operations.push_back(binpicking_operation);

    // Operation 2 - Open Gripper
    binpicking_operation.operation_type = OPERATION_TYPE::GRIPPER_TYPE;
    binpicking_operation.points.clear();
    binpicking_operation.gripper = GRIPPER::OPEN;
    binpicking_operation.error = 0;
    binpicking_operation.info = 0;

    res.operations.push_back(binpicking_operation);

    // Operation 3 - Grasp Trajectory
    binpicking_operation.operation_type = OPERATION_TYPE::TRAJECTORY_FINE_TYPE;

    binpicking_operation.points.clear();
    for (int i = 0; i < grasp_traj_size; i++)
      binpicking_operation.points.push_back(to_grasp_pose.trajectory_.joint_trajectory.points[i]);

    binpicking_operation.gripper = 0;
    binpicking_operation.error = 0;
    binpicking_operation.info = 0;

    res.operations.push_back(binpicking_operation);

    // Operation 4 - Close Gripper
    binpicking_operation.operation_type = OPERATION_TYPE::GRIPPER_TYPE;
    binpicking_operation.points.clear();
    binpicking_operation.gripper = GRIPPER::CLOSE;
    binpicking_operation.error = 0;
    binpicking_operation.info = 0;

    res.operations.push_back(binpicking_operation);

    // Operation 5 - Deapproach trajectory
    binpicking_operation.operation_type = OPERATION_TYPE::TRAJECTORY_FINE_TYPE;

    binpicking_operation.points.clear();
    for (int i = 0; i < deapproach_traj_size; i++)
      binpicking_operation.points.push_back(to_deapproach_pose.trajectory_.joint_trajectory.points[i]);

    binpicking_operation.gripper = 0;
    binpicking_operation.error = 0;
    binpicking_operation.info = 0;

    res.operations.push_back(binpicking_operation);

    // Operation 6 - End Trajectory
    binpicking_operation.operation_type = OPERATION_TYPE::TRAJECTORY_FINE_TYPE;

    binpicking_operation.points.clear();
    for (int i = 0; i < end_traj_size; i++)
      binpicking_operation.points.push_back(to_end_pose.trajectory_.joint_trajectory.points[i]);

    binpicking_operation.gripper = 0;
    binpicking_operation.error = 0;
    binpicking_operation.info = 0;

    res.operations.push_back(binpicking_operation);

    return true;
  }
  else
  {
    return false;
  }
}

bool BinpickingEmulator::binPickingScanAndTrajCallback(photoneo_msgs::operations::Request& req,
                                                       photoneo_msgs::operations::Response& res)
{
  ROS_INFO("BP EMULATOR: Binpicking Service called");

  int start_traj_size, approach_traj_size, grasp_traj_size, deapproach_traj_size, end_traj_size;
  moveit::planning_interface::MoveGroupInterface::Plan to_start_pose;
  moveit::planning_interface::MoveGroupInterface::Plan to_approach_pose;
  moveit::planning_interface::MoveGroupInterface::Plan to_grasp_pose;
  moveit::planning_interface::MoveGroupInterface::Plan to_deapproach_pose;
  moveit::planning_interface::MoveGroupInterface::Plan to_end_pose;

  // Get current state
  robot_state::RobotState current_state(*group_->getCurrentState());

  //---------------------------------------------------
  // Set Start state
  //---------------------------------------------------
  group_->setJointValueTarget(start_pose_from_robot_);
  group_->plan(to_start_pose);
  start_traj_size = to_start_pose.trajectory_.joint_trajectory.points.size();
  current_state.setJointGroupPositions(
      "manipulator", to_start_pose.trajectory_.joint_trajectory.points[start_traj_size - 1].positions);
  group_->setStartState(current_state);

  // Get random bin picking pose from emulator
  bin_pose_msgs::bin_pose srv;
  geometry_msgs::Pose approach_pose, grasp_pose, deapproach_pose;

  if (bin_pose_client_.call(srv))
  {
    grasp_pose = srv.response.grasp_pose;
    approach_pose = srv.response.approach_pose;
    deapproach_pose = srv.response.deapproach_pose;
  }

  //---------------------------------------------------
  // Plan trajectory from current to approach pose
  //---------------------------------------------------
  group_->setPoseTarget(approach_pose);
  bool success_approach = group_->plan(to_approach_pose);
  if (success_approach)
  {
    // Get trajectory size from plan
    approach_traj_size = to_approach_pose.trajectory_.joint_trajectory.points.size();

    // SetStartState instead of trajectory execution
    current_state.setJointGroupPositions(
          "manipulator", to_approach_pose.trajectory_.joint_trajectory.points[approach_traj_size - 1].positions);
    group_->setStartState(current_state);

    // Visualize trajectory in RViz
    visualizeTrajectory(to_approach_pose.trajectory_.joint_trajectory);
  }

  //---------------------------------------------------
  // Plan trajectory from approach to grasp pose
  //---------------------------------------------------
  group_->setPoseTarget(grasp_pose);
  bool success_grasp = group_->plan(to_grasp_pose);
  if (success_grasp)
  {
    // Get trajectory size from plan
    grasp_traj_size = to_grasp_pose.trajectory_.joint_trajectory.points.size();

    // SetStartState instead of trajectory execution
    current_state.setJointGroupPositions(
          "manipulator", to_grasp_pose.trajectory_.joint_trajectory.points[grasp_traj_size - 1].positions);
    group_->setStartState(current_state);

    // Visualize trajectory in RViz
    visualizeTrajectory(to_grasp_pose.trajectory_.joint_trajectory);
  }

  //---------------------------------------------------
  // Plan trajectory from grasp to deapproach pose
  //---------------------------------------------------
  group_->setPoseTarget(deapproach_pose);
  bool success_deapproach = group_->plan(to_deapproach_pose);
  if (success_deapproach)
  {
    // Get trajectory size from plan
    deapproach_traj_size = to_deapproach_pose.trajectory_.joint_trajectory.points.size();

    // SetStartState instead of trajectory execution
    current_state.setJointGroupPositions(
          "manipulator", to_deapproach_pose.trajectory_.joint_trajectory.points[deapproach_traj_size - 1].positions);
    group_->setStartState(current_state);

    // Visualize trajectory in RViz
    visualizeTrajectory(to_deapproach_pose.trajectory_.joint_trajectory);
  }

  //---------------------------------------------------
  // Plan trajectory from deapproach to end pose
  //---------------------------------------------------
  group_->setJointValueTarget(end_pose_from_robot_);
  bool success_end = group_->plan(to_end_pose);
  if (success_end)
  {
    // Get trajectory size from plan
    end_traj_size = to_end_pose.trajectory_.joint_trajectory.points.size();

    // SetStartState instead of trajectory execution
    current_state.setJointGroupPositions("manipulator", to_end_pose.trajectory_.joint_trajectory.points[end_traj_size - 1].positions);
    group_->setStartState(current_state);

    // Visualize trajectory in RViz
    visualizeTrajectory(to_end_pose.trajectory_.joint_trajectory);
  }

  //---------------------------------------------------
  // Compose binpicking as a sequence of operations
  //---------------------------------------------------

  // Check planning result
  if ((success_approach) && (success_grasp) && (success_deapproach) && (success_end))
  {
    photoneo_msgs::operation binpicking_operation;

    // Operation 1 - Approach Trajectory
    binpicking_operation.operation_type = OPERATION_TYPE::TRAJECTORY_FINE_TYPE;

    binpicking_operation.points.clear();
    for (int i = 0; i < approach_traj_size; i++)
      binpicking_operation.points.push_back(to_approach_pose.trajectory_.joint_trajectory.points[i]);

    binpicking_operation.gripper = GRIPPER::OPEN;
    binpicking_operation.error = 0;
    binpicking_operation.info = 0;

    res.operations.push_back(binpicking_operation);

    // Operation 2 - Open Gripper
    binpicking_operation.operation_type = OPERATION_TYPE::GRIPPER_TYPE;
    binpicking_operation.points.clear();
    binpicking_operation.gripper = GRIPPER::OPEN;
    binpicking_operation.error = 0;
    binpicking_operation.info = 0;

    res.operations.push_back(binpicking_operation);

    // Operation 3 - Grasp Trajectory
    binpicking_operation.operation_type = OPERATION_TYPE::TRAJECTORY_FINE_TYPE;

    binpicking_operation.points.clear();
    for (int i = 0; i < grasp_traj_size; i++)
      binpicking_operation.points.push_back(to_grasp_pose.trajectory_.joint_trajectory.points[i]);

    binpicking_operation.gripper = GRIPPER::OPEN;
    binpicking_operation.error = 0;
    binpicking_operation.info = 0;

    res.operations.push_back(binpicking_operation);

    // Operation 4 - Close Gripper
    binpicking_operation.operation_type = OPERATION_TYPE::GRIPPER_TYPE;
    binpicking_operation.points.clear();
    binpicking_operation.gripper = GRIPPER::CLOSE;
    binpicking_operation.error = 0;
    binpicking_operation.info = 0;

    res.operations.push_back(binpicking_operation);

    // Operation 5 - Deapproach trajectory
    binpicking_operation.operation_type = OPERATION_TYPE::TRAJECTORY_FINE_TYPE;

    binpicking_operation.points.clear();
    for (int i = 0; i < deapproach_traj_size; i++)
      binpicking_operation.points.push_back(to_deapproach_pose.trajectory_.joint_trajectory.points[i]);

    binpicking_operation.gripper = GRIPPER::CLOSE;
    binpicking_operation.error = 0;
    binpicking_operation.info = 0;

    res.operations.push_back(binpicking_operation);

    // Operation 6 - End Trajectory
    binpicking_operation.operation_type = OPERATION_TYPE::TRAJECTORY_FINE_TYPE;

    binpicking_operation.points.clear();
    for (int i = 0; i < end_traj_size; i++)
      binpicking_operation.points.push_back(to_end_pose.trajectory_.joint_trajectory.points[i]);

    binpicking_operation.gripper = GRIPPER::CLOSE;
    binpicking_operation.error = 0;
    binpicking_operation.info = 0;

    res.operations.push_back(binpicking_operation);

    return true;
  }
  else
  {
    return false;
  }
}

bool BinpickingEmulator::calibrationAddPointCallback(photoneo_msgs::calibration::Request& req, photoneo_msgs::calibration::Response& res)
{
  ROS_INFO("BP EMULATOR: Calibration Add Point Service called");
  res.calibration_state = 0;
  res.reprojection_error = 0.12345;
  return true;
}

bool BinpickingEmulator::calibrationSetToScannerCallback(std_srvs::Trigger::Request& req, std_srvs::Trigger::Response& res)
{
  ROS_INFO("BP EMULATOR: Calibration Set To Scanner Service called");
  res.success = true;
  return true;
}

bool BinpickingEmulator::calibrationResetCallback(std_srvs::Trigger::Request& req, std_srvs::Trigger::Response& res)
{
  ROS_INFO("BP EMULATOR: Calibration Reset Service called");
  res.success = true;
  return true;
}

void BinpickingEmulator::visualizeTrajectory(trajectory_msgs::JointTrajectory trajectory)
{
  visualization_msgs::Marker marker;

  // Kinematic variables
  robot_model::RobotModelPtr kinematic_model = robot_model_loader_->getModel();
  robot_state::RobotStatePtr kinematic_state(new robot_state::RobotState(kinematic_model));

  marker.header.frame_id = "/base_link";
  marker.ns = "trajectory";
  marker.type = visualization_msgs::Marker::SPHERE;
  marker.action = visualization_msgs::Marker::ADD;

  for (int i = 0; i < trajectory.points.size(); i++)
  {
    kinematic_state->setJointGroupPositions("manipulator", trajectory.points[i].positions);
    const Eigen::Affine3d& end_effector_state = kinematic_state->getGlobalLinkTransform("tool0");

    marker.header.stamp = ros::Time::now();
    marker.id = trajectory_marker_index_++;

    marker.pose.position.x = end_effector_state.translation()[0];
    marker.pose.position.y = end_effector_state.translation()[1];
    marker.pose.position.z = end_effector_state.translation()[2];

    marker.scale.x = 0.01;
    marker.scale.y = 0.01;
    marker.scale.z = 0.01;

    marker.color.r = 0.9f;
    marker.color.g = 0.9f;
    marker.color.b = 0.9f;
    marker.color.a = 1.0;

    marker.lifetime = ros::Duration(5);
    trajectory_pub_.publish(marker);
    ros::Duration(0.001).sleep();
  }
}

int main(int argc, char** argv)
{
  ros::init(argc, argv, "binpicking_emulator");
  ros::NodeHandle nh;

  // Initial wait for Moveit to be properly loaded
  ros::Duration(3).sleep();

  // Wait for moveit services
  bool moveit_available = ros::service::exists("/compute_ik", true);
  while (!moveit_available)
  {
     ros::Duration(1).sleep();
     ROS_WARN("BP EMULATOR: Waiting for Moveit Config to be properly loaded!");
     moveit_available = ros::service::exists("/compute_ik", true);
  }

  // Wait for bin_pose service
  bool bin_pose_emulator_available = ros::service::exists("/bin_pose", true);
  while (!bin_pose_emulator_available)
  {
    ros::Duration(1).sleep();
    bin_pose_emulator_available = ros::service::exists("/bin_pose", true);
    ROS_WARN("BP EMULATOR: Waiting for Bin pose emulator to provide /bin_pose service ");
  }

  // Create BinpickingEmulator instance
  BinpickingEmulator emulator(&nh);

  // Advertise service
  ros::ServiceServer bin_picking_scan_service =
      nh.advertiseService(BINPICKING_SERVICES::SCAN, &BinpickingEmulator::binPickingScanCallback, &emulator);
  ros::ServiceServer bin_picking_traj_service =
      nh.advertiseService(BINPICKING_SERVICES::TRAJECTORY, &BinpickingEmulator::binPickingTrajCallback, &emulator);
  ros::ServiceServer bin_picking_scan_and_traj_service = nh.advertiseService(
      BINPICKING_SERVICES::SCAN_AND_TRAJECTORY, &BinpickingEmulator::binPickingScanAndTrajCallback, &emulator);
  ros::ServiceServer bin_picking_init_service =
      nh.advertiseService(BINPICKING_SERVICES::INITIALIZE, &BinpickingEmulator::binPickingInitCallback, &emulator);
  ros::ServiceServer calibration_add_point_service =
      nh.advertiseService(CALIBRATION_SERVICES::ADD_POINT, &BinpickingEmulator::calibrationAddPointCallback, &emulator);
  ros::ServiceServer calibration_set_to_scanner_service =
      nh.advertiseService(CALIBRATION_SERVICES::SET_TO_SCANNER, &BinpickingEmulator::calibrationSetToScannerCallback, &emulator);
  ros::ServiceServer calibration_reset_service =
      nh.advertiseService(CALIBRATION_SERVICES::RESET, &BinpickingEmulator::calibrationResetCallback, &emulator);

  ROS_WARN("BP EMULATOR: Ready");

  // Start Async Spinner with 2 threads
  ros::AsyncSpinner spinner(2);
  spinner.start();
  ros::waitForShutdown();

  return EXIT_SUCCESS;
}
