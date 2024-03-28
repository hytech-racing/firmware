/*
 * Teensy 4.0 code for rear Sensor Acquisition Board rev5
 */

/**
 *  Include files 
 */

/* Framework */
#include <Arduino.h>
#include <stdint.h>

/* Libraries */
#include "FlexCAN_T4.h"
#include "HyTech_CAN.h"

/* Constants */
#include "rear_sab_dfs.h"

/* Interfaces */
// #include "HyTechCANInterface.h"
#include "MCP_ADC.h"
// #include "TelemetryInterface.h"

/* Systems */
#include "SysClock.h"

/**
 * Utilities
*/
// #include "MessageQueueDefine.h"
#include "DebouncedButton.h"
#include "Filter_IIR.h"

/**
 * Vector Nav
*/
#include "types.h"
#include "hytech.h"

/**
 * Data source
*/

/* One CAN line on rear SAB rev5 */
FlexCAN_T4<CAN2, RX_SIZE_256, TX_SIZE_16> TELEM_CAN;  // Telemetry

/* Outbound CAN message */
SAB_thermistors_1 sab_thermistors_1;
SAB_thermistors_2 sab_thermistors_2;
SAB_CB sab_cb;
TCU_status tcu_status;

/* Inbound CAN message */
MCU_status mcu_status;

/* Sensors */
// ADCs
MCP_ADC<4> ADC1 = MCP_ADC<4>(ADC1_CS, ADC_ISOSPI_SPEED);  // RL corner board
MCP_ADC<4> ADC2 = MCP_ADC<4>(ADC2_CS, ADC_ISOSPI_SPEED);  // RR corner board
MCP_ADC<8> ADC3 = MCP_ADC<8>(ADC3_CS, ADC_SPI_SPEED);  // Thermistors

// ADC_SPI ADC1(ADC1_CS, ADC_SPI_SPEED);


// Pi shutdown
DebouncedButton btn_pi_shutdown;
// VectorNav

/**
 * Interfaces
*/
// TelemetryInterface telem_interface(&CAN2_txBuffer, {THERM_3, THERM_4, THERM_5, THERM_6, THERM_7, THERM_8, THERM_9,
//                                                     SUS_POT_3, SUS_POT_4, RL_LOAD_CELL, RR_LOAD_CELL});

/**
 * Systems
*/
SysClock sys_clock;

/* Metro timers */
// Sensor read
Metro timer_read_all_adcs = Metro(10);
Metro timer_read_imu = Metro(10);               // serial delay from polling request
Metro timer_vectornav_read_binary = Metro(20);  // configured 100Hz
Metro timer_vectornav_change_reading = Metro(40); // change binary output group 25Hz
Metro timer_send_CAN_vn_gps_time = Metro(40);   // 25Hz
Metro timer_send_CAN_vn_position = Metro(50);   // 25Hz
Metro timer_send_CAN_vn_accel = Metro(30);   // 25Hz
Metro timer_send_CAN_vn_ins_status = Metro(50);   // 25Hz
Metro timer_send_CAN_vn_uncomp_accel = Metro(40);   // 25Hz
Metro timer_send_CAN_vn_vel_body = Metro(30);   // 25Hz
Metro timer_send_CAN_vn_angular_rate = Metro(20); // 50Hz
Metro timer_send_CAN_vn_yaw_pitch_roll = Metro(20); // 50Hz

/* Utilities */
// IIR filter for DSP
// Thermistors
Filter_IIR thermistor_iir[TOTAL_THERMISTOR_COUNT];
// Loadcells
// Filter_IIR loadcell_iir[TOTAL_LOADCELL_COUNT] = Filter_IIR{LOADCELL_ALPHA, LOADCELL_ALPHA};  // actually will be done by torque controllers themselves if needed

/* Global variables */
// Vector Nav
uint8_t receiveBuffer[DEFAULT_SERIAL_BUFFER_SIZE];
int binaryOutputNumber;
int currentPacketLength;
// CAN messages (for now)
VN_VEL_t vn_vel_body;
VN_LINEAR_ACCEL_t vn_accel;
VN_LINEAR_ACCEL_UNCOMP_t vn_uncomp_accel;
VN_LAT_LON_t vn_position;
VN_GPS_TIME_t vn_time_gps;
VN_STATUS_t vn_ins_status;
VN_ANGULAR_RATE_t vn_angular_rate;
VN_YPR_t vn_YPR;

/* Function prototypes */
void init_all_CAN_devices();
void parse_telem_CAN_msg(const CAN_message_t &RX_msg);
void update_all_CAN_msg();
void send_sab_CAN_msg();
void send_CAN_vectornav();
void send_CAN_vn_gps_time();
void send_CAN_vn_position();
void send_CAN_vn_accel();
void send_CAN_vn_ins_status();
void send_CAN_vn_uncomp_accel();
void send_CAN_vn_vel_body();
void send_CAN_vn_angular_rate();
void send_CAN_vn_yaw_pitch_roll();
// void process_ring_buffer(CANBufferType &rx_buffer, unsigned long curr_millis);
// void send_all_CAN_msg(CANBufferType &tx_buffer, FlexCAN_T4_Base *can_interface);
void init_all_adcs();
void init_all_iir_filters();
void tick_all_interfaces(const SysTick_s &curr_tick);
void tick_all_systems(const SysTick_s &curr_tick);
// Vector Nav functions
void turnOffAsciiOutput();
void configBinaryOutput(uint8_t binaryOutputNumber, uint8_t fields, uint16_t rateDivisor);
void pollUserConfiguredBinaryOutput(uint8_t binaryOutputNumber);
void readPollingBinaryOutput();
void parseBinaryOutput_1();
void parseBinaryOutput_2();
void parseBinaryOutput_3();
void clearReceiveBuffer();

void setup() {

  // Tick system clock
  SysTick_s curr_tick = sys_clock.tick(micros());

  // Debug print
  Serial.begin(DEBUG_PRINT_BAUDRATE);
  Serial.println("Starting...");

  // Initialize CAN
  init_all_CAN_devices();

  // Initialize all SPI devices
  init_all_adcs();
  Serial.println("SPI initialized");

  // Set IIR filter alpha for thermistors
  init_all_iir_filters();
  Serial.println("IIR filter initialized");

  // Initialize debounced button
  btn_pi_shutdown.begin(PI_SHUTDOWN, 100);
  Serial.println("Debounce button initialized for Pi shutdown");

  // RS232
  Serial2.begin(IMU_RS232_SPEED);
  // Initialize binary output reg. number
  binaryOutputNumber = 0;
  // Initialize binary packet lengh
  currentPacketLength = 0;
  // Configure sensor
  turnOffAsciiOutput();
  configBinaryOutput(1, 0x01, 0);    // 0000 0001
  configBinaryOutput(2, 0x05, 0);    // 0000 0101
  configBinaryOutput(3, 0x28, 0);    // 0010 1000
  Serial.println("VectorNav initialized ... for real this time!");
  Serial.println();

}

void loop() {

  // Tick system clock
  SysTick_s curr_tick = sys_clock.tick(micros());

  // Process received CAN messages
  // Not currently needed
  TELEM_CAN.events();

  // Tick interfaces
  tick_all_interfaces(curr_tick);
  send_CAN_vectornav();

  // Tick systems
  // Not currently needed
  
  // Send outbound CAN messages
  // send_all_CAN_msg(CAN2_txBuffer, &TELEM_CAN);

  // Debug prints to see if we're tripping balls
  TriggerBits_s t = curr_tick.triggers;
  if (t.trigger5) {
    Serial.println("Thermistors:");
    Serial.println(ADC3.get().conversions[THERM_3].raw);
    Serial.println(ADC3.get().conversions[THERM_4].raw);
    Serial.println(ADC3.get().conversions[THERM_5].raw);
    Serial.println(ADC3.get().conversions[THERM_6].raw);
    Serial.println(ADC3.get().conversions[THERM_7].raw);
    Serial.println(ADC3.get().conversions[THERM_8].raw);
    Serial.println(ADC3.get().conversions[THERM_9].raw);
    Serial.println();
    Serial.println("Load cells:");
    Serial.println(ADC1.get().conversions[RL_LOAD_CELL].raw);
    Serial.println(ADC2.get().conversions[RR_LOAD_CELL].raw);
    Serial.println();
    Serial.println("Sus pots:");
    Serial.println(ADC1.get().conversions[SUS_POT_3].raw);
    Serial.println(ADC2.get().conversions[SUS_POT_4].raw);
    Serial.println();
    Serial.println("Vector Nav:");
    
    // u_int8_t fields = 0x05;

    // bool commonField = fields & 0x01;   
    // Serial.printf("common: %d\n", commonField);
    // bool timeField = fields & 0x02;     
    // Serial.printf("time: %d\n", timeField);
    // bool imuField = fields & 0x04;      
    // Serial.printf("imu: %d\n", imuField);
    // bool gpsField = fields & 0x08;     
    // Serial.printf("gps: %d\n", gpsField);
    // bool attitudeField = fields & 0x10;
    // Serial.printf("attitude: %d\n", attitudeField);
    // bool insField = fields & 0x20;      
    // Serial.printf("ins: %d\n", insField);
    // bool gps2Field = fields & 0x40;     
    // Serial.printf("gps2: %d\n", gps2Field);

    for (int i = 0; i < currentPacketLength; i++)
    {
      Serial.printf("%X ", receiveBuffer[i]);
    }
    Serial.printf("\nCurrent packet length: %d", currentPacketLength);
    Serial.println();
    Serial.println();
  }
  
  // Vector Nav data acquisition (for now)
  // Involves delay. moved to bottom to preserve timing
  if (timer_vectornav_change_reading.check())
  {
    binaryOutputNumber = (binaryOutputNumber + 1) % 3;
  }
  
  pollUserConfiguredBinaryOutput(binaryOutputNumber + 1);
  readPollingBinaryOutput();   // should not need to be here now

}

/**
 * Initialize CAN lines 
*/
void init_all_CAN_devices() {
  // Telemetry CAN line
  TELEM_CAN.begin();
  TELEM_CAN.setBaudRate(TELEM_CAN_BAUDRATE);
  TELEM_CAN.enableMBInterrupts();
  TELEM_CAN.onReceive(parse_telem_CAN_msg);

  // delay(500);
}

/**
 * Send CAN function
*/
// void send_all_CAN_msg(CANBufferType &tx_buffer, FlexCAN_T4_Base *can_interface) {
//     while (tx_buffer.available())
//     {
//         CAN_message_t msg;
//         uint8_t buf[sizeof(CAN_message_t)];
//         tx_buffer.pop_front(buf, sizeof(CAN_message_t));
//         memmove(&msg, buf, sizeof(msg));
//         can_interface->write(msg);
//     }    
// }

/**
 * Process Rx buffer
 * Prototype. Not needed atm.
*/
// void process_ring_buffer(CANBufferType &rx_buffer, unsigned long curr_millis) {
//   while (rx_buffer.available()) {
//     CAN_message_t recvd_msg;
//     uint8_t buf[sizeof(CAN_message_t)];
//     rx_buffer.pop_front(buf, sizeof(CAN_message_t));
//     memmove(&recvd_msg, buf, sizeof(recvd_msg));
//     switch (recvd_msg.id)
//     {
//     case 0:
//       /* code */
//       break;
    
//     default:
//       break;
//     }
//   }
// }

/**
 * Initialize all SPI devices
 * MCP ADCs
*/
void init_all_adcs() {

  SPI.begin();

  ADC1.init();
  ADC2.init();
  ADC3.init();

}

/**
 * Initialize all IIR filters
*/
void init_all_iir_filters() {
  for (int i = 0; i < TOTAL_THERMISTOR_COUNT; i++) {
    thermistor_iir[i].set_alpha(THERM_ALPHA);
  }
}

/* Prototype */
void parse_telem_CAN_msg(const CAN_message_t &RX_msg) {
  CAN_message_t rx_msg = RX_msg;
  switch (rx_msg.id) {
    case ID_MCU_STATUS:              
      mcu_status.load(rx_msg.buf);
      break;
  }
}

void update_all_CAN_msg() {

  sab_thermistors_1.set_thermistor3(thermistor_iir[THERM_3].get_prev_reading());
  sab_thermistors_1.set_thermistor4(thermistor_iir[THERM_4].get_prev_reading());
  sab_thermistors_1.set_thermistor5(thermistor_iir[THERM_5].get_prev_reading());
  sab_thermistors_1.set_thermistor6(thermistor_iir[THERM_6].get_prev_reading());

  sab_thermistors_2.set_thermistor7(thermistor_iir[THERM_7].get_prev_reading());
  sab_thermistors_2.set_thermistor8(thermistor_iir[THERM_8].get_prev_reading());
  sab_thermistors_2.set_thermistor9(thermistor_iir[THERM_9].get_prev_reading());

  sab_cb.set_pot3(ADC1.get().conversions[SUS_POT_3].raw);
  sab_cb.set_pot4(ADC2.get().conversions[SUS_POT_4].raw);
  sab_cb.set_RL_load_cell(ADC1.get().conversions[RL_LOAD_CELL].raw);
  sab_cb.set_RR_load_cell(ADC2.get().conversions[RR_LOAD_CELL].raw);

  tcu_status.set_shutdown_status(static_cast<uint16_t>(btn_pi_shutdown.isPressed()));

}

void send_sab_CAN_msg() {
  CAN_message_t msg;

  // Thermistors
  sab_thermistors_1.write(msg.buf);
  msg.id = ID_SAB_THERMISTORS_1;
  msg.len = sizeof(sab_thermistors_1);
  TELEM_CAN.write(msg);

  sab_thermistors_2.write(msg.buf);
  msg.id = ID_SAB_THERMISTORS_2;
  msg.len = sizeof(sab_thermistors_2);
  TELEM_CAN.write(msg);

  // Corner boards
  sab_cb.write(msg.buf);
  msg.id = ID_SAB_CB;
  msg.len = sizeof(sab_cb);
  TELEM_CAN.write(msg);

  // TCU status
  tcu_status.write(msg.buf);
  msg.id = ID_TCU_STATUS;
  msg.len = sizeof(tcu_status);
  TELEM_CAN.write(msg);

  // Vector Nav
  /**
   * So it actually cannot be here
   * Otherwise CAN die a little
  */
}

/**
 * Forward vector nav data onto CAN
*/
void send_CAN_vectornav() {

  send_CAN_vn_gps_time();
  send_CAN_vn_position();
  send_CAN_vn_accel();
  send_CAN_vn_ins_status();
  send_CAN_vn_uncomp_accel();
  send_CAN_vn_vel_body();
  send_CAN_vn_angular_rate();
  send_CAN_vn_yaw_pitch_roll();

}

void send_CAN_vn_gps_time() {
  if (timer_send_CAN_vn_gps_time.check()) {
    CAN_message_t msg;

    auto id = Pack_VN_GPS_TIME_hytech(&vn_time_gps, msg.buf, &msg.len, (uint8_t*) &msg.flags.extended);
    msg.id = id;
    TELEM_CAN.write(msg);
  }
}

void send_CAN_vn_position() {
  if (timer_send_CAN_vn_position.check()) {
    CAN_message_t msg;

    auto id = Pack_VN_LAT_LON_hytech(&vn_position, msg.buf, &msg.len, (uint8_t*) &msg.flags.extended);
    msg.id = id;
    TELEM_CAN.write(msg);
  }
}

void send_CAN_vn_accel() {
  if (timer_send_CAN_vn_accel.check()) {
    CAN_message_t msg;

    auto id = Pack_VN_LINEAR_ACCEL_hytech(&vn_accel, msg.buf, &msg.len, (uint8_t*) &msg.flags.extended);
    msg.id = id;
    TELEM_CAN.write(msg);
  }
}

void send_CAN_vn_ins_status() {
  if (timer_send_CAN_vn_ins_status.check()) {
    CAN_message_t msg;

    auto id = Pack_VN_STATUS_hytech(&vn_ins_status, msg.buf, &msg.len, (uint8_t*) &msg.flags.extended);
    msg.id = id;
    TELEM_CAN.write(msg);
  }
}

void send_CAN_vn_uncomp_accel() {
  if (timer_send_CAN_vn_uncomp_accel.check()) {
    CAN_message_t msg;

    auto id = Pack_VN_LINEAR_ACCEL_UNCOMP_hytech(&vn_uncomp_accel, msg.buf, &msg.len, (uint8_t*) &msg.flags.extended);
    msg.id = id;
    TELEM_CAN.write(msg);
  }
}

void send_CAN_vn_vel_body() {
  if (timer_send_CAN_vn_vel_body.check()) {
    CAN_message_t msg;

    auto id = Pack_VN_VEL_hytech(&vn_vel_body, msg.buf, &msg.len, (uint8_t*) &msg.flags.extended);
    msg.id = id;
    TELEM_CAN.write(msg);
  }
}

void send_CAN_vn_angular_rate() {
  if (timer_send_CAN_vn_angular_rate.check())
  {
    CAN_message_t msg;

    auto id = Pack_VN_ANGULAR_RATE_hytech(&vn_angular_rate, msg.buf, &msg.len, (uint8_t*) &msg.flags.extended);
    msg.id = id;
    TELEM_CAN.write(msg);
  }  
}

void send_CAN_vn_yaw_pitch_roll() {
  if (timer_send_CAN_vn_yaw_pitch_roll.check())
  {
    CAN_message_t msg;

    auto id = Pack_VN_YPR_hytech(&vn_YPR, msg.buf, &msg.len, (uint8_t*) &msg.flags.extended);
    msg.id = id;
    TELEM_CAN.write(msg);
  }  
}

/**
 * Tick interfaces
*/
void tick_all_interfaces(const SysTick_s &curr_tick) {

  TriggerBits_s t = curr_tick.triggers;

  // Serial.println("Sus tick ADC");
  if (t.trigger100) { // 100Hz
    ADC1.tick();
    ADC2.tick();
    ADC3.tick();

    // Filter thermistor readings
    for (int i = 0; i < TOTAL_THERMISTOR_COUNT; i++) {
      thermistor_iir[i].filtered_result(ADC3.get().conversions[i].raw);
    }
  }
  // Serial.println("Sus after tick ADC");

  if (t.trigger50) {  // 50Hz
    // telem_interface.tick(ADC1.get(), ADC2.get(), ADC3.get(), btn_pi_shutdown.isPressed(), thermistor_iir);

    update_all_CAN_msg();
    send_sab_CAN_msg();
  }



}

/**
 * Tick systems
*/
void tick_all_systems(const SysTick_s &curr_tick) {
  
}

void turnOffAsciiOutput() { // VNOFF

  char toSend[DEFAULT_WRITE_BUFFER_MIDIUM];

  #if VN_HAVE_SECURE_CRT
	size_t length = sprintf_s(toSend, size, "$VNWRG,06,%u,%u", ador, port);
	#else
	size_t length = sprintf(toSend, "$VNWRG,06,%u", vn::protocol::uart::VNOFF);
	#endif

  length += sprintf(toSend + length, "*XX\r\n");

  Serial2.print(toSend);
  Serial2.flush();

}

void configBinaryOutput(uint8_t binaryOutputNumber, uint8_t fields, uint16_t rateDivisor) {

  char toSend[DEFAULT_WRITE_BUFFER_LONG];

  bool commonField = fields & 0x01;   // Serial.printf("common: %d\n", commonField);
  bool timeField = fields & 0x02;     // Serial.printf("time: %d\n", timeField);
  bool imuField = fields & 0x04;      // Serial.printf("imu: %d\n", imuField);
  bool gpsField = fields & 0x08;      // Serial.printf("gps: %d\n", gpsField);
  bool attitudeField = fields & 0x10; // Serial.printf("attitude: %d\n", attitudeField);
  bool insField = fields & 0x20;      // Serial.printf("ins: %d\n", insField);
  bool gps2Field = fields & 0x40;     // Serial.printf("gps2: %d\n", gps2Field);

  // First determine which groups are present.
  uint16_t groups = 0;
  if (commonField)
    groups |= 0x0001;         // 0000 0000 0000 0001. common group selected
  if (timeField)
    groups |= 0x0002;
  if (imuField)
    groups |= 0x0004;         // 0000 0000 0000 0100. IMU group selected
  if (gpsField)
    groups |= 0x0008;         // 0000 0000 0000 1000. GNSS1 group selected
  if (attitudeField)
    groups |= 0x0010;
  if (insField)
    groups |= 0x0020;         // 0000 0000 0010 0000. INS group selected
  if (gps2Field)
    groups |= 0x0040;

  // groups = 0010 1101 = 2D

  #if VN_HAVE_SECURE_CRT
  int length = sprintf_s(toSend, sizeof(toSend), "$VNWRG,%u,%u,%u,%X", 74 + binaryOutputNumber, fields.asyncMode, fields.rateDivisor, groups);
  #else
  int length = sprintf(toSend, "$VNWRG,%u,%u,%u,%X", 74 + binaryOutputNumber, vn::protocol::uart::ASYNCMODE_PORT1, rateDivisor, groups); // serial1, 800/16=50Hz, 
  #endif

  if (commonField) {
    #if VN_HAVE_SECURE_CRT
    length += sprintf_s(toSend + length, sizeof(toSend) - length, ",%X", fields.commonField);
    #else
    if (binaryOutputNumber == 1) {
      length += sprintf(toSend + length, ",%X", vn::protocol::uart::COMMONGROUP_TIMEGPS |     // 0000 0000 0110 0010 = 00 62
                                                vn::protocol::uart::COMMONGROUP_ANGULARRATE | 
                                                vn::protocol::uart::COMMONGROUP_POSITION);
    }
    else if (binaryOutputNumber == 2) {
      length += sprintf(toSend + length, ",%X", vn::protocol::uart::COMMONGROUP_YAWPITCHROLL |
                                                vn::protocol::uart::COMMONGROUP_ACCEL |       // 0001 0001 0000 0000 = 11 00
                                                vn::protocol::uart::COMMONGROUP_INSSTATUS);
    }
    #endif
  }
  if (timeField) {
  	#if VN_HAVE_SECURE_CRT
  	length += sprintf_s(toSend + length, sizeof(toSend) - length, ",%X", fields.timeField);
  	#else
  	// length += sprintf(toSend + length, ",%X", fields.timeField);
  	#endif
  }
  if (imuField) {
  	#if VN_HAVE_SECURE_CRT
  	length += sprintf_s(toSend + length, sizeof(toSend) - length, ",%X", fields.imuField);
  	#else
  	length += sprintf(toSend + length, ",%X", vn::protocol::uart::IMUGROUP_UNCOMPACCEL |    // 0000 0000 1000 0100 = 00 84
                                              vn::protocol::uart::IMUGROUP_DELTAVEL);
  	#endif
  }
  if (gpsField) {
  	#if VN_HAVE_SECURE_CRT
  	length += sprintf_s(toSend + length, sizeof(toSend) - length, ",%X", fields.gpsField);
  	#else
  	length += sprintf(toSend + length, ",%X", vn::protocol::uart::GPSGROUP_POSECEF);        // 0000 0000 0100 0000 = 00 40
  	#endif
  }
  if (attitudeField) {
  	#if VN_HAVE_SECURE_CRT
  	length += sprintf_s(toSend + length, sizeof(toSend) - length, ",%X", fields.attitudeField);
  	#else
  	// length += sprintf(toSend + length, ",%X", fields.attitudeField);
  	#endif
  }
  if (insField) {
    #if VN_HAVE_SECURE_CRT
    length += sprintf_s(toSend + length, sizeof(toSend) - length, ",%X", fields.insField);
    #else
    length += sprintf(toSend + length, ",%X", vn::protocol::uart::INSGROUP_VELBODY);        // 0000 0000 0000 1000 = 00 08
    #endif
  }
  if(gps2Field) {
    #if VN_HAVE_SECURE_CRT
    length += sprintf_s(toSend + length, sizeof(toSend) - length, ",%X", fields.gps2Field);
    #else
    // length += sprintf(toSend + length, ",%X", fields.gps2Field);
    #endif
  }

  #if VN_HAVE_SECURE_CRT
  length += sprintf_s(toSend + length, sizeof(toSend) - length, "*");
  #else
  length += sprintf(toSend + length, "*");
  #endif

  length += sprintf(toSend + length, "XX\r\n");

  Serial2.print(toSend);
  Serial2.flush();

}

void pollUserConfiguredBinaryOutput(uint8_t binaryOutputNumber) {

  if (timer_vectornav_read_binary.check()) {

    char toSend[DEFAULT_WRITE_BUFFER_SIZE];

    size_t length = sprintf(toSend, "$VNBOM,%u*", binaryOutputNumber);
    length += sprintf(toSend + length, "XX\r\n");

    Serial2.print(toSend);
    Serial2.flush();

    timer_read_imu.reset();

    // delay(20);
    
    // while (Serial2.available()) {
    //   Serial.print(Serial2.read(), HEX);
    // }

    // Serial.println();

    // readPollingBinaryOutput();

  }

}

void readPollingBinaryOutput() {

  if (timer_read_imu.check()) {

    int index = 0;

    while (Serial2.available())
    {
      receiveBuffer[index++] = Serial2.read();
    }

    if (receiveBuffer[0] == 0xFA) {
      switch (receiveBuffer[1])
      {
        case 0x01:
          parseBinaryOutput_1();
          break;

        case 0x05:
          parseBinaryOutput_2();
          break;

        case 0x28:
          parseBinaryOutput_3();
          break;
        
        default:
          break;
      }
    }

  }

}

void parseBinaryOutput_1() {

  int binaryPacketLength = 1 + 1 + 2 * BINARY_OUTPUT_GROUP_COUNT_1 + BINARY_OUTPUT_PAYLOAD_1 + 2;  // in bytes: sync, groups, group fields, payload, crc

  uint8_t syncByte = receiveBuffer[0];
#if SANITY_CHECK
  if (syncByte != 0xFA)
    return;
#endif

  uint8_t groupsByte = receiveBuffer[1];
#if SANITY_CHECK
  if (groupsByte != 0x01)   // 0000 0001
    return;
#endif

  uint16_t groupField1 = ((uint16_t)receiveBuffer[3] << 8) | receiveBuffer[2];
#if SANITY_CHECK
  if (groupField1 != 0x0062)  // 0000 0000 0110 0010
    return;
#endif

  uint64_t timeGPS = (receiveBuffer[7 + OFFSET_PADDING_1] << (8 * 7)) | (receiveBuffer[6 + OFFSET_PADDING_1] << (8 * 6)) | 
                      (receiveBuffer[5 + OFFSET_PADDING_1] << (8 * 5)) | (receiveBuffer[4 + OFFSET_PADDING_1] << (8 * 4)) |
                      (receiveBuffer[3 + OFFSET_PADDING_1] << (8 * 3)) | (receiveBuffer[2 + OFFSET_PADDING_1] << (8 * 2)) | 
                      (receiveBuffer[1 + OFFSET_PADDING_1] << (8 * 1)) | receiveBuffer[0 + OFFSET_PADDING_1];

  float angularRateBodyX = (receiveBuffer[11 + OFFSET_PADDING_1] << (8 * 3)) | (receiveBuffer[10 + OFFSET_PADDING_1] << (8 * 2)) | 
                            (receiveBuffer[9 + OFFSET_PADDING_1] << (8 * 1)) | receiveBuffer[8 + OFFSET_PADDING_1];
  float angularRateBodyY = (receiveBuffer[15 + OFFSET_PADDING_1] << (8 * 3)) | (receiveBuffer[14 + OFFSET_PADDING_1] << (8 * 2)) | 
                            (receiveBuffer[13 + OFFSET_PADDING_1] << (8 * 1)) | receiveBuffer[12 + OFFSET_PADDING_1];
  float angularRateBodyZ = (receiveBuffer[19 + OFFSET_PADDING_1] << (8 * 3)) | (receiveBuffer[18 + OFFSET_PADDING_1] << (8 * 2)) | 
                            (receiveBuffer[17 + OFFSET_PADDING_1] << (8 * 1)) | receiveBuffer[16 + OFFSET_PADDING_1];

  double latitude = (receiveBuffer[27 + OFFSET_PADDING_1] << (8 * 7)) | (receiveBuffer[26 + OFFSET_PADDING_1] << (8 * 6)) | 
                    (receiveBuffer[25 + OFFSET_PADDING_1] << (8 * 5)) | (receiveBuffer[24 + OFFSET_PADDING_1] << (8 * 4)) |
                    (receiveBuffer[23 + OFFSET_PADDING_1] << (8 * 3)) | (receiveBuffer[22 + OFFSET_PADDING_1] << (8 * 2)) | 
                    (receiveBuffer[21 + OFFSET_PADDING_1] << (8 * 1)) | receiveBuffer[20 + OFFSET_PADDING_1];
  double longitude = (receiveBuffer[35 + OFFSET_PADDING_1] << (8 * 7)) | (receiveBuffer[34 + OFFSET_PADDING_1] << (8 * 6)) | 
                      (receiveBuffer[33 + OFFSET_PADDING_1] << (8 * 5)) | (receiveBuffer[32 + OFFSET_PADDING_1] << (8 * 4)) |
                      (receiveBuffer[31 + OFFSET_PADDING_1] << (8 * 3)) | (receiveBuffer[30 + OFFSET_PADDING_1] << (8 * 2)) | 
                      (receiveBuffer[29 + OFFSET_PADDING_1] << (8 * 1)) | receiveBuffer[28 + OFFSET_PADDING_1];
  double altitude = (receiveBuffer[43 + OFFSET_PADDING_1] << (8 * 7)) | (receiveBuffer[42 + OFFSET_PADDING_1] << (8 * 6)) | 
                    (receiveBuffer[41 + OFFSET_PADDING_1] << (8 * 5)) | (receiveBuffer[40 + OFFSET_PADDING_1] << (8 * 4)) |
                    (receiveBuffer[39 + OFFSET_PADDING_1] << (8 * 3)) | (receiveBuffer[38 + OFFSET_PADDING_1] << (8 * 2)) | 
                    (receiveBuffer[37 + OFFSET_PADDING_1] << (8 * 1)) | receiveBuffer[36 + OFFSET_PADDING_1];

  uint16_t crc = (receiveBuffer[45 + OFFSET_PADDING_1] << 8) | receiveBuffer[44 + OFFSET_PADDING_1];

  // Shove onto CAN
  vn_time_gps.vn_gps_time = timeGPS;  // uint64_t
  // Serial.println(timeGPS);

  vn_position.vn_gps_lat_ro = HYTECH_vn_gps_lat_ro_toS(latitude);  // uint32_t
  // Serial.println(HYTECH_vn_gps_lat_ro_toS(latitude));
  vn_position.vn_gps_lon_ro = HYTECH_vn_gps_lon_ro_toS(longitude);
  // Serial.println(HYTECH_vn_gps_lon_ro_toS(longitude));

  vn_angular_rate.angular_rate_x_ro = HYTECH_angular_rate_x_ro_toS(angularRateBodyX);
  // Serial.println(HYTECH_angular_rate_x_ro_toS(angularRateBodyX));
  vn_angular_rate.angular_rate_y_ro = HYTECH_angular_rate_y_ro_toS(angularRateBodyY);
  // Serial.println(HYTECH_angular_rate_y_ro_toS(angularRateBodyY));
  vn_angular_rate.angular_rate_z_ro = HYTECH_angular_rate_z_ro_toS(angularRateBodyZ);
  // Serial.println(HYTECH_angular_rate_z_ro_toS(angularRateBodyZ));

  currentPacketLength = binaryPacketLength;
  
  // clearReceiveBuffer();

}

void parseBinaryOutput_2() {

  int binaryPacketLength = 1 + 1 + 2 * BINARY_OUTPUT_GROUP_COUNT_2 + BINARY_OUTPUT_PAYLOAD_2 + 2;  // in bytes: sync, groups, group fields, payload, crc

  uint8_t syncByte = receiveBuffer[0];
#if SANITY_CHECK
  if (syncByte != 0xFA)
    return;
#endif

  uint8_t groupsByte = receiveBuffer[1];
#if SANITY_CHECK
  if (groupsByte != 0x05)   // 0000 0101
    return;
#endif

  uint16_t groupField1 = ((uint16_t)receiveBuffer[3] << 8) | receiveBuffer[2];
#if SANITY_CHECK
  if (groupField1 != 0x1100)    // 0001 0001 0000 0000
    return;
#endif

  uint16_t groupField2 = (receiveBuffer[5] << 8) | receiveBuffer[4];
#if SANITY_CHECK
  if (groupField2 != 0x0084)    // 0000 0000 1000 0100
    return;
#endif

  uint32_t yawBits = (((uint32_t)receiveBuffer[3 + OFFSET_PADDING_2_ORGINAL]) << (8 * 3)) | ((uint32_t)(receiveBuffer[2 + OFFSET_PADDING_2_ORGINAL]) << (8 * 2)) | 
                      (((uint32_t)receiveBuffer[1 + OFFSET_PADDING_2_ORGINAL]) << (8 * 1)) | ((uint32_t)receiveBuffer[0 + OFFSET_PADDING_2_ORGINAL]);
  float yaw;                    
  memcpy(&yaw, &yawBits, 4);
  Serial.printf("Yaw: %f ", yaw);

  uint32_t pitchBits = (receiveBuffer[7 + OFFSET_PADDING_2_ORGINAL] << (8 * 3)) | (receiveBuffer[6 + OFFSET_PADDING_2_ORGINAL] << (8 * 2)) | 
                      (receiveBuffer[5 + OFFSET_PADDING_2_ORGINAL] << (8 * 1)) | receiveBuffer[4 + OFFSET_PADDING_2_ORGINAL];
  float pitch;                    
  memcpy(&pitch, &pitchBits, 4);                    
  Serial.printf("Pitch: %f ", pitch);

  uint32_t rollBits = (receiveBuffer[11 + OFFSET_PADDING_2_ORGINAL] << (8 * 3)) | (receiveBuffer[10 + OFFSET_PADDING_2_ORGINAL] << (8 * 2)) | 
                      (receiveBuffer[9 + OFFSET_PADDING_2_ORGINAL] << (8 * 1)) | receiveBuffer[8 + OFFSET_PADDING_2_ORGINAL]; 
  float roll;                    
  memcpy(&roll, &rollBits, 4);  
  Serial.printf("Roll: %f ", roll);                    

  float accelBodyX = (receiveBuffer[3 + OFFSET_PADDING_2] << (8 * 3)) | (receiveBuffer[2 + OFFSET_PADDING_2] << (8 * 2)) | 
                      (receiveBuffer[1 + OFFSET_PADDING_2] << (8 * 1)) | receiveBuffer[0 + OFFSET_PADDING_2];
  float accelBodyY = (receiveBuffer[7 + OFFSET_PADDING_2] << (8 * 3)) | (receiveBuffer[6 + OFFSET_PADDING_2] << (8 * 2)) | 
                      (receiveBuffer[5 + OFFSET_PADDING_2] << (8 * 1)) | receiveBuffer[4 + OFFSET_PADDING_2];
  float accelBodyZ = (receiveBuffer[11 + OFFSET_PADDING_2] << (8 * 3)) | (receiveBuffer[10 + OFFSET_PADDING_2] << (8 * 2)) | 
                      (receiveBuffer[9 + OFFSET_PADDING_2] << (8 * 1)) | receiveBuffer[8 + OFFSET_PADDING_2];

  uint16_t InsStatus = (receiveBuffer[13 + OFFSET_PADDING_2] << 8) | receiveBuffer[12 + OFFSET_PADDING_2];
  // Serial.printf("Ins status: %X\n", InsStatus);

  float uncompAccelBodyX = (receiveBuffer[17 + OFFSET_PADDING_2] << (8 * 3)) | (receiveBuffer[16 + OFFSET_PADDING_2] << (8 * 2)) | 
                            (receiveBuffer[15 + OFFSET_PADDING_2] << (8 * 1)) | receiveBuffer[14 + OFFSET_PADDING_2];                  
  float uncompAccelBodyY = (receiveBuffer[21 + OFFSET_PADDING_2] << (8 * 3)) | (receiveBuffer[20 + OFFSET_PADDING_2] << (8 * 2)) | 
                            (receiveBuffer[19 + OFFSET_PADDING_2] << (8 * 1)) | receiveBuffer[18 + OFFSET_PADDING_2];
  float uncompAccelBodyZ = (receiveBuffer[25 + OFFSET_PADDING_2] << (8 * 3)) | (receiveBuffer[24 + OFFSET_PADDING_2] << (8 * 2)) | 
                           (receiveBuffer[23 + OFFSET_PADDING_2] << (8 * 1)) | receiveBuffer[22 + OFFSET_PADDING_2];
  Serial.printf("UncompAccelBodyZ: %f \n", uncompAccelBodyZ);

  float deltaVelX = (receiveBuffer[29 + OFFSET_PADDING_2] << (8 * 3)) | (receiveBuffer[28 + OFFSET_PADDING_2] << (8 * 2)) | 
                    (receiveBuffer[27 + OFFSET_PADDING_2] << (8 * 1)) | receiveBuffer[26 + OFFSET_PADDING_2];
  float deltaVelY = (receiveBuffer[33 + OFFSET_PADDING_2] << (8 * 3)) | (receiveBuffer[32 + OFFSET_PADDING_2] << (8 * 2)) | 
                    (receiveBuffer[31 + OFFSET_PADDING_2] << (8 * 1)) | receiveBuffer[30 + OFFSET_PADDING_2];
  float deltaVelZ = (receiveBuffer[37 + OFFSET_PADDING_2] << (8 * 3)) | (receiveBuffer[36 + OFFSET_PADDING_2] << (8 * 2)) | 
                    (receiveBuffer[35 + OFFSET_PADDING_2] << (8 * 1)) | receiveBuffer[34 + OFFSET_PADDING_2];

  uint16_t crc = (receiveBuffer[39 + OFFSET_PADDING_2] << 8) | receiveBuffer[38 + OFFSET_PADDING_2];

  vn_accel.vn_lin_ins_accel_x_ro = HYTECH_vn_lin_ins_accel_x_ro_toS(accelBodyX);  // int16_t
  vn_accel.vn_lin_ins_accel_y_ro = HYTECH_vn_lin_ins_accel_y_ro_toS(accelBodyY);
  vn_accel.vn_lin_ins_accel_z_ro = HYTECH_vn_lin_ins_accel_z_ro_toS(accelBodyZ);

  vn_ins_status.vn_gps_status = InsStatus;  // uint16_t

  vn_uncomp_accel.vn_lin_uncomp_accel_x_ro = HYTECH_vn_lin_uncomp_accel_x_ro_toS(uncompAccelBodyX);  // int16_t
  vn_uncomp_accel.vn_lin_uncomp_accel_y_ro = HYTECH_vn_lin_uncomp_accel_y_ro_toS(uncompAccelBodyY);
  vn_uncomp_accel.vn_lin_uncomp_accel_z_ro = uncompAccelBodyZ;

  vn_YPR.vn_yaw_ro = yaw;
  vn_YPR.vn_pitch_ro = pitch;
  vn_YPR.vn_roll_ro = roll;

  // Missing CAN message for deltaVel right now

  currentPacketLength = binaryPacketLength;

  // clearReceiveBuffer();

}

void parseBinaryOutput_3() {

  int binaryPacketLength = 1 + 1 + 2 * BINARY_OUTPUT_GROUP_COUNT_3 + BINARY_OUTPUT_PAYLOAD_3 + 2;  // in bytes: sync, groups, group fields, payload, crc

  uint8_t syncByte = receiveBuffer[0];
#if SANITY_CHECK
  if (syncByte != 0xFA)
    return;
#endif

  uint8_t groupsByte = receiveBuffer[1];
#if SANITY_CHECK
  if (groupsByte != 0x28)   // 0010 1000
    return;
#endif

  uint16_t groupField1 = ((uint16_t)receiveBuffer[3] << 8) | receiveBuffer[2];
#if SANITY_CHECK
  if (groupField1 != 0x0040)
    return;
#endif

  uint16_t groupField2 = (receiveBuffer[5] << 8) | receiveBuffer[4];
#if SANITY_CHECK
  if (groupField2 != 0x0008)
    return;
#endif

  double posEcef0 = (receiveBuffer[7 + OFFSET_PADDING_3] << (8 * 7)) | (receiveBuffer[6 + OFFSET_PADDING_3] << (8 * 6)) | 
                    (receiveBuffer[5 + OFFSET_PADDING_3] << (8 * 5)) | (receiveBuffer[4 + OFFSET_PADDING_3] << (8 * 4)) |
                    (receiveBuffer[3 + OFFSET_PADDING_3] << (8 * 3)) | (receiveBuffer[2 + OFFSET_PADDING_3] << (8 * 2)) | 
                    (receiveBuffer[1 + OFFSET_PADDING_3] << (8 * 1)) | receiveBuffer[0 + OFFSET_PADDING_3];
  double posEcef1 = (receiveBuffer[15 + OFFSET_PADDING_3] << (8 * 7)) | (receiveBuffer[14 + OFFSET_PADDING_3] << (8 * 6)) | 
                    (receiveBuffer[13 + OFFSET_PADDING_3] << (8 * 5)) | (receiveBuffer[12 + OFFSET_PADDING_3] << (8 * 4)) |
                    (receiveBuffer[11 + OFFSET_PADDING_3] << (8 * 3)) | (receiveBuffer[10 + OFFSET_PADDING_3] << (8 * 2)) | 
                    (receiveBuffer[9 + OFFSET_PADDING_3] << (8 * 1)) | receiveBuffer[8 + OFFSET_PADDING_3];
  double posEcef2 = (receiveBuffer[23 + OFFSET_PADDING_3] << (8 * 7)) | (receiveBuffer[22 + OFFSET_PADDING_3] << (8 * 6)) | 
                    (receiveBuffer[21 + OFFSET_PADDING_3] << (8 * 5)) | (receiveBuffer[20 + OFFSET_PADDING_3] << (8 * 4)) |
                    (receiveBuffer[19 + OFFSET_PADDING_3] << (8 * 3)) | (receiveBuffer[18 + OFFSET_PADDING_3] << (8 * 2)) | 
                    (receiveBuffer[17 + OFFSET_PADDING_3] << (8 * 1)) | receiveBuffer[16 + OFFSET_PADDING_3];

  float velBodyX = (receiveBuffer[27 + OFFSET_PADDING_3] << (8 * 3)) | (receiveBuffer[26 + OFFSET_PADDING_3] << (8 * 2)) | 
                    (receiveBuffer[25 + OFFSET_PADDING_3] << (8 * 1)) | receiveBuffer[24 + OFFSET_PADDING_3];
  float velBodyY = (receiveBuffer[31 + OFFSET_PADDING_3] << (8 * 3)) | (receiveBuffer[30 + OFFSET_PADDING_3] << (8 * 2)) | 
                    (receiveBuffer[29 + OFFSET_PADDING_3] << (8 * 1)) | receiveBuffer[28 + OFFSET_PADDING_3];
  float velBodyZ = (receiveBuffer[35 + OFFSET_PADDING_3] << (8 * 3)) | (receiveBuffer[34 + OFFSET_PADDING_3] << (8 * 2)) | 
                    (receiveBuffer[33 + OFFSET_PADDING_3] << (8 * 1)) | receiveBuffer[32 + OFFSET_PADDING_3];

  uint16_t crc = (receiveBuffer[37 + OFFSET_PADDING_3] << 8) | receiveBuffer[36 + OFFSET_PADDING_3];

  // Missing CAN message for PosEcef right now

  vn_vel_body.vn_body_vel_x_ro = HYTECH_vn_body_vel_x_ro_toS(velBodyX);  // int16_t
  vn_vel_body.vn_body_vel_y_ro = velBodyY;
  vn_vel_body.vn_body_vel_z_ro = velBodyZ;

  currentPacketLength = binaryPacketLength;

  // clearReceiveBuffer();

}

void clearReceiveBuffer() {

  for (int i = 0; i < DEFAULT_SERIAL_BUFFER_SIZE; i++)
  {
    receiveBuffer[i] = 0;
  }
  
}





