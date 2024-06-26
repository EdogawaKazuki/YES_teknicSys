#define _USE_MATH_DEFINES

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <conio.h>
#include <Windows.h>
#include "CDPR.h"
// #include "ABBgetVar.h"
#include "Dependencies\sFoundation20\inc\pubSysCls.h"
#include "Dependencies\TcAdsDll\Include\TcAdsDef.h" // Define Ads Def-s before using API
#include "Dependencies\TcAdsDll\Include\TcAdsAPI.h"
// #include "Dependencies\DynamixelSDK-3.7.31\include\dynamixel_sdk\dynamixel_sdk.h"

using namespace std;
using namespace sFnd;

bool SetSerialParams(HANDLE hComm);
void SendGripperSerial(HANDLE hComm, unsigned char* Ard_char, bool readTwice = true);
int CheckMotorNetwork();
int ConnectRailMotors(AmsAddr *pAddr);
int RaiseRailTo(HANDLE hComm, CDPR &r, AmsAddr *pAddr, int id, double target);
int RunParaBlend(CDPR &r, double point[7], bool showAttention = false);
void RunBricksTraj(HANDLE hComm, HANDLE brakeComm, CDPR &r, AmsAddr *pAddr, unsigned char* Ard_char, int listOffset, bool showAttention = false, bool waitBtn = false);
void ReverseBricksTraj(CDPR &r, int listOffset, bool showAttention = false);
void RunTrajPoints(CDPR &r);
int RunExternPoints(CDPR &r);
void RecordMotorTrq(CDPR &r);
void SendMotorGrp(CDPR &r, bool IsTorque = false, bool IsLinearRail = false);
void TrjHome(CDPR &r);
bool ReadBricksFile(CDPR &r);
void MN_DECL AttentionDetected(const mnAttnReqReg &detected); // this is attention from teknic motors

vector<string> comHubPorts;
vector<INode*> nodeList; // create a list for each node
vector<unsigned long> hdlList; // create list for easy use of handlers, listed in string adsVarNames[]
vector<vector<double>> brickPos;
unsigned int portCount;
char attnStringBuf[512]; // Create a buffer to hold the attentionReg information    
const int TwinCat_NUM = 4; // No. of axes listed in TwinCat programme
double step = 0.01; // in meters, for manual control
float targetTorque = -4.1; //2.5 in %, -ve for tension, also need to UPDATE in switch case 't'!!!!!!!!!
const int MILLIS_TO_NEXT_FRAME = 20, UserInput_Sec_Timeout = 15, SleepTime = 21; //35 note the basic calculation time is abt 16ms; sleep-time in 24 hr
// double railOffset = 0.0; // linear rails offset
char limitType = 'C'; // A for home, B for limits, C for default
char quitType = 'r'; // q for emergency quit, f for finish traj, e for error msg received, r for resume/default
int bkTrjCount = 0, trjCount = 0; // log info for loop numbers and error occurs daily

int main()
{   
    // Create robot model
    CDPR robot; // Read model.json and create object
    if(!robot.IsGood()){ return -1; } // quit programme if object creation failed
    robot.PrintHome();
    targetTorque = robot.GetTargetTorque();
    
    // Local variables for COM port communication
    HANDLE hComm, nanoComm; // Handle to the Serial port, https://github.com/xanthium-enterprises/Serial-Programming-Win32API-C
    char ComPortName[] = "\\\\.\\COM26"; // Name of the arduino Serial port(May Change) to be opened,
    char NanoComPortName[] = "\\\\.\\COM23"; // Name of the arduino Serial port(May Change) to be opened,
    unsigned char Ard_char[8] = {'(','o',',',' ',' ',' ',' ',')'};

    //// Open serial port for Arduino with bluetooth
    hComm = CreateFile(ComPortName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hComm == INVALID_HANDLE_VALUE){ cout << "Error: " << ComPortName << " cannot be opened.\n"; }
    else { cout << ComPortName << " opened.\n"; }
    if (!SetSerialParams(hComm)) { return -1; }
    //// Open serial port for nano Arduino for 4 poles brakes
    nanoComm = CreateFile(NanoComPortName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (nanoComm == INVALID_HANDLE_VALUE){ cout << "Error: " << NanoComPortName << " cannot be opened.\n"; }
    else { cout << NanoComPortName << " opened.\n"; }
    if (!SetSerialParams(nanoComm)) { return -1; }
    
    //// Initialize linear rails through Twincat ADS
    AmsAddr       Addr;
    PAmsAddr      pAddr = &Addr;
    long nErr{0};
    double actPos[TwinCat_NUM]{};
    if(ConnectRailMotors(pAddr)){ cout << "Failed to connect linear rails. Exit programme.\n"; return -1; };
    
    //// Initiallize cable robot motor network
    SysManager* myMgr = SysManager::Instance();
    // Start the programme, scan motors in network
    try{
        if (CheckMotorNetwork() < 0){
            cout << "Motor network not available. Exit programme." << endl;
            return -1;
        }
    }
    catch(sFnd::mnErr& theErr) {    //This catch statement will intercept any error from the Class library
        printf("Port Failed to open, Check to ensure correct Port number and that ClearView is not using the Port\n");  
        printf("Caught error: addr=%d, err=0x%08x\nmsg=%s\n", theErr.TheAddr, theErr.ErrorCode, theErr.ErrorMsg);
        return -1;
    }
    // IPort &myPort = myMgr->Ports(2);
    robot.PoseToLength(robot.home, robot.offset); // save offset values according to home pose

    cout << "Motor network available. Pick from menu for the next action:\nt - Tighten cables with Torque mode\ny - Loose the cables\nh - Move to Home\n8 - Manually adjust cable lengths\nl - Linear rails motions\nu - Update current position from external file\nr - Reset Rotation to zero\ng - Gripper functions\nj - Read Json file and update model params\ni - Info: show menu\nn - Move on to Next step" << endl;
    /*
    "Motor network available. Pick from menu for the next action:\n
        t - Tighten cables with Torque mode\n
        y - Loose the cables\n
        h - Move to Home\n
        8 - Manually adjust cable lengths\n
        l - Linear rails motions\n
        u - Update current position from external file\n
        r - Reset Rotation to zero\n
        g - Gripper functions\n
        j - Read Json file and update model params\n
        i - Info: show menu\n
        n - Move on to Next step"
    */
    char cmd;
    do {
        try{
            do {
                bool allDone = false, stabilized = false, busyFlag[TwinCat_NUM]{}, homeFlag[TwinCat_NUM]{}; // default values for bool array are false
                cin >> cmd;
                switch (cmd){
                    case 'x':   // Testing grouds
                        cout << "In testing mode...\n";
                        while(cmd != 'b'){ 
                            int railID{0}; double targetH{0};
                            cout << "Rail ID (0-3): ";
                            cin >> railID;
                            cout << "Target height (0-2m): ";
                            cin >> targetH;
                            // RaiseRailTo(nanoComm, robot, pAddr, 0, 0.02);
                            if(RaiseRailTo(nanoComm, robot, pAddr, railID, targetH)){ cout << "Rail trajectory [" << railID << "] aborted\n"; }
                            cout << "Continue c? Back to main menu b?\n";
                            cin >> cmd;
                        }
                        cout << "Exited testing mode.\n";
                        break;
                    case 'i':   // Show menu
                        cout << "Pick from menu for the next action:\nt - Tighten cables with Torque mode\ny - Loose the cables\nh - Move to Home\n8 - Manually adjust cable lengths\nl - Linear rails motions\nu - Update current position from external file\nr - Reset Rotation to zero\ng - Gripper functions\nj - Read Json file and update model params\ni - Info: show menu\nn - Move on to Next step\n";
                        break;
                    case 'y':   // Loosen cables using positive torque
                        targetTorque = 1;
                    case 't':   // Tighten cables according to torque **Only for 8 motors
                        cout << "Current target torque = " << targetTorque << endl;
                        // set the acclimit to a small value for tightening the torque
                        for(INode* n : nodeList){ n->Motion.AccLimit = 200; }
                        while(!stabilized) {
                            SendMotorGrp(robot, true);
                            Sleep(50); // Wait for it 
                            for (int n = 0; n < robot.GetNodeNum(); n++){ // check if all motor are stabilized
                                if(nodeList[n]->Motion.TrqCommanded.Value() > targetTorque) { break; }
                                stabilized = true;
                            }
                        } 
                        for(INode* n : nodeList){
                            n->Motion.TrqCommanded.Refresh();
                            cout << n->Motion.TrqCommanded.Value() << "\t";
                            // stop the motor
                            n->Motion.MoveVelStart(0);
                            // resume the normal acclimit
                            n->Motion.AccLimit = 40000;
                        }
                        cout << "\nTorque mode tightening completed" << endl;
                        targetTorque = robot.GetTargetTorque();
                        break;
                    case 'h':   // Homing for all motors!! Including linear rail
                        allDone = false;
                        for (int n = 0; n<nodeList.size(); n++) { 
                            nodeList[n]->Motion.MoveWentDone(); //Clear the rising edge Move done register
                            nodeList[n]->Motion.MovePosnStart(0, true); // absolute position
                        }
                        while(!allDone) {
                            for (INode* n : nodeList) {
                                if(!n->Motion.MoveIsDone()) { break; }
                                allDone = true;
                            }
                        }
                        copy(begin(robot.home), end(robot.home), begin(robot.in)); // copy home array into input array
                        cout << "Homing completed" << endl;
                        break;
                    case '8':   // Manual cable adjustment
                        cout << "0 to 7 - motor id to adjust cable length\n8 - move 4 linear rails together\na or d - increase or decrease cable length\ni - Print current position\nh - Move motor to home position.\nb - Back to previous menu\n";
                        /*
                        0 to 7 - motor id to adjust cable length
                        8 - move 4 linear rails together
                        a or d - increase or decrease cable length
                        b - Back to previous menu";
                        i - Print current position.
                        h - Move motor to home position.
                        */
                        while(cmd != 'b'){
                            cin >> cmd;
                            if('0' <= cmd && cmd <= robot.GetNodeNum() + '0'){ // verify input
                                int id = cmd - '0' + 1; // get motor id
                                int sCount = robot.ToMotorCmd(-1, step) / 5 ;// = ToMotorCmd(robot, -1, step) / 5;
                                cout << "Motor "<< cmd <<" selected.\n";
                                do{
                                    cmd = getch();
                                    switch(cmd){
                                        case 'a': // increase
                                            if(id == robot.GetNodeNum()){ for(int n = robot.GetNodeNum(); n<robot.GetNodeNum()+4; n++){ nodeList[n]->Motion.MovePosnStart(sCount); }}
                                            else { nodeList[id]->Motion.MovePosnStart(sCount); }
                                            break;
                                        case 'd': // decrease
                                            if(id == robot.GetNodeNum()){ for(int n = robot.GetNodeNum(); n<robot.GetNodeNum()+4; n++){ nodeList[n]->Motion.MovePosnStart(-sCount); }}
                                            else { nodeList[id]->Motion.MovePosnStart(-sCount); }
                                            break;
                                        case 'i': // print current status
                                            if(id == robot.GetNodeNum()){
                                                for(int n = robot.GetNodeNum(); n<robot.GetNodeNum()+4; n++){
                                                    nodeList[n]->Motion.PosnMeasured.Refresh();
                                                    cout << (double) nodeList[n]->Motion.PosnMeasured << endl;
                                                }
                                                cout << endl;
                                            }
                                            else{
                                                nodeList[id]->Motion.PosnMeasured.Refresh();
                                                cout << (double) nodeList[id]->Motion.PosnMeasured << endl;
                                            }
                                            break;
                                        case 'h':
                                            if(id == robot.GetNodeNum()){ cout << "Homing for linear rails are not implemented here.\n"; break; }
                                            else { 
                                                nodeList[id]->Motion.VelLimit = 300;
                                                nodeList[id]->Motion.MoveWentDone();
                                                nodeList[id]->Motion.MovePosnStart(0, true);
                                                while(!nodeList[id]->Motion.MoveIsDone()){} // wait for moving finish // caution: forever loop!!!
                                                nodeList[id]->Motion.VelLimit = 3000;
                                                cout << "Individual homing completed.\n";
                                                break;
                                            }
                                    }                        
                                    //Sleep(100); // do we need this? nope
                                }while(cmd =='a'|| cmd =='d' || cmd =='h' || cmd =='i');
                                cout << "Motor "<< id <<" deselected.\n";
                            }
                        }
                        cout << "Manual adjustment terminated" << endl;
                        break;
                    case 'r': // rotate robot
                        cout << "Attenion: robot will rotate to 0,0,0...\n";
                        {
                            double point[7] = {0,0,0,0,0,0,3500}; // Default 3500 ms duaration to rotate to 0
                            copy(robot.in, robot.in+3, begin(point)); // copy x,y,z position
                            cout << "Goal coordinates: " << point[0] << ", " << point[1] << ", " << point[2] << ", " << point[3] << ", " << point[4] << ", " << point[5] << ", " << point[6] << endl;
                            if(RunParaBlend(robot, point) < 0) { cout << "Trajectory aborted.\n"; break; }
                            cout << "Rotation reset completed.\n";      
                        }
                        break;
                    case 'g': // test gripper
                        cout << "In gripper testing mode:\ni - close gripper\no = open gripper\np - rotate gripper\nb - return to previous menu\n";
                        /*
                            In gripper testing mode:
                            i - close gripper
                            o = open gripper
                            p - rotate gripper
                            b - return to previous menu
                        */
                        while(cmd != 'b'){
                            cmd = getch();
                            switch(cmd){
                                case 'i':
                                    Ard_char[1] = 'c';
                                    SendGripperSerial(hComm, Ard_char);
                                    break;
                                case 'o':
                                    Ard_char[1] = 'o';
                                    SendGripperSerial(hComm, Ard_char);
                                    break;
                                case 'p':
                                    Ard_char[1] = 'd';
                                    cout << "Rotation: ";
                                    cin >> attnStringBuf; //read rotate angle
                                    copy(begin(attnStringBuf), begin(attnStringBuf)+4, begin(Ard_char)+3); // Write the first 4 char into the serial array
                                    SendGripperSerial(hComm, Ard_char);
                                    fill(begin(Ard_char)+3, begin(Ard_char)+7, ' '); // Reset the ending number with space char
                                    break;
                                case 'r': //??
                                    Ard_char[1] = 'r';
                                    SendGripperSerial(hComm, Ard_char);
                                    break;
                            }                        
                        }
                        cout << "Gripper testing terminated.\n";
                        break;
                    case 'j': // update model
                        robot.UpdateModel();
                        break;
                    // case 'l':   // Linear rail motions
                    //     cout << "0 to 3 - home linear rail individually\n4 - home all 4 linear rails automatically\nb - Back to previous menu\nany other keys - stop the linear rail from current motion\n";
                    //     while(cmd != 'b'){
                    //         cin >> cmd;
                    //         if('/' < cmd && cmd < 52){ // homing individually
                    //             int id = cmd - 48;
                    //             cout << "Homing linear rail #"<< cmd <<".\n";
                    //             HomeLinearRail(id);
                    //         }
                    //         else if(cmd == 52){ // homing 4 all tgt
                    //             for (int i = 0; i < 4 ; i++){
                    //                 HomeLinearRail(i);
                    //             }
                    //             cout << "All linear rails are homed.\n";
                    //         }
                    //     }
                    //     cout << "Linear rail homing terminated\n";
                    //     break; 
                    case 'u':   // Update in1[] and offset[] from csv file // given robot position, calcuate the cable length, then update the motor count.
                        ifstream file ("lastPos.txt"); //"lastPos.txt" or "currentPos.csv"
                        string temp;
                        int count = 0;
                        if(file.is_open()){
                            try{
                                while (file >> temp){
                                    if(count < 6){ robot.in[count] = stod(temp); } // convert string to double stod() for the first 6 inputs
                                    else if(count < 10){ robot.railOffset[count-6] = stod(temp); } // reading the rail offset, then break while loop
                                    else { break; }
                                    count++;
                                }
                                cout << "Completed reading from external file" << endl; //"Completed updating from external pose file"
                            }
                            catch(int e){ cout << "Check if currentPos.csv matches the in1 input no." << endl; }

                            robot.PoseToLength(robot.in, robot.out, robot.railOffset); //pose_to_length(in1, out1, railOffset);
                            for (int n = 0; n < nodeList.size() + robot.GetRailNum(); n++){
                                double step = robot.ToMotorCmd(n, robot.out[n]);
                                if (n < nodeList.size()){ // for teknic motors
                                    nodeList[n]->Motion.PosnMeasured.Refresh();
                                    nodeList[n]->Motion.AddToPosition(-nodeList[n]->Motion.PosnMeasured.Value() + step);
                                }
                                else { // for twincat motors
                                    nErr = AdsSyncWriteReq(pAddr,ADSIGRP_SYM_VALBYHND,hdlList[0], sizeof(step), &step); // write "MAIN.Axis1_GoalPos"
                                    if (nErr) { cout << "Error: Rail[" << n-nodeList.size() << "] AdsSyncWriteReq: " << nErr << '\n'; }
                                    homeFlag[n-nodeList.size()] = true; // signal targeted rail motor for homing
                                    nErr = AdsSyncWriteReq(pAddr,ADSIGRP_SYM_VALBYHND,hdlList[3], sizeof(homeFlag), &homeFlag[0]); // write "MAIN.bHomeSwitch"
                                    if (nErr) { cout << "Error: Rail[" << n-nodeList.size() << "] Set postiton Command. AdsSyncWriteReq: " << nErr << '\n'; }
                                    homeFlag[n-nodeList.size()] = false; // return to false
                                    busyFlag[n-nodeList.size()] = false;
                                    while(!busyFlag[n-nodeList.size()]){ // wait for motor busy flag on, ie. update current pos started
                                        nErr = AdsSyncReadReq(pAddr, ADSIGRP_SYM_VALBYHND, hdlList[4], sizeof(busyFlag), &busyFlag[0]); // read "MAIN.Axis_Home.Busy"
                                        if (nErr) { cout << "Error: Rail[" << n-nodeList.size() << "] AdsSyncReadReq: " << nErr << '\n'; break; }
                                    }
                                    while(busyFlag[n-nodeList.size()]){ // wait for motor busy flag off, ie. completed updated position
                                        nErr = AdsSyncReadReq(pAddr, ADSIGRP_SYM_VALBYHND, hdlList[4], sizeof(busyFlag), &busyFlag[0]);
                                        if (nErr) { cout << "Error: Rail[" << n-nodeList.size() << "] AdsSyncReadReq: " << nErr << '\n'; break; }
                                    }
                                }
                            }
                            cout << "Updating motor counts completed" << endl;
                            cout << "Current coordinates: "; robot.PrintIn();
                            cout << "Linear rail offset: "; robot.PrintRail();
                            cout << "Motor internal counts: ";
                            // for teknic motors
                            for (int id = 0; id < nodeList.size(); id++){
                                nodeList[id]->Motion.PosnMeasured.Refresh();
                                cout << (double) nodeList[id]->Motion.PosnMeasured << "\t";
                            }
                            // for twincat motors
                            nErr = AdsSyncReadReq(pAddr, ADSIGRP_SYM_VALBYHND, hdlList[2], sizeof(actPos), &actPos[0]); // read "MAIN.actPos"
                            if (nErr) { cout << "Error: Rail[s] AdsSyncReadReq: " << nErr << '\n'; }
                            for (int id = 0; id < robot.GetRailNum(); id++){ cout << actPos[id] << "\t"; }
                            cout << endl;
                        }
                        break;
                }
            } while(cmd != 'n');
        }
        catch(sFnd::mnErr& theErr) {    //This catch statement will intercept any error from the Class library
            printf("ERROR: Motor command failed.\n");  
            printf("Caught error: addr=%d, err=0x%08x\nmsg=%s\n", theErr.TheAddr, theErr.ErrorCode, theErr.ErrorMsg);
            return -3;
        }
        
        // Set linear rail brakes as usually enabled
        // myMgr->Ports(2).BrakeControl.BrakeSetting(0, BRAKE_PREVENT_MOTION);

        cout << "Choose from menu for cable robot motion:\nt - Read from \"bricks.csv\" file for brick positions\nm - Manual input using w,a,s,d,r,f,v,g\np - Run Point-to-point trajectory\ne - Run External trajectory step points\nq - Request current torQue readings\ni - Info: show menu\nn - Prepare to disable motors and exit programme\nr - Return to previous section" << endl;
        /*
            Choose from menu for cable robot motion:
                t - Read from \"bricks.csv\" file for brick positions
                m - Manual input using w,a,s,d,r,f,v,g
                p - Run Point-to-point trajectory
                e - Run External trajectory step points
                q - Request current torQue readings
                i - Info: show menu
                nn - Prepare to disable motors and exit programme
                r - Return to previous section;
        */
        do {
            bool waitBtn = false;
            cin >> cmd;
            switch (cmd){
                case 'i':    // Show menu
                    cout << "Choose from menu for cable robot motion:\nt - Read from \"bricks.csv\" file for brick positions\nm - Manual input using w,a,s,d,r,f,v,g\np - Run Point-to-point trajectory\ne - Run External trajectory step points\nq - Request current torQue readings\ni - Info: show menu\nn - Prepare to disable motors and exit programme\nr - Return to previous section" << endl;
                    break;
                case 't':   // Read brick file, plan trajectory
                case 'T':
                    if(!ReadBricksFile(robot)){ continue; } // Read "bricks.csv"
                    RunBricksTraj(hComm, nanoComm, robot, pAddr, Ard_char, 0, true);
                    bkTrjCount += 1; // Add counter for Brick traj-s
                    break;
                case 'm':   // Manual wasdrf
                case 'M':
                    cout << "Press 'q' to quit manual input anytime.\n'h' for Homing.\n'x' to adjust increment step size.\n";
                    // nodeList[8]->Port.BrakeControl.BrakeSetting(0, BRAKE_ALLOW_MOTION); // disable enable brake
                    while(cmd != 'q' && cmd != 'Q'){
                        cmd = getch();
                        switch(cmd){
                            case 'I':
                            case 'i':
                                Ard_char[1] = 'c';
                                SendGripperSerial(hComm, Ard_char);
                                continue;
                            case 'O':
                            case 'o':
                                Ard_char[1] = 'o';
                                SendGripperSerial(hComm, Ard_char);
                                continue;
                            case 'P':
                            case 'p':
                                Ard_char[1] = 'd';
                                cout << "Rotation: ";
                                cin >> attnStringBuf;
                                copy(begin(attnStringBuf), begin(attnStringBuf)+4, begin(Ard_char)+3); // Write the first 4 char into the serial array
                                SendGripperSerial(hComm, Ard_char);
                                fill(begin(Ard_char)+3, begin(Ard_char)+7, ' '); // Reset the ending number with space char
                                continue;
                            case 'W':
                            case 'w':
                                robot.in[1] += step;
                                break;
                            case 'S':
                            case 's':
                                robot.in[1] -= step;
                                break;
                            case 'A':
                            case 'a':
                                robot.in[0] -= step;
                                break;
                            case 'D':
                            case 'd':
                                robot.in[0] += step;
                                break;
                            case 'R':
                            case 'r':
                                robot.in[2] += step;
                                break;
                            case 'F':
                            case 'f':
                                robot.in[2] -= step;
                                break;
                            case 'H':
                            case 'h':
                                cout << "Homing...\n"; 
                                TrjHome(robot);
                                break;
                            case 'X':
                            case 'x':
                                cout << "Current step size: " << step << "m. Please enter new step size: ";
                                cin >> step;
                                if (!cin.good()){ cout << "Invalid input!"; }
                                else if (abs(step) > 0.1){ cout << "Warning! Step size is too large, may cause vigorious motions.";}
                                else { cout << "New step size: " << step << endl; break; }
                                cout << " Step size is now set to 0.01m.\n";
                                step = 0.01;
                                break;
                            case 'C':
                            case 'c':
                                cout << "Are you sure to clear warnings from triggered linear rail limit switch? Type \"CLEAR\" to confirm.\n";
                                {
                                    string reply;
                                    cin >> reply;
                                    if (reply == "CLEAR"){ limitType = 'C'; cout << "Warning dismissed.\n"; }
                                    else { cout << "Warning is not cleared.\n"; }
                                }
                                continue;
                        }
                        robot.PrintIn();
                        if(robot.CheckLimits()){
                            robot.PoseToLength(robot.in, robot.out, robot.railOffset);
                            robot.PrintOut();
                            SendMotorGrp(robot);
                            
                            Sleep(step*1000);
                        }
                        else{
                            cout << "WARNING: Intended position out of bound!\n";
                            switch(cmd){
                                case 'W':
                                case 'w':
                                    robot.in[1] -= step;
                                    break;
                                case 'S':
                                case 's':
                                    robot.in[1] += step;
                                    break;
                                case 'A':
                                case 'a':
                                    robot.in[0] += step;
                                    break;
                                case 'D':
                                case 'd':
                                    robot.in[0] -= step;
                                    break;
                                case 'R':
                                case 'r':
                                    robot.in[2] -= step;
                                    break;
                                case 'F':
                                case 'f':
                                    robot.in[2] += step;
                                    break;
                            }
                        }
                    }
                    // nodeList[8]->Port.BrakeControl.BrakeSetting(0, BRAKE_PREVENT_MOTION); // enable brake afterwards
                    cout << "Quit manual control\n";
                    break;
                case 'e':   // Use external trajectory step points
                case 'E':
                    if(RunExternPoints(robot)){ cout << "Step point trajectory aborted\n"; }
                    break;
                case 'p':   // Typical point to point trajectory
                case 'P':
                    RunTrajPoints(robot);
                    trjCount += 1; // Add counter for point-to-point traj-s
                    break;
                case 'q':   // Request current torque readings
                case 'Q':
                    for(int i = 0; i < nodeList.size(); i++){                    
                        nodeList[i]->Motion.TrqMeasured.Refresh();
                        cout << "[" << i << "]: " << nodeList[i]->Motion.TrqMeasured.Value() << endl;
                    }
                    RecordMotorTrq(robot);
                    break;
            }
        } while(cmd != 'n' && cmd != 'r');
    } while(cmd != 'n' && quitType != 'e');

    //// Safe system shut down, safe last pos and emegency shut down
    // Saving last position before quiting programme
    cout << "Saving last position...\n";
    ofstream myfile;
    myfile.open ("lastPos.txt");
    myfile << robot.in[0] << " " << robot.in[1] << " " << robot.in[2] << " " << robot.in[3] << " " << robot.in[4] << " " << robot.in[5] << endl;
    myfile << robot.railOffset[0] << " " << robot.railOffset[1] << " " << robot.railOffset[2] << " " << robot.railOffset[3] << endl;
    for(INode* n : nodeList){
        n->Motion.PosnMeasured.Refresh();
        myfile << n->Motion.PosnMeasured.Value() << " ";
    }
    AdsSyncReadReq(pAddr, ADSIGRP_SYM_VALBYHND, hdlList[2], sizeof(actPos), &actPos[0]); // read "MAIN.actPos"
    for(int i = 0; i < robot.GetRailNum(); i++){ myfile << actPos[i] << " "; }
    myfile.close();
    tm *fn; time_t now = time(0); fn = localtime(&now);
    myfile.open("log.txt", ios::app);
    myfile <<  fn->tm_mon +1 << "/" << fn->tm_mday << ": " << bkTrjCount << " brick built runs; " << trjCount << " traj runs." <<endl;
    myfile.close();
    
    //// List of what-if-s???
    
    {   // Send 'f' signal to bluetooth gripper for shutting down
        Ard_char[1] = 'f';
        SendGripperSerial(hComm, Ard_char);
        // Send "(4:1)" to brake all rails        
        Ard_char[1] = '4'; Ard_char[2] = ':'; Ard_char[3] = '1'; Ard_char[4] = ')'; Ard_char[7] = ' ';
        SendGripperSerial(nanoComm, Ard_char, false); // close brake after motion
        //Close the Serial Port-s
        CloseHandle(hComm);
        CloseHandle(nanoComm);
    }

    {   // Close ADS communication port, disable power for motors
        long lHdlVar; bool FALSE_FLAG = false;
        nErr = AdsSyncReadWriteReq(pAddr, ADSIGRP_SYM_HNDBYNAME, 0x0, sizeof(lHdlVar), &lHdlVar, sizeof("MAIN.power"), "MAIN.power");
        if (nErr){ cout << "Error: AdsSyncReadWriteReq: " << nErr << '\n'; return nErr; }
        nErr = AdsSyncWriteReq(pAddr,ADSIGRP_SYM_VALBYHND,lHdlVar, sizeof(FALSE_FLAG), &FALSE_FLAG);
        if (nErr){ cout << "Error: AdsSyncWriteReq: " << nErr << '\n'; return nErr; }
        else { cout << "Linear rail motors disabled.\n"; }
        nErr = AdsSyncWriteReq(pAddr,ADSIGRP_SYM_RELEASEHND,0,sizeof(lHdlVar),&lHdlVar);
        if ( AdsPortClose() ){ cout << "Error: AdsPortClose: " << nErr << '\n'; }
    }

    for(int i = 0; i < nodeList.size(); i++){ //Disable Nodes
        nodeList[i]->EnableReq(false);
    }
    nodeList.clear();
    myMgr->PortsClose(); // Close down the ports
    cout << "Teknic motors are disabled\n";
    return 1;
}

// set communication port
bool SetSerialParams(HANDLE hComm){
    // Set parameters for serial port
    DCB dcbSerialParams = { 0 }; // Initializing DCB structure
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    bool Status = GetCommState(hComm, &dcbSerialParams); // retreives  the current settings
    if (Status == false){ cout << "Error in GetCommState()\n"; return false; }

    dcbSerialParams.BaudRate = CBR_57600;// Setting BaudRate
    dcbSerialParams.ByteSize = 8;         // Setting ByteSize = 8
    dcbSerialParams.StopBits = ONESTOPBIT;// Setting StopBits = 1
    dcbSerialParams.Parity   = NOPARITY;  // Setting Parity = None

    SetCommState(hComm, &dcbSerialParams);
    if (Status == false){ cout << "Error! in Setting DCB Structure\n"; return false; }
    else{
        printf("   Setting DCB Structure Successful\n");
        printf("       Baudrate = %d\n", dcbSerialParams.BaudRate);
        printf("       ByteSize = %d\n", dcbSerialParams.ByteSize);
        printf("       StopBits = %d\n", dcbSerialParams.StopBits);
        printf("       Parity   = %d\n", dcbSerialParams.Parity);
        cout << endl;
    }

    // Set timeouts
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout         = 50;
    timeouts.ReadTotalTimeoutConstant    = 50;
    timeouts.ReadTotalTimeoutMultiplier  = 10;
    timeouts.WriteTotalTimeoutConstant   = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;

    if (SetCommTimeouts(hComm, &timeouts) == FALSE){ cout << "Error! in Setting Time Outs\n"; return false; }
    // Set recieve mask                
    if (!(bool)SetCommMask(hComm, EV_RXCHAR)){ cout << "Error! in Setting CommMask\n"; }
    
    return true;
}

// receive data from gripper
void ReadGripperSerial(HANDLE hComm){
    DWORD dwEventMask, BytesRead;
    int i{0};
    char tmp, msg[256];

    bool Status = WaitCommEvent(hComm, &dwEventMask, NULL); // wait till brick is ready from ABB
    if (Status == false){ cout << "Error in setting WaitCommEvent()\n";} //quitType = 'q'; return; }
    else {
        do{
            ReadFile(hComm, &tmp, sizeof(tmp), &BytesRead, NULL);
            msg[i] = tmp;
            i++;
        }while (BytesRead>0);        
    }
    
    for(int j=0; j< i-1; j++){
        cout << msg[j];
    } 
}

// send command to gripper
void SendGripperSerial(HANDLE hComm, unsigned char* Ard_char, bool readTwice){
    DWORD dNoOfBytesWritten = 0;
    if (!(bool)WriteFile(hComm, Ard_char, 8, &dNoOfBytesWritten, NULL)){ cout << "Arduino writing error: " << GetLastError() << endl; }    
    ReadGripperSerial(hComm);
    if (readTwice){ ReadGripperSerial(hComm); }// Repeat to make sure the feedback is received
}

// init motors
int CheckMotorNetwork() {
    SysManager* myMgr = SysManager::Instance();

    sFnd::SysManager::FindComHubPorts(comHubPorts);

    cout << "Found " <<comHubPorts.size() << " SC Hubs\n";
    for (portCount = 0; portCount < comHubPorts.size(); portCount++) {
        myMgr->ComHubPort(portCount, comHubPorts[portCount].c_str());
    }
    if (portCount < 0) {
        cout << "Unable to locate SC hub port\n";
        return -1;
    }
    //if(portCount==0) { return -1; } // do we need this?
    
    myMgr->PortsOpen(portCount);
    for (int i = 0; i < portCount; i++) { // check no. of nodes in each ports
        IPort &myPort = myMgr->Ports(i);
        // myPort.BrakeControl.BrakeSetting(0, BRAKE_AUTOCONTROL); // do we need this?
        // myPort.BrakeControl.BrakeSetting(1, BRAKE_AUTOCONTROL);
        printf(" Port[%d]: state=%d, nodes=%d\n", myPort.NetNumber(), myPort.OpenState(), myPort.NodeCount());

        // Init nodes
        for (int iNode = 0; iNode < myPort.NodeCount(); iNode++) {
            INode &theNode = myPort.Nodes(iNode);
            theNode.EnableReq(false); //Ensure Node is disabled before loading config file
            myMgr->Delay(200);

            printf("   Node[%d]: type=%d\n", int(iNode), theNode.Info.NodeType());
            printf("            userID: %s\n", theNode.Info.UserID.Value());
            printf("        FW version: %s\n", theNode.Info.FirmwareVersion.Value());
            printf("          Serial #: %d\n", theNode.Info.SerialNumber.Value());
            printf("             Model: %s\n", theNode.Info.Model.Value());

            theNode.Status.AlertsClear();               //Clear Alerts on node 
            theNode.Motion.NodeStopClear();             //Clear Nodestops on Node               
            theNode.EnableReq(true);                    //Enable node 
            theNode.Motion.PosnMeasured.AutoRefresh(true);
            theNode.Motion.TrqMeasured.AutoRefresh(true);
            printf("Node %d enabled. ", iNode);

            theNode.AccUnit(INode::RPM_PER_SEC);        //Set the units for Acceleration to RPM/SEC
            theNode.VelUnit(INode::RPM);                //Set the units for Velocity to RPM
            theNode.Motion.AccLimit = 40000;           //100000 Set Acceleration Limit (RPM/Sec)
            theNode.Motion.NodeStopDecelLim = 5000;
            theNode.Motion.VelLimit = 3000;             //700 Set Velocity Limit (RPM)
            theNode.Info.Ex.Parameter(98, 1);           //enable interrupting move
            theNode.Motion.Adv.TriggerGroup(1);         //Set all nodes trigger group num. as 1
            cout << "AccLimit and VelLimit set." << endl;

            nodeList.push_back(&theNode);               // add node to list

            double timeout = myMgr->TimeStampMsec() + 2000; //TIME_TILL_TIMEOUT; //define a timeout in case the node is unable to enable
            //This will loop checking on the Real time values of the node's Ready status
            while (!theNode.Motion.IsReady()) {
                if (myMgr->TimeStampMsec() > timeout) {
                    printf("Error: Timed out waiting for Node %d to enable\n", iNode);
                    return -2;
                }
            }
        }
    }
    return 0;
}

// connect to rail motors
int ConnectRailMotors(AmsAddr *pAddr){
    long nErr, nPort;
    unsigned long lHdlVar;
    bool TRUE_FLAG = true;

    // Open communication port on the ADS router
    nPort = AdsPortOpen();
    nErr = AdsGetLocalAddress(pAddr);
    if (nErr){ cout << "Error: AdsGetLocalAddress: " << nErr << '\n'; return nErr; }
    pAddr->port = 851; // Port number for Twincat 3

    // Enable power for motors, start state machine
    // Get handle of the request
    nErr = AdsSyncReadWriteReq(pAddr, ADSIGRP_SYM_HNDBYNAME, 0x0, sizeof(lHdlVar), &lHdlVar, sizeof("MAIN.power"), "MAIN.power");
    if (nErr){ cout << "Error: AdsSyncReadWriteReq: " << nErr << '\n'; return nErr; }
    // Use the handle to write data
    nErr = AdsSyncWriteReq(pAddr,ADSIGRP_SYM_VALBYHND,lHdlVar, sizeof(TRUE_FLAG), &TRUE_FLAG);
    if (nErr){ cout << "Error: AdsSyncWriteReq: " << nErr << '\n'; return nErr; }
    else { cout << "Linear rail motors enabled.\n"; }
    // Release the handle
    nErr = AdsSyncWriteReq(pAddr,ADSIGRP_SYM_RELEASEHND,0,sizeof(lHdlVar),&lHdlVar);

    // Create list of handler by name
    string adsVarNames[] = {"MAIN.Axis_GoalPos", "MAIN.startMove", "MAIN.actPos", "MAIN.bHomeSwitch", "MAIN.homeBusy"}; // data type: double, bool[], double[], bool[], bool[].
    for(int i=0; i < *(&adsVarNames+1)-adsVarNames; i++){
        nErr = AdsSyncReadWriteReq(pAddr, ADSIGRP_SYM_HNDBYNAME, 0x0, sizeof(lHdlVar), &lHdlVar, adsVarNames[i].length(), &adsVarNames[i][0]);
        if (nErr){ cout << "Error: AdsSyncReadWriteReq: " << nErr << ", in creating handler for " << adsVarNames[i] << "\n"; return nErr; }
        hdlList.push_back(lHdlVar);
    }
    cout << "Completed creating handler list of " << hdlList.size() << endl;
    return 0;
}

// move rail
int RaiseRailTo(HANDLE hComm, CDPR &r, AmsAddr *pAddr, int id, double target){ // id:[0-3] !!! Define velocity limit !!!
    unsigned char Ard_char[8] = {'(','0',':','0',')',' ',' ',' '};
    long nErr;
    double step{}, posNow[TwinCat_NUM]{};
    bool bArry[TwinCat_NUM]{};
    bArry[id] = true; // signal target rail to move

    cout << "RAil[" << id << "] target: " << target << endl;
    if(id > r.GetRailNum()-1){ cout << "WARNING! Intended rail does not exist!\n"; return -2; }
    if (target < 0 || target > 2.55) { cout << "WARNING! Intended rail offset is out of bound!\n"; return -2; }
    Ard_char[1] = '0' + id;
    SendGripperSerial(hComm, Ard_char, false); // open brake before motion

    double velLmt = 0.01; // meters per sec // measured vel 0.025 ms-1
    double dura = abs(target - r.railOffset[id])/velLmt*1000;// *1000 to change unit to ms
    if(dura <= 200){ return 0; } // Don't run traj for incorrect timing 
    static double a, b, c; // coefficients for cubic spline trajectory
    cout << "\nATTENTION: Raise rail function is called. Estimated time: " << dura << "\n";
    
    // Solve coefficients of equations for cubic
    a = r.railOffset[id];
    b = 3 / (dura * dura) * (target - r.railOffset[id]); 
    c = -2 / (dura * dura * dura) * (target - r.railOffset[id]);
    
    // Run the trajectory till the given time is up
    double t = 0;
    auto start = chrono::steady_clock::now();
    long dur = 0;
    while (t <= dura){        
        // CUBIC equation, per time step pose
        r.railOffset[id] = a + b * t * t + c * t * t * t; // update new rail offset

        // get absolute cable lengths and rail positions in meters
        r.PoseToLength(r.in, r.out, r.railOffset);
        //r.PrintOut(true);

        // Move individual rail and motor
        step = r.ToMotorCmd(id, r.out[id]);
        nodeList[id]->Motion.MovePosnStart(step, true); // absolute position

        step = r.ToMotorCmd(id+r.GetNodeNum(), r.out[id+r.GetNodeNum()]);
        nErr = AdsSyncWriteReq(pAddr,ADSIGRP_SYM_VALBYHND,hdlList[0], sizeof(step), &step); // write "MAIN.Axis_GoalPos"
        if (nErr) { cout << "Error: Rail[" << id << "] AdsSyncWriteReq: " << nErr << '\n'; }
        nErr = AdsSyncWriteReq(pAddr,ADSIGRP_SYM_VALBYHND,hdlList[1], sizeof(bArry), &bArry[0]); // write "MAIN.startMove"
        if (nErr) { cout << "Error: Rail[" << id << "] Start Absolute Move Command. AdsSyncWriteReq: " << nErr << '\n'; }

        // Write to traking file
        ofstream myfile;
        myfile.open ("traking.txt");
        myfile << r.in[0] << " " << r.in[1] << " " << r.in[2] << " " << r.in[3] << " " << r.in[4] << " " << r.in[5] << endl;
        myfile << r.railOffset[0] << " " << r.railOffset[1] << " " << r.railOffset[2] << " " << r.railOffset[3] << endl;
        myfile.close();

        dur = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now()-start).count();
        double dif = MILLIS_TO_NEXT_FRAME - dur - 1;
        if(dif > 0) { Sleep(dif);}
        t += MILLIS_TO_NEXT_FRAME;
        start = chrono::steady_clock::now(); // refresh the ending time in loop

        if(kbhit()){ // Emergency quit during trajectory control
            cout << "\nSystem interrupted!! Do you want to quit the trajectory control?\nq - Quit trajectory\nf - Finish this loop of motion\nr - Resume trajectory\n";
            cin >> quitType;
            if(quitType == 'q' || quitType == 'Q' || quitType == 'e'){
                cout << "Trajectory emergency quit\n";
                Ard_char[1] = '4'; Ard_char[3] = '1';
                SendGripperSerial(hComm, Ard_char, true); // close all brake
                return -1;
            }
        }
        if(limitType != 'C'){
            cout << "WARNING! Linear rail limits triggered. Please quit the programme and check the system.\n";
            return -3;
        }
    }
    // Wait for rail motor to reach intented position before enabling brake again
    while(posNow[id]>step+2 || posNow[id]<step-2){
        nErr = AdsSyncReadReq(pAddr, ADSIGRP_SYM_VALBYHND, hdlList[2], sizeof(posNow), &posNow[0]); // read "MAIN.AxisList[id].NcToPlc.ActPos"
        if (nErr) { cout << "Error: Rail[" << id << "] AdsSyncReadReq: " << nErr << '\n'; break; }
        // cout << posNow[id] << endl;
    }
    Sleep(50);
    cout << "Rail traj done .... \n";

    Ard_char[3] = '1';
    SendGripperSerial(hComm, Ard_char, false); // close brake after motion
    return 0;
}

// create blend traj
int RunParaBlend(CDPR &r, double point[7], bool showAttention){
    float vMax[6] = {.6, .6, .6, 0.8, 0.8, 0.8}; // m/s, define the maximum velocity for each DoF
    float aMax[6] = {80, 80, 80, 10, 10, 10}; // m/s^2, define the maximum acceleration for each DoF
    static double a[6], b[6], c[6], d[6], e[6], f[6], g[6], tb[6]; // trajectory coefficients
    static double sQ[6], Q[6], o[6];
    static double dura;
    double unitV = sqrt(pow(point[0]-r.in[0],2)+pow(point[1]-r.in[1],2)+pow(point[2]-r.in[2],2)); // the root to divide by to get unit vector
    dura = point[6]; // Save the new dura for every new point
    cout << "Destination: " <<point[0]<<","<<point[1]<<","<<point[2]<<";"<<point[5]<< " Will run for " << dura << "ms...\n";
    if(dura <= 200){ return 0; } // Don't run traj for incorrect timing 

    // Solve parabolic blend coefficients for each DoF
    for(int i = 0; i < 6; i++){
        sQ[i] = r.in[i]; // start point, from current position
        Q[i] = point[i]; // end point, from goal position
        vMax[i] /= 1000; // change the velocity unit to meter per ms
        aMax[i] /= 1000000; // change the unit to meter per ms square
        tb[i] = i<3? dura-unitV/vMax[i] : dura-abs(Q[i]-sQ[i])/vMax[i];
        if(tb[i] < 0) {
            cout << "WARNING: Intended trajectory exceeds velocity limit in DoF "<< i << ".\n";
            return -1;
        }
        else if (tb[i] > dura / 2){
            if (showAttention){ cout << "ATTENTION: Trajectory for DoF " << i << " will be in cubic form.\n"; }
            tb[i] = dura / 2;
            vMax[i] = 2 * (Q[i] - sQ[i]) / dura;
        }
        else if(i<3){ vMax[i] = vMax[i] * (Q[i] - sQ[i]) / unitV; } // vMax in x,y,z accordingly
        else if(Q[i]<sQ[i]){ vMax[i] *= -1; } //Fix velocity direction for rotation
        o[i] = vMax[i] / 2 / tb[i]; // times 2 to get acceleration
        if(abs(o[i]*2) > aMax[i]){
            cout << "WARNING: Intended trajectory acceleration <" << abs(o[i]*2) << "> exceeds limit in DoF "<< i << ".\n";
            return -1;
        }
        
        // solve coefficients of equations for parabolic
        a[i] = sQ[i];
        b[i] = o[i];

        c[i] = sQ[i] - vMax[i] * tb[i] / 2;
        d[i] = vMax[i];

        e[i] = Q[i] - o[i] * dura * dura;
        f[i] = 2 * o[i] * dura;
        g[i] = -o[i];
    }
    
    auto start = chrono::steady_clock::now();
    long dur = 0;
    // Run the trajectory till the given time is up
    double t = 0;
    while (t <= point[6]){        
        // PARABOLIC BLEND equation, per time step pose
        for (int j = 0; j < 6; j++){
            if (t <= tb[j]){
                r.in[j] = a[j] + b[j] * t * t;
            }
            else if(t <= point[6]-tb[j]){
                r.in[j] = c[j] + d[j] * t;
            }
            else{
                r.in[j] = e[j] + f[j] * t + g[j] * t * t;
            }
        }
        // get absolute cable lengths in meters
        // cout << "IN: "<< in1[0] << " " << in1[1] << " " << in1[2] << " " << in1[3] << " " << in1[4] << " " << in1[5] << endl;
        r.PoseToLength(r.in, r.out, r.railOffset);
        // cout << "OUT: "<<  out1[0] << "\t" << out1[1] << "\t" << out1[2] << "\t" << out1[3] << "\t" <<  out1[4] << "\t" << out1[5] << "\t" << out1[6] << "\t" << out1[7] << endl;
        
        SendMotorGrp(r);
        if(quitType == 'e'){ // Motor error message?
            cout << "WARNING! Motor error message received. System will now shut now.\n";
            return -4;
        }

        // Write to traking file
        ofstream myfile;
        myfile.open ("traking.txt");
        myfile << r.in[0] << " " << r.in[1] << " " << r.in[2] << " " << r.in[3] << " " << r.in[4] << " " << r.in[5] << endl;
        myfile << r.railOffset[0] << " " << r.railOffset[1] << " " << r.railOffset[2] << " " << r.railOffset[3] << endl;
        myfile.close();

        auto end = chrono::steady_clock::now();
        dur = chrono::duration_cast<chrono::milliseconds>(end-start).count();
        
        double dif = MILLIS_TO_NEXT_FRAME - dur - 1;
        if(dif > 0) { Sleep(dif);}
        t += MILLIS_TO_NEXT_FRAME;

        if(kbhit()){ // Emergency quit during trajectory control
            cout << "\nSystem interrupted!! Do you want to quit the trajectory control?\nq - Quit trajectory\nf - Finish this loop of motion\nr - Resume trajectory\n";
            cin >> quitType;
            if(quitType == 'q' || quitType == 'Q'){
                cout << "Trajectory emergency quit\n";
                return -2;
            }
        }
        if(limitType != 'C'){
            cout << "WARNING! Linear rail limits triggered. Please quit the programme and check the system.\n";
            return -3;
        }
        start = end;
    }
    return 0;
}

// double ScaleRailLvl(double brickLvl){
//     double output = brickLvl * 0.8;
//     if (output > 1.2){ return 1.2; } // Upper limit of linear rail
//     else if (output < 0) { return 0; } // lower limit of linear rail
//     return output;
// }

//void RunBricksTraj(const dynamixel::GroupSyncRead &groupSyncRead, int listOffset, bool showAttention, bool waitBtn){
void RunBricksTraj(HANDLE hComm, HANDLE brakeComm, CDPR &r, AmsAddr *pAddr, unsigned char* Ard_char, int listOffset, bool showAttention, bool waitBtn){
    double brickPickUp[7] = {9.925, 6.709, -2.96625, 0, 0, 0.0148, 10}; // !!!! Define the brick pick up point !!!!, the last digit is a dummy number for time duration.
    double safePt[6] = {8.24, 6.51, -2.7, 0, 0, -0.0237}; // a safe area near to the arm // 0.21 safe height from ABB
    double goalPos[7] = {2, 2, 1, 0, 0, 0, 10}; // updated according to brick position
    double velLmt = 0.45; // meters per second
    double safeT = 1500; // in ms, time to raise to safety height
    double safeH = 0.12; // meter, safety height from building brick level
    double currentBrkLvl = r.railOffset[r.GetRailNum()-1]; // meter, check if the last rail offset is the same as target BrkLvl
    double lowPole = -2.9; // lowest 5th pole z-value
    double safeZoneH = 0.4; // height of safe zone from bottom cable to top cable near EE
    double dura = 0;
    unsigned char tmp, msg[256];
    DWORD dwEventMask, BytesRead, dNoOfBytesWritten = 0;
    bool Status;

    if(r.brickPickUp[0] != 0){ copy(r.brickPickUp, r.brickPickUp+6, begin(brickPickUp)); } // Check if robot has its own brick pick-up pose

    // Go through the given bricks
    for (int i = 0; i < brickPos.size(); i++) {
        ofstream myfile; int timeout_i = 0;

        // Check if rails need to be raised
        // if(brickPos[i][2] != currentBrkLvl){
        //     currentBrkLvl = brickPos[i][2];
        //     for(int id = 0; id < r.RailNum(); id++){
        //         if(RaiseRailTo(brakeComm, r, pAddr, id, currentBrkLvl) < 0) { cout << "Trajectory aborted.\n"; return; } // raise rail to the building brick level
        //     }
        // }

        // Wait for button input before builing a brick
        if(waitBtn && quitType != 'f'){
            cout << "...Waiting for button input...\n";
            myfile.open ("indexBrk.txt");
            myfile << -2; // attraction screen
            myfile.close();
            do{
                nodeList[0]->Status.RT.Refresh(); // Refresh again to check if button is pushed
                if(kbhit()){ // Emergency quit during trajectory control
                    cout << "\nSystem interrupted!! Do you want to quit the trajectory control?\nq - Quit trajectory\nf - Finish this loop of motion\nr - Resume trajectory\n";
                    cin >> quitType; break;
                }
                time_t now = time(0); tm *fn = localtime(&now); if(fn->tm_hour >= SleepTime) { quitType = 'f'; break; } // Quit button loop directly after sleep time
            }while(!nodeList[0]->Status.RT.Value().cpm.InA);
        }

        // Update index file for grasshopper display
        myfile.open ("indexBrk.txt");
        myfile << i + 1 + listOffset;
        myfile.close();

        // Go pick up a brick
        if(showAttention) { cout << "Picking up a brick\n"; }
        //// ... inform brick station ... ////
        Ard_char[1] = 'd'; Ard_char[3] = '9'; Ard_char[4] = '0'; Ard_char[5] = ' '; Ard_char[6] = ' '; // 5th pole it +1.52 deg to site x-axis
        SendGripperSerial(hComm, Ard_char); // Rotate gripper //163 <- 26 deg at brick pick-up station
        Ard_char[3] = ' '; Ard_char[4] = ' '; Ard_char[5] = ' '; // Reset the ending number with space char
        Sleep(50); // short break between bluetooth communication
        Ard_char[1] = 'o';
        SendGripperSerial(hComm, Ard_char); // Open gripper
        goalPos[6] = sqrt(pow(brickPickUp[0]-r.in[0],2)+pow(brickPickUp[1]-r.in[1],2)+pow(brickPickUp[2]+0.21-r.in[2],2))/velLmt*1000; // calculate time // 0.21 is height above brick pickup
        copy(brickPickUp, brickPickUp+3, begin(goalPos));
        goalPos[5] = 0.0141; // calculated yaw for +0.21 height
        goalPos[2] += 0.21; // 0.21 safe height from ABB
        if(RunParaBlend(r, goalPos) < 0) { cout << "Trajectory aborted.\n"; return; } // Go to safe height above pick up
        Sleep(100);
        //// ... wait for brick to be available ... ////
        
        // fall and pick up brick
        brickPickUp[6] = safeT*1.2;
        if(RunParaBlend(r, brickPickUp) < 0) { cout << "Trajectory aborted.\n"; return; } // Go pick up
        Sleep(600); //////////// FOR TESTING ONYL, delete later!!!!!!!!!!!!!!!!!!
        Ard_char[1] = 'c';
        SendGripperSerial(hComm, Ard_char); // Close gripper
        Sleep(800); // wait for grippper to close
        //// ... wait for brick released from station, also 5th pole is at safe position ... ////

        // Go to building level
        if(showAttention) { cout << "Raising brick from station\n"; }
        copy(brickPickUp, brickPickUp+6, begin(goalPos));
        goalPos[2] += 0.14;
        goalPos[6] = safeT;
        if(RunParaBlend(r, goalPos) < 0) { cout << "Trajectory aborted.\n"; return; } // raise the brick from rail station
        if(showAttention) { cout << "Going to building level\n"; }
        Ard_char[1] = 'd';
        string s = to_string((int)(brickPos[i][4] + 92.2 - brickPos[i][3]/3.1415965*180)); // <-+27, constant frame to gripper offset; - yaw rotation in EE
        for(int i=0; i<min(s.size(),4); i++){ Ard_char[i+3]=s[i]; }
        SendGripperSerial(hComm, Ard_char); // Rotate gripper according to brickPos
        copy(safePt, safePt+6, begin(goalPos)); // safe point
        goalPos[6] = sqrt(pow(safePt[0]-r.in[0],2)+pow(safePt[1]-r.in[1],2)+pow(safePt[2]-r.in[2],2))/velLmt*1000; // calculate time
        if(RunParaBlend(r, goalPos) < 0) { cout << "Trajectory aborted.\n"; return; } // return to safe point
        if(brickPos[i][1]+2.149848*brickPos[i][0]>25.250178){ // check if target brick pos falls into the region where cables may hit 5th pole
            if(showAttention){ cout << "Attention: Brick is in mountain region. Caution: may hit 5th pole\n"; }
            // near road, check if 5th pole posistion need moving
            if((lowPole-brickPos[i][2])*(lowPole-brickPos[i][2]+safeZoneH)<=0){
                // Move 5th pole to just above the safe zone
                // MoveBrkRail(brickPos[i][2]+safeZoneH);
                goalPos[2] = lowPole; // safe level as 5th pole
                goalPos[6] = (goalPos[2]-r.in[2])/velLmt*1000;
                if(showAttention) { cout << "Lowering to pole position??\n"; }
                if(RunParaBlend(r, goalPos) < 0) { cout << "Trajectory aborted.\n"; return; } 
            }
        }
        else if(brickPos[i][1]-2.271415*brickPos[i][0]<-13.260362){ // check if target brick pos falls into the region where cables may hit 5th pole
            if(showAttention){ cout << "Attention: Brick is in brick loading region. Caution: may hit 5th pole\n"; }
            if((lowPole-brickPos[i][2])*(lowPole-brickPos[i][2]+safeZoneH)<=0){
                // if lowPole < brickPos[i][2]+EeH { move ee to 5th pole height || move 5th above EE safeZone}
                // else {move 5th pole to ee height}
                // move to 5th pole height
                goalPos[2] = lowPole; // safe level as 5th pole
                goalPos[6] = (goalPos[2]-r.in[2])/velLmt*1000;
                if(showAttention) { cout << "Lowering to pole position??\n"; }
                if(RunParaBlend(r, goalPos) < 0) { cout << "Trajectory aborted.\n"; return; } 
            }
        }
        goalPos[2] = brickPos[i][2] + r.GetEEOffset() + safeH; // brick level with safe height
        if(r.in[2] < brickPos[i][2] + r.GetEEOffset() + safeH){ // if current position is below target brick height, then raise brick first // need this?
            goalPos[6] = (goalPos[2]-r.in[2])/velLmt*1000;
            if(RunParaBlend(r, goalPos) < 0) { cout << "Trajectory aborted.\n"; return; } // raise the brick to building level
        }
        
        // Go to brick placing position
        if(showAttention) { cout << "Going to brick position\n"; }
        goalPos[0] = brickPos[i][0];
        goalPos[1] = brickPos[i][1];
        goalPos[5] = brickPos[i][3]; // Include yaw angle
        goalPos[6] = sqrt(pow(goalPos[0]-r.in[0],2)+pow(goalPos[1]-r.in[1],2)+pow(goalPos[2]-r.in[2],2))/velLmt*1000;
        if(RunParaBlend(r, goalPos) < 0) { cout << "Trajectory aborted.\n"; return; }

        // Place brick
        if(showAttention) { cout << "Placing brick\n"; }
        goalPos[2] -= safeH;
        goalPos[6] = safeT;
        if(RunParaBlend(r, goalPos) < 0) { cout << "Trajectory aborted.\n"; return; }
        Ard_char[1] = 'o';
        SendGripperSerial(hComm, Ard_char); // Open gripper
        Sleep(200); //Wait a while after placing brick
        
        // Rise and leave building area, stand by for next brick pick up
        if(showAttention) { cout << "Going to stand by position\n"; }
        goalPos[2] += safeH;
        if(RunParaBlend(r, goalPos) < 0) { cout << "Trajectory aborted.\n"; return; } // leave building level
        copy(begin(safePt), end(safePt), begin(goalPos)); // safe x,y,z position
        if(r.in[2] > safePt[2]){ // if current position is above safe point, then return to safe point within lowering z-height
            goalPos[2] = r.in[2];
            goalPos[6] = sqrt(pow(goalPos[0]-r.in[0],2)+pow(goalPos[1]-r.in[1],2))/velLmt*1000;
            if(RunParaBlend(r, goalPos) < 0) { cout << "Trajectory aborted.\n"; return; } // raise the brick to building level
            goalPos[2] = safePt[2];
        }
        goalPos[6] = sqrt(pow(goalPos[0]-r.in[0],2)+pow(goalPos[1]-r.in[1],2)+pow(goalPos[2]-r.in[2],2))/velLmt*1000;
        if(RunParaBlend(r, goalPos) < 0) { cout << "Trajectory aborted.\n"; return; } // return to safe point 
        
        r.PrintIn();
        cout << r.railOffset[0] << " " << r.railOffset[1] << " " << r.railOffset[2] << " " << r.railOffset[3] << endl;
        cout << "----------Completed brick #" << i + 1 + listOffset <<"----------" << endl;
    }
    Ard_char[1] = 'r';
    SendGripperSerial(hComm, Ard_char); // Release gripper
    copy(r.home, r.home+6, begin(goalPos));
    goalPos[6] = sqrt(pow(goalPos[0]-r.in[0],2)+pow(goalPos[1]-r.in[1],2)+pow(goalPos[2]-r.in[2],2))/velLmt*1000;
    if(RunParaBlend(r, goalPos) < 0) { cout << "Trajectory aborted.\n"; return; } // home after building all
    cout << "Photo time~~\n"; Sleep(3000);
}

// void ReverseBricksTraj(const dynamixel::GroupSyncRead &groupSyncRead, int listOffset, bool showAttention){
void ReverseBricksTraj(CDPR &r, int listOffset, bool showAttention){
    double brickDropOff[7] = {0.72, 2.14, 1.9, 0, 0, 0, 10}; // !!!! Define the brick drop off point !!!!, the last digit is a dummy number for time duration. // rotation 165 for drop off
    double safePt[3] = {1.6, 1.8, 2.05}; // a safe area near the drop off
    double goalPos[7] = {2, 2, 1, 0, 0, 0, 10}; // updated according to brick position
    double velLmt = 0.27; // meters per second
    double safeT = 1100; // in ms, time to raise to safety height //1200 ms
    double safeH = 0.08; // meter, safety height from building brick level
    double currentBrkLvl = r.railOffset[r.GetRailNum()-1]; // meter, check if the rail offset is the same as target BrkLvl
    double dura = 0;

    ofstream myfile;
    myfile.open ("indexBrk.txt");
    myfile << -1; // -1 for disassembly
    myfile.close();
    
    // Go through the given bricks
    for (int i = brickPos.size()-1; i > -1; i--) {
        // // Check if rails need to be raised
        // if(ScaleRailLvl(brickPos[i][2] - 0.04) != currentBrkLvl){
        //     currentBrkLvl = ScaleRailLvl(brickPos[i][2] - 0.04); // Offset one brick height from building levei, ie 0.04m
        //     if(RaiseRailTo(currentBrkLvl) < 0) { cout << "Trajectory aborted.\n"; return; } // raise rail to the building brick level
        // }

        // Go to building level
        if(showAttention) { cout << "Going to building level\n"; }
        // if(packetHandler->write2ByteTxRx(portHandler, DXL2_ID, ADDR_GOAL_CURRENT, gpOpen, &dxl_error)){ cout << "Error in opening gripper\n"; return; }
        copy(r.in, r.in+2, begin(goalPos));
        goalPos[2] = brickPos[i][2] + r.GetEEOffset() + safeH; // brick level
        goalPos[6] = sqrt(pow(goalPos[2]-r.in[2],2))/velLmt*1000 + 800; // calculate time + hardcode time to smooth out sudden jump
        if(RunParaBlend(r, goalPos) < 0) { cout << "Trajectory aborted.\n"; return; } // raise from current pos to builing level
        
        // Go to brick placing position
        // rotationG = brickPos[i][3] * 11.26666;; // conversion from angle to motor command
        // if(packetHandler->write4ByteTxRx(portHandler, DXL1_ID, ADDR_GOAL_POSITION, rotationG, &dxl_error)){ cout << "Error in rotating gripper\n"; return; }
        if(showAttention) { cout << "Going to brick position\n"; }
        goalPos[0] = brickPos[i][0];                       
        goalPos[1] = brickPos[i][1];
        goalPos[6] = sqrt(pow(goalPos[0]-r.in[0],2)+pow(goalPos[1]-r.in[1],2))/velLmt*1000;
        if(RunParaBlend(r, goalPos) < 0) { cout << "Trajectory aborted.\n"; return; }
        
        // Retrieve brick
        if(showAttention) { cout << "Retrieving brick\n"; }
        goalPos[2] -= safeH;
        goalPos[6] = safeT;
        if(RunParaBlend(r, goalPos) < 0) { cout << "Trajectory aborted.\n"; return; }
        // if(packetHandler->write2ByteTxRx(portHandler, DXL2_ID, ADDR_GOAL_CURRENT, gpClose, &dxl_error)){ cout << "Error in closing gripper\n"; return; }
        Sleep(1500); // Wait for gripper to close

        // Rise and leave building area, stand by for next brick pick up
        if(showAttention) { cout << "Going to drop off position\n"; }
        goalPos[2] += safeH;
        if(RunParaBlend(r, goalPos) < 0) { cout << "Trajectory aborted.\n"; return; } // leave building level
        // if(packetHandler->write4ByteTxRx(portHandler, DXL1_ID, ADDR_GOAL_POSITION, neutralRot, &dxl_error)){ cout << "Error in rotating gripper\n"; return; }
        copy(begin(safePt), end(safePt), begin(goalPos)); // safe x,y position
        goalPos[6] = sqrt(pow(goalPos[0]-r.in[0],2)+pow(goalPos[1]-r.in[1],2)+pow(goalPos[2]-r.in[2],2))/velLmt*1000;
        if(RunParaBlend(r, goalPos) < 0) { cout << "Trajectory aborted.\n"; return; } // return to safe point 
        
        // Dropping off a brick
        if(showAttention) { cout << "Dropping off the brick\n"; }
        brickDropOff[6] = sqrt(pow(brickDropOff[0]-r.in[0],2)+pow(brickDropOff[1]-r.in[1],2)+pow(brickDropOff[2]-r.in[2],2))/velLmt*1000; // calculate time
        if(RunParaBlend(r, brickDropOff) < 0) { cout << "Trajectory aborted.\n"; return; }
        brickDropOff[6] = 800; // Safe time for dropping
        brickDropOff[2] -= 0.02; // go down a little more before real drop
        if(RunParaBlend(r, brickDropOff) < 0) { cout << "Trajectory aborted.\n"; return; }
        // if(packetHandler->write2ByteTxRx(portHandler, DXL2_ID, ADDR_GOAL_CURRENT, gpOpen, &dxl_error)){ cout << "Error in opening gripper\n"; return; }
        Sleep(500); // wait a little after dropping brick
        brickDropOff[2] += 0.02; // go up a little more after real drop
        if(RunParaBlend(r, brickDropOff) < 0) { cout << "Trajectory aborted.\n"; return; }
        copy(begin(safePt), end(safePt), begin(goalPos)); // safe point
        goalPos[6] = sqrt(pow(goalPos[0]-r.in[0],2)+pow(goalPos[1]-r.in[1],2)+pow(goalPos[2]-r.in[2],2))/velLmt*1000;
        if(RunParaBlend(r, goalPos) < 0) { cout << "Trajectory aborted.\n"; return; } // return to safe point 
        
        r.PrintIn();
        cout << "----------Retrieved brick #" << i + 1 + listOffset <<"----------" << endl;
    }
    // packetHandler->write2ByteTxRx(portHandler, DXL2_ID, ADDR_GOAL_CURRENT, 0, &dxl_error);
    copy(r.home, r.home+3, begin(goalPos));
    goalPos[6] = sqrt(pow(goalPos[0]-r.in[0],2)+pow(goalPos[1]-r.in[1],2)+pow(goalPos[2]-r.in[2],2))/velLmt*1000;
    if(RunParaBlend(r, goalPos) < 0) { cout << "Trajectory aborted.\n"; return; } // home after retrieving all
}

void RunTrajPoints(CDPR &r){
    // Read raw points from traj.csv
    ifstream file ("traj.csv");
    vector<double> row;
    string line, word, temp;

    brickPos.clear();
    if(file.is_open()){
        while (getline(file, line)){
            row.clear();
            stringstream s(line);
            while (s >> word){
                row.push_back(stod(word)); // convert string to double stod()
            }
            brickPos.push_back(row);
        }
        cout << "Completed reading traj input file" << endl;
    }
    else{ cout << "Failed to read input file. Please check \"traj.csv\" file." << endl; return;
    }
    
    // Go through the given points
    double goalPos[7] {};
    for (int i = 0; i < brickPos.size(); i++) {
        copy(begin(brickPos[i]), end(brickPos[i]), begin(goalPos));
        // cout << "i: " << i << endl;
        // cout << "brickPos("<< brickPos[i].size()<<"): "<< brickPos[i][0]<<","<< brickPos[i][1]<<","<< brickPos[i][2]<<","<< brickPos[i][3]<<","<< brickPos[i][4]<<","<< brickPos[i][5]<<","<< brickPos[i][6]<<endl;
        if(RunParaBlend(r, goalPos) < 0) { cout << "Trajectory aborted.\n"; return; }              
        r.PrintIn();
        cout << r.railOffset[0] << " " << r.railOffset[1] << " " << r.railOffset[2] << " " << r.railOffset[3] << endl;
        cout << "----------Completed traj point #" << i + 1 <<"----------" << endl;
    }
}

int RunExternPoints(CDPR &r){
    // Read raw points from extern.csv
    ifstream file ("extern.csv");
    vector<double> row;
    string line, word, temp;

    brickPos.clear();
    if(file.is_open()){
        while (getline(file, line)){
            row.clear();
            stringstream s(line);
            while (s >> word){
                row.push_back(stod(word)); // convert string to double stod()
            }
            brickPos.push_back(row);
        }
        cout << "Completed reading external traj step points input file" << endl;
    }
    else{ cout << "Failed to read input file. Please check \"extern.csv\" file." << endl; return -1; }
    
    ofstream myfile;
    myfile.open("extern_log.csv", ios::app);
    tm *fn; time_t now = time(0); fn = localtime(&now);
    myfile <<  fn->tm_mon +1 << "/" << fn->tm_mday << ": " << fn->tm_hour << ":" << fn->tm_min << ":" << fn->tm_sec << "\n";;
    myfile.close();
    
    auto start = chrono::steady_clock::now();
    long dur = 0;
    // Go through the given points
    for (int i = 0; i < brickPos.size(); i++) {    
        copy(begin(brickPos[i]), end(brickPos[i]), begin(r.in));   
        cout << "[" << brickPos.size() - i << "] "; r.PrintIn();
        
        r.PoseToLength(r.in, r.out, r.railOffset);
        SendMotorGrp(r);
        RecordMotorTrq(r); // Record measured torque at each command step
        if(quitType == 'e'){ // Motor error message?
            cout << "WARNING! Motor error message received. System will now shut now.\n";
            return -4;
        }

        // Write to traking file
        myfile.open ("traking.txt");
        myfile << r.in[0] << " " << r.in[1] << " " << r.in[2] << " " << r.in[3] << " " << r.in[4] << " " << r.in[5] << endl;
        myfile << r.railOffset[0] << " " << r.railOffset[1] << " " << r.railOffset[2] << " " << r.railOffset[3] << endl;
        myfile.close();

        auto end = chrono::steady_clock::now();
        dur = chrono::duration_cast<chrono::milliseconds>(end-start).count();
        
        double dif = MILLIS_TO_NEXT_FRAME - dur - 1;
        if(dif > 0) { Sleep(dif); }

        if(kbhit()){ // Emergency quit during trajectory control
            cout << "\nSystem interrupted!! Do you want to quit the trajectory control?\nq - Quit trajectory\nf - Finish this loop of motion\nr - Resume trajectory\n";
            cin >> quitType;
            if(quitType == 'q' || quitType == 'Q'){
                cout << "Trajectory emergency quit\n";
                return -2;
            }
        }
        if(limitType != 'C'){
            cout << "WARNING! Linear rail limits triggered. Please quit the programme and check the system.\n";
            return -3;
        }
        start = end;
    }
    return 0;
}

void SendMotorCmd(CDPR &r, int n){
    ofstream myfile;
    // convert to absolute cable length command
    try{
        // int32_t step = ToMotorCmd(n, out1[n]);
        int32_t step = r.ToMotorCmd(n, r.out[n]);
        nodeList[n]->Motion.Adv.MovePosnStart(step, true, true); // absolute position, wait for trigger

        if (nodeList[n]->Status.Alerts.Value().isInAlert()) {
            myfile.open("log.txt", ios::app);
            myfile << "Alert from Motor [" << n << "]: "<< nodeList[n]->Status.Alerts.Value().bits <<"\n";
            myfile.close();
        }
    }
    catch(sFnd::mnErr& theErr) {    //This catch statement will intercept any error from the Class library
        cout << "\nERROR: Motor [" << n << "] command failed.\n";  
        printf("Caught error: addr=%d, err=0x%08x\nmsg=%s\n", theErr.TheAddr, theErr.ErrorCode, theErr.ErrorMsg);
        quitType = 'e';
        tm *fn; time_t now = time(0); fn = localtime(&now);
        myfile.open("log.txt", ios::app);
        myfile << "\nERROR: Motor [" << n << "] command failed. " << fn->tm_hour << ":"<< fn->tm_min << ":" << fn->tm_sec <<"\n";
        myfile << "Caught error: addr="<< (int) theErr.TheAddr<<", err="<<hex<<theErr.ErrorCode <<"\nmsg="<<theErr.ErrorMsg<<"\n";
        myfile.close();
    }
}

void SendMotorTrq(CDPR &r, int n){
    nodeList[n]->Motion.TrqCommanded.Refresh();
    // The API cannot set torque directily, but it will give the target torque of current command
    // So we can send move command and then verify the retured value to adjust the torque
    float currentTrqCommanded = nodeList[n]->Motion.TrqCommanded.Value();

    // still question, the output should be need more torque?
    if(currentTrqCommanded > targetTorque){ nodeList[n]->Motion.MoveVelStart(-300); }
    else if (currentTrqCommanded < targetTorque - 1.8){ nodeList[n]->Motion.MoveVelStart(150); cout << "Too much torque!!\n";}
    else{ nodeList[n]->Motion.MoveVelStart(-10);}
    printf("Node[%d], current torque: %f\n", n, currentTrqCommanded);
}

void SendMotorGrp(CDPR &r, bool IsTorque, bool IsLinearRail){
    SysManager* myMgr = SysManager::Instance();
    IPort &myPort = myMgr->Ports(0); // only if one scHub is used
    void (*func)(CDPR&, int){ SendMotorCmd };
    if(IsTorque){ func = SendMotorTrq; }
    int indexOffset = IsLinearRail? 4 : 0; // offset in nodeList
    
    thread nodeThreads[8];
    for(int i = 0; i < r.GetNodeNum(); i++){
        nodeThreads[i] = thread((*func), r, i + indexOffset); 
    }
    for(int i = 0; i < r.GetNodeNum(); i++){
        nodeThreads[i].join();
    }
    // myPort.Adv.TriggerMovesInGroup(1);
    if (quitType!='e'){ myPort.Adv.TriggerMovesInGroup(1); } // Only move all if no error is caugth
}

void RecordMotorTrq(CDPR &r){ // Record current measured torque for all listed motors
    static auto start = chrono::steady_clock::now();
    ofstream myfile;
    myfile.open("extern_log.csv", ios::app);
    // for(int i = 0; i < nodeList.size(); i++){
    //     myfile << nodeList[i]->Motion.TrqMeasured.Value() << ", " ;
    // }

    double tmp[8]; // array for storing read torque value, while using threading
    auto readTorqueThread = [&tmp](int i) { tmp[i] = nodeList[i]->Motion.TrqMeasured.Value(); };
    thread nodeThreads[8];
    for(int i = 0; i < r.GetNodeNum(); i++){
        nodeThreads[i] = thread(readTorqueThread, i); 
    }
    for(int i = 0; i < r.GetNodeNum(); i++){
        nodeThreads[i].join();
    }
    myfile << tmp[0] << ", " << tmp[1] << ", " << tmp[2] << ", " << tmp[3] << ", " << tmp[4] << ", " << tmp[5] << ", " << tmp[6] << ", " << tmp[7] << ", ";
    myfile << r.in[0] << ", " << r.in[1] << ", " << r.in[2] << ", " << r.in[3] << ", " << r.in[4] << ", " << r.in[5] << ", ";
    auto end = chrono::steady_clock::now();
    long dur = chrono::duration_cast<chrono::milliseconds>(end-start).count();
    myfile << dur <<"\n";
    myfile.close();
    start = end;
}

void TrjHome(CDPR &r){// !!! Define the task space velocity limit for homing !!!
    double velLmt = 0.1; // 0.1 // unit in meters per sec
    double dura = sqrt(pow(r.   wein[0]-r.home[0],2)+pow(r.in[1]-r.home[1],2)+pow(r.in[2]-r.home[2],2))/velLmt*1000; // *1000 to change unit to ms
    double a[6], b[6], c[6]; // cubic coefficients
    double t = 0;
    cout << "Expected homing duration: " << dura <<"ms\n";
    if (dura == 0){ return; }

    for(int i = 0; i < 6; i++){
        // solve coefficients of equations for cubic
        a[i] = r.in[i];
        b[i] = 3 / (dura * dura) * (r.home[i] - r.in[i]);
        c[i] = -2 / (dura * dura * dura) * (r.home[i] - r.in[i]);
    }
    
    auto start = chrono::steady_clock::now();
    long dur = 0;
    while (t <= dura){  
        // CUBIC equation
        for (int j = 0; j < 6; j++){
            r.in[j] = a[j] + b[j] * t * t + c[j] * t * t * t;
        }
        // r.PrintIn();
        r.PoseToLength(r.in, r.out, r.railOffset);
        // cout << "OUT: "<<  out1[0] << " " << out1[1] << " " << out1[2] << " " << out1[3] << endl;
        
        SendMotorGrp(r);
        RecordMotorTrq(r);

        // Write to traking file
        ofstream myfile;
        myfile.open ("traking.txt");
        myfile << r.in[0] << " " << r.in[1] << " " << r.in[2] << " " << r.in[3] << " " << r.in[4] << " " << r.in[5] << endl;
        myfile << r.railOffset[0] << " " << r.railOffset[1] << " " << r.railOffset[2] << " " << r.railOffset[3] << endl;
        myfile.close();
        
        auto end = chrono::steady_clock::now();
        dur = chrono::duration_cast<chrono::milliseconds>(end-start).count();
        cout << " Before sleep: " << dur << endl;
        
        double dif = MILLIS_TO_NEXT_FRAME - dur;
        if(dif > 0) { Sleep(dif); }
        // Sleep(MILLIS_TO_NEXT_FRAME);

        end = chrono::steady_clock::now();
        dur = chrono::duration_cast<chrono::milliseconds>(end-start).count();
        cout << " Time elasped: " << dur << "\tTime left: " << dura - t << endl;
        t += MILLIS_TO_NEXT_FRAME;
        if(kbhit()){ // Emergency quit during trajectory control
            cout << "\nSystem interrupted!! Do you want to quit the trajectory control?\nq - Quit trajectory\nr - Resume trajectory\n";
            char cmd;
            cin >> cmd;
            if(cmd == 'q' || cmd == 'Q'){
                cout << "Trajectory control aborted.\n";
                t = dura;
                break;
            }
        }
        if(limitType != 'C'){
            cout << "WARNING! Linear rail limits triggered. Please quit the programme and check the system.\n";
            return;
        }
        start = end;
    }
    cout << "Homing with trajectory completed\n";
}

bool ReadBricksFile(CDPR &r){ // Define which file to read here !!!
    ifstream file ("bricks.csv");
    vector<double> row;
    string line, word, temp;

    brickPos.clear();
    if(file.is_open()){
        while (getline(file, line)){
            row.clear();
            stringstream s(line);
            while (s >> word){
                row.push_back(stod(word)); // convert string to double stod()
            }
            // Convert angles
            if(row[4]<0){ row[4] += 180; } // convert -ve degs to 180
            // if(row[4]>180){ row[4] -= 180; } // convert 360 degs to 180
            // Calculate and add x, y offsets due to rotation mis-alignment
            row[0] -= r.tempD*sin((r.tempA + row[4])*M_PI/180);
            row[1] += r.tempD*cos((r.tempA + row[4])*M_PI/180);
            brickPos.push_back(row);
        }
        cout << "Completed reading brick position input file" << endl;
    }
    else{ cout << "Failed to read input file. Please check \"bricks.csv\" file." << endl; return false;
    }
    return true;
}

void HomeLinearRail(int n){
    double velLmt = -2000; // IMPORTANT!!!!! Set the linear homing speed here!!
    double hLmtOffset[4] = {-34000, -34000, -34000, -34000}; // Set offset from home switch to real "home" in motor counts units

    SysManager* myMgr = SysManager::Instance();
    INode &theNode = myMgr->Ports(2).Nodes(n);
    theNode.Motion.VelocityReachedTarget();
    theNode.Motion.MoveVelStart(velLmt);
            
    while(!kbhit()){
        switch(limitType){
        case 'A':
            cout << "InA detected!\n";
            theNode.Motion.MoveVelStart(0);
            while(!theNode.Motion.VelocityAtTarget()){} // wait till motor stopped
            theNode.Motion.AddToPosition(-theNode.Motion.PosnMeasured.Value()+hLmtOffset[n]); // Add offset here if applicable
            cout << "Reached homed swtich.\n";
            theNode.Motion.MovePosnStart(0, true);
            while(!theNode.Motion.MoveIsDone()){}
            cout << "Homing completed.\n";
            limitType = 'C';
            return;
        case 'B':
            cout << "InB detected!\n";
            velLmt *= -1;
            theNode.Motion.MoveVelStart(velLmt);
            if (velLmt > 0) { // ie moving upwards
                while(limitType != 'A') {} //wait till home switch is passed
                Sleep(2000);
                velLmt *= -1;
                theNode.Motion.MoveVelStart(velLmt);
                limitType = 'C'; // wait till the home is hit from upward side
                break;
            } // else, just break and wait till homed
            break;
        }
    }
    theNode.Motion.MoveVelStart(0);
    cout << "Linear rail motion interrupted\n";
}

void MN_DECL AttentionDetected(const mnAttnReqReg &detected){
    // Make a local, non-const copy for printing purposes
    mnAttnReqReg myAttns = detected;
    // Load the buffer with the string representation of the attention information
    myAttns.AttentionReg.StateStr(attnStringBuf, 512);
    // Print it out to the console
    printf("ATTENTION: port %d, node=%d, attn=%s\n", detected.MultiAddr >> 4, detected.MultiAddr, attnStringBuf);

    limitType = attnStringBuf[strlen(attnStringBuf)-2];
}