#ifndef Robot_H
#define Robot_H

#include "..\Dependencies\eigen-3.3.7\Eigen\Dense"
#include "GripperController.h"
#include "RailController.h"
#include "CableController.h"
#include "BrakeController.h"
#include "Logger.h"
#include "utils.h"
#include <string>
#include <vector>
using namespace Eigen;
using namespace std;

// CDPR with specific robot parameters
class Robot{
private:
    bool isOnline{false};
    bool isValidModel{false};
    bool isConnected{false};
    int cableMotorNum{8}; // !!!!! IMPORTANT !!!!! Put in the number of motors for cables before compiling the programme
    int cableMotorBrakeNum{4}; // !!!!! IMPORTANT !!!!! Put in the number of motors for cables before compiling the programme    
    int railMotorNum{4}; // Typical 4 linear rails
    double endEffToGroundOffset = -0.125; // offset from endeffector to ground, in meters // YES -0.280
    double cableMotorScale = 509295.8179; // 6400 encoder count per revoltion, 25 times gearbox, 50mm spool radias. ie 6400*25/(2*pi*0.05) 
    double cableMotorScaleIndividual[8] = {509295.8179, 509295.8179, 509295.8179, 509295.8179, 509295.8179, 509295.8179, 509295.8179, 509295.8179}; // 6400 encoder count per revoltion, 25 times gearbox, 50mm spool radias. ie 6400*25/(2*pi*0.05) 
    double railMotorScale = 38400000; // 6400 encoder count per revoltion, 30 times gearbox, linear rail pitch 5mm. ie 6400*30/0.005 
    double pulleyRadius = 0.045; // radius of rotating pulley on frame
    float targetTrq = -2.5; // in %, -ve for tension, also need to UPDATE in switch case 't'!!!!!!!!!
    float absTrqLmt = 60.0; // Absolute torque limit, to be checked before commanding 8 motors to move simutaneously in length command
    float endEffectorPosLimit[6]{-0.1, 11.5, -0.02, 13.8, -3.3, 0.3}; // Rough boundaries of the cable robot
    float endEffVelLmt = 0.45;
    float railVelLmt = 0.01;
    float brickPickUpOffset = 0.21;
    Vector3d frmOut[8]; // coordinates of the attachment points on frame at the beginning
    Vector3d endOut[8]; // local coordinates of cable attachment points on end-effector, ie ^er_B
    Vector3d frmOutUnitV[8]; // unit vectors/directions of the fixed cable attachments on frame
    string gripperCommPort; // Gripper Communicate Port 
    string railBrakeCommPort; // Rail Brake Communicate Port 
    string cableBrakeCommPort; // Rail Brake Communicate Port 
    string posLabel[6]{"x", "y", "z", "roll", "pitch", "yaw"}; 
    bool useGripper{ false};
    bool useCableMotor{ false };
    bool useCableBraker{ false };
    bool useRailMotor{ false };
    bool useRailBraker{ false };
    bool useEBrake{true};
    bool useIndividualCableScale{false};
    Logger posLogger;
    int MILLIS_TO_NEXT_FRAME = 50;
    bool eBrake(bool cableBrake, bool railBrake);


public:
    int railMotorCableMotorOffset = 2; // offset from rail motor to cable motor, in index
    double rotationalAngleOffset{0}; // rotational angel offset
    double rotationalDistanceOffset{0}; // rotational distance offset
    
    double homePos[6]{}; // home posisiton
    double endEffectorPos[6]{}; // end-effector task-space position {x,y,z, alpha, beta, gama}, in meter and radian
    // double cableLength[8]{}; // cable lengths in meter //12 assume there are 8 motors + 4 linear rails
    double railOffset[4]{}; // individual heights or positions of the rails
    double startRailOffset[4]{}; // individual heights or positions of the rails at the start of the program. For raise rail with cable command 
    double brickPickUpPos[6]{}; // position at 5th pole for brick pick up with possible z-rotaion
    double brickPrePickUpPos[6]{}; // position at 5th pole for brick pick up with possible z-rotaion
    CableController cable;
    RailController rail;
    GripperController gripper;
    BrakeController brake;
    string RPiIP = "192.168.50.138";
    int RPiPort = 5000;

    int safeT = 1500;
// Constructor
    Robot();
    Robot(string robotConfigPath);
    void Connect();
    void Disconnect();
    bool IsConnected();

// Update
    /// @brief Read robot model config from file.
    /// @param filename string. Config file name.
    void UpdateModelFromFile(string filename, bool reconnect=false);

    /// @brief Read robot Position from csv/txt file
    /// @param filename string. Position file name
    void UpdatePosFromFile(string filename, bool calibration=true);

    /// @brief Read robot Position from csv/txt file
    /// @param filename string. Position file name
    void UpdatePosFromLog(string filename);

    /// @brief Update end effector pos
    /// @param pos double array, end effector pos
    void UpdatePos(double pos[6]);


// Verify
    /// @brief Check whether the robot is valid, ususally used after load the robot config.
    /// @return Bool
    bool IsValid();

    /// @brief Check whether the end effector position is in the limits or not.
    /// @return Bool.
    bool CheckLimits();

// Converter
    /// @brief Give end effector pos and rail offset, get cable length and update the lengths attribute. 
    /// @param eePos double array, Input end effector pos
    /// @param railOffset double array, Input rail offset. 
    /// @param lengths double array, Output cable length
    vector<double> EEPoseToCableLength(double eePos[], double railOffset[]);

    /// @brief Give end effector pos and rail offset, get cable length and update the lengths attribute. 
    /// @param eePos double array, Input end effector pos
    /// @param railOffset double array, Input rail offset. 
    /// @param lengths double array, Output cable length
    vector<double> EEPoseToCableLength(vector<double>, double railOffset[]);

    /// @brief Give end effector pos and rail offset, get cable length and update the lengths attribute. 
    /// @param eePos double array, Input end effector pos
    /// @param lengths double array, Output cable length
    vector<double> EEPoseToCableLength(double eePos[]);

    /// @brief Give end effector pos and rail offset, get cable length and update the lengths attribute. 
    /// @param eePos double vector, Input end effector pos
    /// @param lengths double array, Output cable length
    vector<double> EEPoseToCableLength(vector<double> eePos);

    /// @brief Convert cable length to motor command
    /// @param motorID int, motor ID
    /// @param length double, cable length
    /// @return int32_t motor command
    int32_t CableMotorLengthToCmd(int motorID, double length);

    /// @brief Convert cable motor command to length
    /// @param motorID int, motor ID
    /// @param cmd int32_t, motor command
    /// @return double, cable length
    double CableMotorCmdToLength(int motorID, int cmd);

    /// @brief Convert rail height to motor command
    /// @param motorID int, motor ID
    /// @param length double, cable length
    /// @return int32_t motor command
    int32_t RailMotorLengthToCmd(int motorID, double length);

    /// @brief Convert rail motor command to length
    /// @param motorID int, motor ID
    /// @param cmd int32_t, motor command
    /// @return double, rail length
    double RailMotorCmdToLength(int motorID, int cmd);

    vector<int32_t> EEPoseToCmd(vector<double> eePos);

    vector<int32_t> EEPoseToCmd(vector<double> eePos, double railOffset[]);

    vector<int32_t> EEPoseToCmd(double eePos[], double railOffset[]);
    
    vector<int32_t> EEPoseToCmd(double eePos[]);

    vector<vector<int32_t>> PoseTrajToCmdTraj(vector<vector<double>> trajectory);
    
// Print Data
    /// @brief Pint current end effector pos
    void PrintEEPos();

    /// @brief Pint brick pick pos
    void PrintBrickPickUpPos();

    /// @brief Print current rail offset
    void PrintRailOffset();

    /// @brief Print current cable length 
    void PrintCableLength();

    /// @brief Print home pos
    void PrintHomePos();

    /// @brief Print robot config file
    void PrintRobotConfig();

// Getter

    vector<double> GetCableLength();
    /// @brief Get end effector offset from end effector to ground, in meter.
    /// @return Double, Effector offset from end effector to ground, in meter.
    double GetEEToGroundOffset();

    /// @brief Get target torque.
    /// @return float, Target torque.
    float GetWorkingTrq();

    /// @brief Absolute torque limit
    /// @return float, Absolute torque limit
    float GetAbsTrqLmt();

    /// @brief Get cable motors number.
    /// @return int, number of cable motor
    int GetCableMotorNum();

    /// @brief Get rail motors number.
    /// @return int, number of rail motor
    int GetRailMotorNum();

    /// @brief Get Gripper Comm Port 
    /// @return string
    string GetGripperCommPort();

    /// @brief Get Rail Brake Comm Port
    /// @return string 
    string GetRailBrakeCommPort();

    /// @brief Get Cable Brake Comm Port
    /// @return string 
    string GetCableBrakeCommPort();

    /// @brief Cable motor scale
    /// @return Cable motor scale
    double GetCableMotorScale(int motorID);

    /// @brief Rail motor scale
    /// @return Rail motor scale
    double GetRailMotorScale();

    float GetEffVelLmt();
    void SavePosToFile(string filename);
    int GetCableMotorBrakeNum();

    
// Setter

// Traj
    bool RunCableTraj (vector<vector<double>> trajectory, bool showAtten=true);
    bool MoveToParaBlend(double dest[], int time, bool showAtten=true);
    bool MoveToParaBlend(double dest[], bool showAtten=true);
    bool MoveToLinear(double dest[], int time, bool showAtten=true, bool useEBrake=true);;
    void RaiseRailWithCableByLengthAbsulote(int railIndex, int cableIndex, float length);
};

#endif