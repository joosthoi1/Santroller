#pragma once
#include "../../../config/config.h"
#include "../Controller.h"
#include "Output.h"
#if OUTPUT_TYPE == XINPUT
#include "XInputOutput.h"
#elif OUTPUT_TYPE == KEYBOARD
#include "KeyboardOutput.h"
#endif
#include "../../util.h"
class OutputHandler {
public:
  static Output output;
  void process(Controller *controller);
  void init();
  bool ready();
};

extern "C" {
void EVENT_USB_Device_Connect(void);
void EVENT_USB_Device_Disconnect(void);
void EVENT_USB_Device_ConfigurationChanged(void);
void EVENT_USB_Device_ControlRequest(void);
void EVENT_USB_Device_StartOfFrame(void);
uint16_t get_descriptor(const uint8_t DescriptorType, const uint8_t DescriptorNumber, const void **const DescriptorAddress);
}