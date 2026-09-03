#pragma once
#include <zephyr/kernel.h>

// FSM States and Transitions
enum FSMStates {
  FSM_BOOTING,
  FSM_STOPPED,
  FSM_IDLE,
  FSM_EXECUTE,
  FSM_HOLDING,
  FSM_HELD,
  FSM_STOPPING,
  FSM_SUSPENDING,
  FSM_SUSPENDED,
  FSM_ABORTING,
  FSM_ABORTED,
  FSM_CLEARING
};

enum FSMTransitions {
  TRAN_EVT_NONE,
  TRAN_EVT_ENABLE,
  TRAN_EVT_BOOTED,
  TRAN_EVT_RUN,
  TRAN_EVT_HOLD,
  TRAN_EVT_RESUME,
  TRAN_EVT_STOP,
  TRAN_EVT_SUSPEND,
  TRAN_EVT_UNSUSPEND,
  TRAN_EVT_ESTOP,
  TRAN_EVT_FAULT_CLEAR,
  TRAN_EVT_ABORT,
};

// Input Ports
struct CommandHeader {
  uint8_t cmdType;
  uint32_t seqNum;
  int64_t timestampMs;
};

struct DriveVelocityCommand {
  struct CommandHeader hdr;
  float velocity;
  float headingRad;
};

struct DrivePositionCommand {
  struct CommandHeader hdr;
  float distance;
  float headingRad;
};

struct ArmCommand {
  struct CommandHeader hdr;
  uint16_t jointIdx;
  float armAngleRad;
};

struct SWSafetyCommands {
  enum FSMTransitions swCommand;
};

// Output Ports
struct IMUTelemetry {
  float ax, ay, az;
  float gx, gy, gz;
};

struct PowerTelemetry {
  float currentDraw;
  float batteryVoltage;
};

struct MotorStates {
  float wheelSpeedRPM;
  float wheelTorque;
  float encoderPositionRad;
  uint8_t isRunning;
};

struct ArmMotorStates {
  float encoderPositionRad;
  uint8_t isRunning;
};

struct PeripheralTelemetry {
  struct IMUTelemetry imu;
  struct PowerTelemetry power;
  struct MotorStates m1, m2, m3, m4;
  struct ArmMotorStates j1, j2, j3, j4, j5, j6;
};

// Safety State
struct SafetyState {
  bool estopAsserted;
  bool hwEnableActive;
  bool watchdogOk;
  bool upstreamHeartbeatOk;
  bool powerFault;
  bool motorFault;
  bool armFault;
  bool sensorFault;
};

// Internal Control Structs
struct VCUState {
  enum FSMStates cs, ns;
  struct SafetyState safety;

  // capabilities
  bool drive_allowed, arm_allowed, telemetry_active, config_mutable,
      manual_control_allowed, autonomy_control_allowed, fault_reset_allowed;
};

// Command Processing Telemetry
enum CommandStatus {
  CMD_ACCEPTED,
  CMD_REJECTED_FORMAT,
  CMD_REJECTED_STALE,
  CMD_REJECTED_SEQUENCE,
  CMD_REJECTED_WRONG_STATE,
  CMD_REJECTED_CAPABILITY,
  CMD_REJECTED_OUT_OF_RANGE,
  CMD_REJECTED_SAFETY,
  CMD_REJECTED_SUBSYSTEM
};

struct CommandTelemetry {
  enum CommandStatus lastStatus;
  uint32_t lastSeqNum;
  uint32_t acceptedCount;
  uint32_t rejectedCount;
};

// Diagnostic Counters
struct DiagnosticCounters {
  uint32_t uptimeMs;
  uint32_t resetCount;
  uint32_t rxPacketCount;
  uint32_t txPacketCount;
  uint32_t busErrorCount;
  uint32_t faultCount;
};

// Upstream Telemetry Output
struct VCUTelemetry {
  struct VCUState state;
  struct SafetyState safety;
  struct CommandTelemetry cmd;
  struct PeripheralTelemetry peripheral;
  struct DiagnosticCounters diag;
};
