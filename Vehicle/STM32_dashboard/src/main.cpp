#include "main.h"

// initialize/construct singleton object(this was a nightmare to setup and
// can probably be replaced at some point with traditional class)
hytech_dashboard* hytech_dashboard::_instance = NULL;
// get instance and set to dashboard pointer for easier reference
hytech_dashboard* dashboard = hytech_dashboard::getInstance();

// DO NOT MOVE THIS!!! VVVVVVV has conflict with STM32_CAN for some reason
Dashboard_Controls dash_controls = Dashboard_Controls();

//Create STM32_CAN object to pass to DashboardCAN
// ALT because we are using pins PB 8/9
STM32_CAN stm_can( CAN1, ALT);
CAN_message_t msg;
//Create dashboard_can object
DashboardCAN dashboard_can(&stm_can);


void setup(void)
{

  // set non-needed Display pins low
  pinMode(PC5, OUTPUT);
  pinMode(PB1, OUTPUT);
  pinMode(PA3, OUTPUT);
  digitalWrite(PC5, LOW);
  digitalWrite(PB1, LOW);
  
  // set LED high so we know code is running
  digitalWrite(PA3, HIGH);

  // begin usb serial for STM32
  SerialUSB.begin();

  SerialUSB.println("HELLO");

  // run startup sequence for dasboard
  dashboard->startup();

  // initialize buttons and other controls
  dash_controls.startup();
}

void loop(void) {
  // read can messages from CAN bus
  dashboard_can.read_CAN();

  // update controls (buttons, knobs, buzzer etc.)
  dash_controls.update(&dashboard_can);

  // send dashboard status message
  dashboard_can.send_status();
  
  // refresh dashboard (display and neopixels)
  dashboard->refresh((DashboardCAN*) &dashboard_can);
}

