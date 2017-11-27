/*
    dxl_sdk_test.cpp
    Copyright (C) 2017 Niryo
    All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "niryo_one_driver/niryo_one_communication.h"

NiryoOneCommunication::NiryoOneCommunication() 
{
    ros::param::get("~can_enabled", can_enabled);
    ros::param::get("~dxl_enabled", dxl_enabled);
    ros::param::get("~niryo_one_hw_check_connection_frequency", niryo_one_hw_check_connection_frequency);

    if (!can_enabled) {
        ROS_WARN("CAN communication is disabled for debug purposes");
    }
    if (!dxl_enabled) {
        ROS_WARN("DXL communication is disabled for debug purposes");
    }

    if (can_enabled) {
        canComm.reset(new CanCommunication());
    }

    if (dxl_enabled) {
        dxlComm.reset(new DxlCommunication());
    }

    new_calibration_requested = false;
    niryo_one_comm_ok = false;
    can_comm_ok = false;
    dxl_comm_ok = false;
}

int NiryoOneCommunication::init()
{
    int result = 0;
    if (can_enabled) {
        result = canComm->init();
        if (result != 0) {
            return result;
        }
    }
    if (dxl_enabled) {
        result =  dxlComm->init();
        if (result != 0) {
            return result;
        }
    }
    return result;
}

bool NiryoOneCommunication::scanAndCheckMotors()
{
    bool result = true;
    if (can_enabled) {
        result = ((canComm->scanAndCheck() == CAN_SCAN_OK) && result); 
    }
    if (dxl_enabled) {
        result = ((dxlComm->scanAndCheck() == DXL_SCAN_OK) && result);
    }
    niryo_one_comm_ok = result;
    return result;
}

bool NiryoOneCommunication::isConnectionOk()
{
    bool result = true;
    if (can_enabled) {
        result = ((canComm->isConnectionOk()) && result);
    }
    if (dxl_enabled) {
        result = ((dxlComm->isConnectionOk()) && result);
    }
    niryo_one_comm_ok = result;
    return result;
}

void NiryoOneCommunication::manageCanConnectionLoop()
{
    if (!can_enabled) { 
        return; 
    }

    ros::Rate check_connection_rate = ros::Rate(niryo_one_hw_check_connection_frequency);
    bool motors_ok = false;

    while (ros::ok()) {
        if (!canComm->isConnectionOk() || new_calibration_requested) {
            new_calibration_requested = false;
            ROS_WARN("Stop Can hw control");
            canComm->stopHardwareControlLoop();
            ros::Duration(0.1).sleep();
           
            while (canComm->scanAndCheck() != CAN_SCAN_OK) { // wait for connection to be up
                ROS_WARN("Scan to find stepper motors...");
                ros::Duration(0.25).sleep();
            }
            
            // once connected, set calibration flag
            ROS_INFO("Set calibration flag");
            canComm->setCalibrationFlag(true);
                
            // deactivate motors (?)
            canComm->setTorqueOn(false);
    
            canComm->startHardwareControlLoop(true); // limited mode
            motors_ok = false;

            while (!motors_ok) {
                // check if calibration could be done (user flag + execution success)
                int calibration_result = canComm->calibrateMotors(); 
                if (calibration_result == CAN_STEPPERS_CALIBRATION_OK) { 
                    motors_ok = true;
                    activateLearningMode(true);
                }
                else { // if calibration is not ok, wait and retry 
                    // check if connection is still ok
                    if (!canComm->isConnectionOk()) {
                        while (canComm->scanAndCheck() != CAN_SCAN_OK) { // wait for connection to be up
                            ROS_WARN("Scan to find stepper motors...");
                            ros::Duration(0.25).sleep();
                        }
                    }

                    // last calibration has failed, reset flag
                    if (calibration_result != CAN_STEPPERS_CALIBRATION_WAITING_USER_INPUT) {
                        canComm->setCalibrationFlag(true);
                        // go back to limited mode (during calibration, hw control loop is stopped)
                        canComm->startHardwareControlLoop(true); 
                    }
                    
                    ros::Duration(0.25).sleep();
                }
            }

            ROS_WARN("Resume can hw control");
            activateLearningMode(true);
            if (dxl_enabled) {
                canComm->startHardwareControlLoop(!dxlComm->isConnectionOk());
            }
            else {
                canComm->startHardwareControlLoop(false);
            }
        }
        else { // can connection ok + calibrated
            if (dxl_enabled && !dxlComm->isConnectionOk()) {
                if (!canComm->isOnLimitedMode()) {
                    canComm->startHardwareControlLoop(true);
                }
            }
            else {
                if (canComm->isOnLimitedMode()) {
                    canComm->setTorqueOn(false);
                    canComm->startHardwareControlLoop(false);
                }
            }
        }
        check_connection_rate.sleep();
    }
}

void NiryoOneCommunication::manageDxlConnectionLoop()
{
    if (!dxl_enabled) {
        return;
    }

    ros::Rate check_connection_rate = ros::Rate(niryo_one_hw_check_connection_frequency);

    while (ros::ok()) {
        if (!dxlComm->isConnectionOk()) {
            ROS_WARN("Stop Dxl hw control");
            dxlComm->stopHardwareControlLoop();
            ros::Duration(0.1).sleep();

            while (dxlComm->scanAndCheck() != DXL_SCAN_OK) { // wait for connection to be up
                ROS_WARN("Scan to find Dxl motors");
                ros::Duration(0.25).sleep();
            }

            ROS_WARN("Resume Dxl hw control");
            dxlComm->setTorqueOn(false);
            activateLearningMode(true);
            if (can_enabled) {
                dxlComm->startHardwareControlLoop(!canComm->isConnectionOk());
            }
            else {
                dxlComm->startHardwareControlLoop(false);
            }
        }
        else { // dxl connection ok
            if (can_enabled && !canComm->isConnectionOk()) {
                if (!dxlComm->isOnLimitedMode()) {
                    dxlComm->startHardwareControlLoop(true);
                }
            }
            else {
                if (dxlComm->isOnLimitedMode()) {
                    dxlComm->setTorqueOn(false);
                    dxlComm->startHardwareControlLoop(false);
                }
            }
        }
        check_connection_rate.sleep();
    }
}

void NiryoOneCommunication::manageHardwareConnection()
{
    can_connection_loop_thread.reset(new std::thread(boost::bind(&NiryoOneCommunication::manageCanConnectionLoop, this)));
    dxl_connection_loop_thread.reset(new std::thread(boost::bind(&NiryoOneCommunication::manageDxlConnectionLoop, this)));
}

void NiryoOneCommunication::startHardwareControlLoop()
{
    if (can_enabled) { canComm->startHardwareControlLoop(false); }
    if (dxl_enabled) { dxlComm->startHardwareControlLoop(false); }
}

void NiryoOneCommunication::stopHardwareControlLoop()
{
    if (can_enabled) { canComm->stopHardwareControlLoop(); }
    if (dxl_enabled) { dxlComm->stopHardwareControlLoop(); }
}

void NiryoOneCommunication::resumeHardwareControlLoop()
{
    if (can_enabled) { canComm->startHardwareControlLoop(false); }
    if (dxl_enabled) { dxlComm->startHardwareControlLoop(false); }
}

void NiryoOneCommunication::synchronizeMotors(bool begin_traj)
{
    if (can_enabled) {
        canComm->synchronizeSteppers(begin_traj);
    }
}

int NiryoOneCommunication::allowMotorsCalibrationToStart(int mode)
{
    int result = CAN_STEPPERS_CALIBRATION_OK; // todo

    if (can_enabled) {
        canComm->validateMotorsCalibrationFromUserInput(mode);
    }
    if (dxl_enabled) {
        // todo check dxl in bounds
    }
    
    return result;
}

void NiryoOneCommunication::requestNewCalibration()
{
    new_calibration_requested = true;
}

bool NiryoOneCommunication::isCalibrationInProgress()
{
    if (can_enabled) {
        return canComm->isCalibrationInProgress();
    }
    return false;
}

void NiryoOneCommunication::getHardwareStatus(bool *is_connection_ok, std::string &error_message, 
        int *calibration_needed, bool *calibration_in_progress,
        std::vector<std::string> &motor_names, std::vector<int32_t> &temperatures, std::vector<double> &voltages,
        std::vector<int32_t> &hw_errors)
{
    bool can_connection_ok = !can_enabled; // if CAN disabled, declare connection ok
    int can_calibration_needed = 0;
    bool can_calibration_in_progress = false;
    std::string can_error_message = "";
    std::vector<std::string> can_motor_names;
    std::vector<int32_t> can_temperatures;
    std::vector<double> can_voltages;
    std::vector<int32_t> can_hw_errors;
    
    bool dxl_connection_ok = !dxl_enabled; // if Dxl disabled, declare connection ok
    int dxl_calibration_needed = 0;
    bool dxl_calibration_in_progress = false;
    std::string dxl_error_message = "";
    std::vector<std::string> dxl_motor_names;
    std::vector<int32_t> dxl_temperatures;
    std::vector<double> dxl_voltages;
    std::vector<int32_t> dxl_hw_errors;

    if (can_enabled) {
        canComm->getHardwareStatus(&can_connection_ok, can_error_message, &can_calibration_needed,
                &can_calibration_in_progress, can_motor_names, can_temperatures, can_voltages, can_hw_errors);
    }
    if (dxl_enabled) {
        dxlComm->getHardwareStatus(&dxl_connection_ok, dxl_error_message, &dxl_calibration_needed,
                &dxl_calibration_in_progress, dxl_motor_names, dxl_temperatures, dxl_voltages, dxl_hw_errors);
    }

    motor_names.clear();
    temperatures.clear();
    voltages.clear();
    hw_errors.clear();

    motor_names.insert(motor_names.end(), can_motor_names.begin(), can_motor_names.end());
    motor_names.insert(motor_names.end(), dxl_motor_names.begin(), dxl_motor_names.end());
    temperatures.insert(temperatures.end(), can_temperatures.begin(), can_temperatures.end());
    temperatures.insert(temperatures.end(), dxl_temperatures.begin(), dxl_temperatures.end());
    voltages.insert(voltages.end(), can_voltages.begin(), can_voltages.end());
    voltages.insert(voltages.end(), dxl_voltages.begin(), dxl_voltages.end());
    hw_errors.insert(hw_errors.end(), can_hw_errors.begin(), can_hw_errors.end());
    hw_errors.insert(hw_errors.end(), dxl_hw_errors.begin(), dxl_hw_errors.end());

    *(is_connection_ok) = (can_connection_ok && dxl_connection_ok);
    *(calibration_needed) = (can_calibration_needed || dxl_calibration_needed);
    *(calibration_in_progress) = (can_calibration_in_progress || dxl_calibration_in_progress);
    error_message = "";
    error_message += can_error_message;
    if (dxl_error_message != "") { 
        error_message += "\n";
    }
    error_message += dxl_error_message;
}

void NiryoOneCommunication::getFirmwareVersions(std::vector<std::string> &motor_names,
        std::vector<std::string> &firmware_versions)
{
    std::vector<std::string> can_firmware_versions;
    std::vector<std::string> can_motor_names;

    if (can_enabled) {
        canComm->getFirmwareVersions(can_motor_names, can_firmware_versions);
    }

    motor_names.clear();
    firmware_versions.clear();

    firmware_versions.insert(firmware_versions.end(), can_firmware_versions.begin(), can_firmware_versions.end());
    motor_names.insert(motor_names.end(), can_motor_names.begin(), can_motor_names.end());
}

void NiryoOneCommunication::getCurrentPosition(double pos[6])
{
    if (can_enabled) { canComm->getCurrentPosition(&pos[0], &pos[1], &pos[2], &pos[3]); }
    if (dxl_enabled) { dxlComm->getCurrentPosition(&pos[4], &pos[5]); }

    // if disabled (debug purposes)
    if (!can_enabled) {
        pos[0] = pos_can_disabled[0];
        pos[1] = pos_can_disabled[1];
        pos[2] = pos_can_disabled[2];
        pos[3] = pos_can_disabled[3];
    }

    if (!dxl_enabled) {
        pos[4] = pos_dxl_disabled[0];
        pos[5] = pos_dxl_disabled[1];
    }
}

void NiryoOneCommunication::sendPositionToRobot(const double cmd[6])
{
    if (can_enabled) { canComm->setGoalPosition(cmd[0], cmd[1], cmd[2], cmd[3]); }
    if (dxl_enabled) { dxlComm->setGoalPosition(cmd[4], cmd[5]); }

    // if disabled (debug purposes)
    if (!can_enabled) {
        pos_can_disabled[0] = cmd[0];
        pos_can_disabled[1] = cmd[1];
        pos_can_disabled[2] = cmd[2];
        pos_can_disabled[3] = cmd[3];
    }

    if (!dxl_enabled) {
        pos_dxl_disabled[0] = cmd[4];
        pos_dxl_disabled[1] = cmd[5];
    }
}

void NiryoOneCommunication::activateLearningMode(bool activate) 
{
    if (can_enabled) { canComm->setTorqueOn(!activate); }
    if (dxl_enabled) { dxlComm->setTorqueOn(!activate); }
}

bool NiryoOneCommunication::setLeds(std::vector<int> &leds, std::string &message)
{
    if (leds.size() != 4) {
        message = "Led array must have 4 values";
        return false;
    }

    if (dxl_enabled) {
        dxlComm->setLeds(leds);
    }

    message = "Set LED ok";
    return true;
}

bool NiryoOneCommunication::activateDcMotor(bool activate)
{
    // TODO
    return true;
}

int NiryoOneCommunication::pullAirVacuumPump(uint8_t id, uint16_t pull_air_position, uint16_t pull_air_hold_torque)
{
    if (dxl_enabled) {
        return dxlComm->pullAirVacuumPump(id, pull_air_position, pull_air_hold_torque);
    }
    return VACUUM_PUMP_STATE_PULLED;
}

int NiryoOneCommunication::pushAirVacuumPump(uint8_t id, uint16_t push_air_position)
{
    if (dxl_enabled) {
        return dxlComm->pushAirVacuumPump(id, push_air_position);
    }
    return VACUUM_PUMP_STATE_PUSHED;
}

int NiryoOneCommunication::pingAndSetDxlTool(uint8_t id, std::string name)
{
    if (dxl_enabled) {
        return dxlComm->pingAndSetTool(id, name);
    }
    return TOOL_STATE_PING_OK;
}

int NiryoOneCommunication::openGripper(uint8_t id, uint16_t open_position, uint16_t open_speed, uint16_t open_hold_torque)
{
    if (dxl_enabled) {
        return dxlComm->openGripper(id, open_position, open_speed, open_hold_torque);
    }
    return GRIPPER_STATE_OPEN;
}

int NiryoOneCommunication::closeGripper(uint8_t id, uint16_t close_position, uint16_t close_speed, uint16_t close_hold_torque, uint16_t close_max_torque)
{
    if (dxl_enabled) {
        return dxlComm->closeGripper(id, close_position, close_speed, close_hold_torque, close_max_torque);
    }
    return GRIPPER_STATE_CLOSE;
}