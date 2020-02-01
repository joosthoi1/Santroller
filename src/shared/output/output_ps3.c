#include "output_ps3.h"
#include "output_handler.h"
#include "usb/Descriptors.h"
#include <stdlib.h>
// Bindings to go from controller to ps3
static const uint8_t PROGMEM ps3ButtonBindings[] = {
    XBOX_Y,    XBOX_A,     XBOX_B,          XBOX_X,
    0xff,      0xff,       XBOX_LB,         XBOX_RB,
    XBOX_BACK, XBOX_START, XBOX_LEFT_STICK, XBOX_RIGHT_STICK,
    XBOX_HOME, XBOX_UNUSED};

static const uint8_t PROGMEM ps3AxisBindings[] = {
    XBOX_DPAD_UP, XBOX_DPAD_RIGHT, XBOX_DPAD_DOWN, XBOX_DPAD_LEFT, 0xFF,
    0xFF,         XBOX_LB,         XBOX_RB,        XBOX_Y,         XBOX_B,
    XBOX_A,       XBOX_X};
static const uint8_t PROGMEM ghAxisBindings[] = {
    XBOX_DPAD_LEFT, XBOX_DPAD_DOWN, XBOX_DPAD_RIGHT,
    XBOX_DPAD_UP,   XBOX_X,         XBOX_B};
// Annoyingly, axis bindings for GH guitars overlap. This is the set of axis
// that overlap the previous list.
static const uint8_t PROGMEM ghAxisBindings2[] = {0xff, XBOX_LB, XBOX_Y,
                                                  XBOX_A};
static const uint8_t PROGMEM hat_bindings[] = {
    0x08, 0x00, 0x04, 0x08, 0x06, 0x07, 0x05, 0x08, 0x02, 0x01, 0x03};
static const uint8_t *currentAxisBindings;
static uint8_t currentAxisBindingsLen = 0;

static uint8_t id[] = {0x21, 0x26, 0x01, 0x07, 0x00, 0x00, 0x00, 0x00};

void ps3_create_report(USB_ClassInfo_HID_Device_t *const HIDInterfaceInfo,
                       uint8_t *const ReportID, const uint8_t ReportType,
                       void *ReportData, uint16_t *const ReportSize) {

  USB_PS3Report_Data_t *JoystickReport = (USB_PS3Report_Data_t *)ReportData;
  uint8_t button;
  for (uint8_t i = 0; i < sizeof(ps3ButtonBindings); i++) {
    button = pgm_read_byte(ps3ButtonBindings + i);
    if (button == 0xff) continue;
    bool bit_set = bit_check(controller.buttons, button);
    bit_write(bit_set, JoystickReport->buttons, i);
  }
  if (config.main.sub_type == SWITCH_GAMEPAD_SUBTYPE) {
    // Swap a and b on the switch
    COPY(A, B);
    COPY(B, A);
  }
  for (uint8_t i = 0; i < currentAxisBindingsLen; i++) {
    button = pgm_read_byte(currentAxisBindings + i);
    if (button == 0xff) continue;
    bool bit_set = bit_check(controller.buttons, button);
    if (config.main.sub_type == PS3_GUITAR_GH_SUBTYPE &&
        i < sizeof(ghAxisBindings2)) {
      button = pgm_read_byte(ghAxisBindings2 + i);
      bit_set |= bit_check(controller.buttons, button);
    }
    JoystickReport->axis[i] = bit_set ? 0xFF : 0x00;
  }

  // Hat Switch
  button = controller.buttons & 0xF;
  JoystickReport->hat =
      button > 0x0a ? 0x08 : pgm_read_byte(hat_bindings + button);

  // Tilt / whammy
  bool tilt = controller.r_y == 32767;
  if (config.main.sub_type == PS3_GUITAR_GH_SUBTYPE) {
    JoystickReport->r_x = (controller.r_x >> 8) + 128;
    // GH PS3 guitars have a tilt axis
    JoystickReport->accel[0] = tilt ? 0x0184 : 0x01f7;
  }
  if (config.main.sub_type == PS3_GUITAR_RB_SUBTYPE) {
    JoystickReport->r_x = 128 - (controller.r_x >> 8);
    // RB PS3 guitars use R for a tilt bit
    bit_write(tilt, JoystickReport->buttons, SWITCH_R);
    // Swap y and x, as RB controllers have them inverted
    COPY(X, Y);
    COPY(Y, X);
  }
  if (config.main.sub_type == PS3_GUITAR_GH_SUBTYPE ||
      config.main.sub_type == PS3_GUITAR_RB_SUBTYPE) {
    // XINPUT guitars use LB for orange, PS3 uses L
    COPY(LB, L);
  }
  if (config.main.sub_type == PS3_DRUM_GH_SUBTYPE ||
      config.main.sub_type == PS3_DRUM_RB_SUBTYPE) {

    // XINPUT guitars use LB for orange, PS3 uses R
    COPY(LB, R);
    // XINPUT guitars use RB for bass, PS3 uses L
    COPY(RB, L);
  }
  if (config.main.sub_type == PS3_GAMEPAD_SUBTYPE ||
      config.main.sub_type == SWITCH_GAMEPAD_SUBTYPE) {
    bit_write(controller.lt > 50, JoystickReport->buttons, SWITCH_L);
    bit_write(controller.rt > 50, JoystickReport->buttons, SWITCH_R);
    JoystickReport->axis[4] = controller.lt;
    JoystickReport->axis[5] = controller.rt;
    JoystickReport->l_x = (controller.l_x >> 8) + 128;
    JoystickReport->l_y = (controller.l_y >> 8) + 128;
    JoystickReport->r_x = (controller.r_x >> 8) + 128;
    JoystickReport->r_y = (controller.r_y >> 8) + 128;
  } else {
    JoystickReport->l_x = 0x80;
    JoystickReport->l_y = 0x80;
    // r_y is tap, so lets disable it.
    JoystickReport->r_y = 0x7d;
  }
  *ReportSize = sizeof(USB_PS3Report_Data_t);
}
void ps3_control_request(void) {
  if (config.main.sub_type != SWITCH_GAMEPAD_SUBTYPE &&
      USB_ControlRequest.wIndex == interface.Config.InterfaceNumber) {
    if (USB_ControlRequest.bmRequestType ==
        (REQDIR_DEVICETOHOST | REQTYPE_CLASS | REQREC_INTERFACE)) {
      if (USB_ControlRequest.bRequest == HID_REQ_GetReport) {
        // Send out init packets for the ps3
        Endpoint_ClearSETUP();
        while (!(Endpoint_IsINReady()))
          ;
        for (uint8_t i = 0; i < sizeof(id); i++) { Endpoint_Write_8(id[i]); }
        Endpoint_ClearIN();
        Endpoint_ClearStatusStage();
        return;
      }
    }
  }
}
void ps3_init() {
  create_hid_report = ps3_create_report;
  control_request = ps3_control_request;
  if(config.main.sub_type > SWITCH_GAMEPAD_SUBTYPE) {
    currentAxisBindings = ps3AxisBindings;
    currentAxisBindingsLen = sizeof(ps3AxisBindings);
    if (config.main.sub_type > PS3_GAMEPAD_SUBTYPE) {
      currentAxisBindings = ghAxisBindings;
      currentAxisBindingsLen = sizeof(ghAxisBindings);
    }
    // Is the id stuff below actually important? check with a ps3 emulator.
    if (config.main.sub_type == PS3_GUITAR_GH_SUBTYPE) {
      id[3] = 0x06;
    } else if (config.main.sub_type == PS3_GUITAR_RB_SUBTYPE) {
      id[3] = 0x00;
    } else if (config.main.sub_type == PS3_DRUM_GH_SUBTYPE) {
      id[3] = 0x06;
    } else if (config.main.sub_type == PS3_DRUM_RB_SUBTYPE) {
      id[3] = 0x00;
    }
  }
}