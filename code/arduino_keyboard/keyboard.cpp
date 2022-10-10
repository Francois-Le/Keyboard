#include "keyboard.h"

#include "stdint.h"

#include "Keyboard.h"
#include "usb_phy_api.h"

#define REPORT_ID_KEYBOARD 1
#define REPORT_ID_VOLUME   3

using namespace arduino;


Keyboard::Keyboard(uint16_t vendor_id, uint16_t product_id, uint16_t product_release):
  USBHID(0, 0, vendor_id, product_id, product_release)
{
}

Keyboard::~Keyboard()
{
}


void Keyboard::press(uint8_t* keys, uint8_t modifiers, uint8_t mediaKey)
{
  HID_REPORT report;
  report.data[0] = REPORT_ID_KEYBOARD;
  report.data[1] = modifiers;
  report.data[2] = 0;
  report.data[3] = keys[0];
  report.data[4] = keys[1];
  report.data[5] = keys[2];
  report.data[6] = keys[3];
  report.data[7] = keys[4];
  report.data[8] = keys[5];
  report.length = 9;
  send(&report);

  report.data[0] = REPORT_ID_VOLUME;
  report.data[1] = mediaKey;
  report.length = 2;
  send(&report);
}

void Keyboard::releaseAll()
{
  HID_REPORT report;
  report.data[0] = REPORT_ID_KEYBOARD;
  report.data[1] = 0;
  report.data[2] = 0;
  report.data[3] = 0;
  report.data[4] = 0;
  report.data[5] = 0;
  report.data[6] = 0;
  report.data[7] = 0;
  report.data[8] = 0;
  report.length = 9;

  send(&report);
}

const uint8_t *Keyboard::report_desc()
{
  static const uint8_t reportDescriptor[] = {
    USAGE_PAGE(1), 0x01,                    // Generic Desktop
    USAGE(1), 0x06,                         // Keyboard
    COLLECTION(1), 0x01,                    // Application
    REPORT_ID(1),       REPORT_ID_KEYBOARD,

    USAGE_PAGE(1), 0x07,                    // Key Codes
    USAGE_MINIMUM(1), 0xE0,
    USAGE_MAXIMUM(1), 0xE7,
    LOGICAL_MINIMUM(1), 0x00,
    LOGICAL_MAXIMUM(1), 0x01,
    REPORT_SIZE(1), 0x01,
    REPORT_COUNT(1), 0x08,
    INPUT(1), 0x02,                         // Data, Variable, Absolute
    REPORT_COUNT(1), 0x01,
    REPORT_SIZE(1), 0x08,
    INPUT(1), 0x01,                         // Constant


    REPORT_COUNT(1), 0x05,
    REPORT_SIZE(1), 0x01,
    USAGE_PAGE(1), 0x08,                    // LEDs
    USAGE_MINIMUM(1), 0x01,
    USAGE_MAXIMUM(1), 0x05,
    OUTPUT(1), 0x02,                        // Data, Variable, Absolute
    REPORT_COUNT(1), 0x01,
    REPORT_SIZE(1), 0x03,
    OUTPUT(1), 0x01,                        // Constant


    REPORT_COUNT(1), 0x06,
    REPORT_SIZE(1), 0x08,
    LOGICAL_MINIMUM(1), 0x00,
    LOGICAL_MAXIMUM(1), 0x65,
    USAGE_PAGE(1), 0x07,                    // Key Codes
    USAGE_MINIMUM(1), 0x00,
    USAGE_MAXIMUM(1), 0x65,
    INPUT(1), 0x00,                         // Data, Array
    END_COLLECTION(0),

    // Media Control
    USAGE_PAGE(1), 0x0C,
    USAGE(1), 0x01,
    COLLECTION(1), 0x01,
    REPORT_ID(1), REPORT_ID_VOLUME,
    USAGE_PAGE(1), 0x0C,
    LOGICAL_MINIMUM(1), 0x00,
    LOGICAL_MAXIMUM(1), 0x01,
    REPORT_SIZE(1), 0x01,
    REPORT_COUNT(1), 0x07,
    USAGE(1), 0xB5,             // Next Track
    USAGE(1), 0xB6,             // Previous Track
    USAGE(1), 0xB7,             // Stop
    USAGE(1), 0xCD,             // Play / Pause
    USAGE(1), 0xE2,             // Mute
    USAGE(1), 0xE9,             // Volume Up
    USAGE(1), 0xEA,             // Volume Down
    INPUT(1), 0x02,             // Input (Data, Variable, Absolute)
    REPORT_COUNT(1), 0x01,
    INPUT(1), 0x01,
    END_COLLECTION(0),
  };
  reportLength = sizeof(reportDescriptor);
  return reportDescriptor;
}

#define DEFAULT_CONFIGURATION (1)
#define TOTAL_DESCRIPTOR_LENGTH ((1 * CONFIGURATION_DESCRIPTOR_LENGTH) \
                                 + (1 * INTERFACE_DESCRIPTOR_LENGTH) \
                                 + (1 * HID_DESCRIPTOR_LENGTH) \
                                 + (2 * ENDPOINT_DESCRIPTOR_LENGTH))

const uint8_t *Keyboard::configuration_desc(uint8_t index)
{
  if (index != 0) {
    return NULL;
  }
  uint8_t configuration_descriptor_temp[] = {
    CONFIGURATION_DESCRIPTOR_LENGTH,    // bLength
    CONFIGURATION_DESCRIPTOR,           // bDescriptorType
    LSB(TOTAL_DESCRIPTOR_LENGTH),       // wTotalLength (LSB)
    MSB(TOTAL_DESCRIPTOR_LENGTH),       // wTotalLength (MSB)
    0x01,                               // bNumInterfaces
    DEFAULT_CONFIGURATION,              // bConfigurationValue
    0x00,                               // iConfiguration
    C_RESERVED | C_SELF_POWERED,        // bmAttributes
    C_POWER(0),                         // bMaxPower

    INTERFACE_DESCRIPTOR_LENGTH,        // bLength
    INTERFACE_DESCRIPTOR,               // bDescriptorType
    0x00,                               // bInterfaceNumber
    0x00,                               // bAlternateSetting
    0x02,                               // bNumEndpoints
    HID_CLASS,                          // bInterfaceClass
    HID_SUBCLASS_BOOT,                  // bInterfaceSubClass
    HID_PROTOCOL_KEYBOARD,              // bInterfaceProtocol
    0x00,                               // iInterface

    HID_DESCRIPTOR_LENGTH,              // bLength
    HID_DESCRIPTOR,                     // bDescriptorType
    LSB(HID_VERSION_1_11),              // bcdHID (LSB)
    MSB(HID_VERSION_1_11),              // bcdHID (MSB)
    0x00,                               // bCountryCode
    0x01,                               // bNumDescriptors
    REPORT_DESCRIPTOR,                  // bDescriptorType
    (uint8_t)(LSB(report_desc_length())), // wDescriptorLength (LSB)
    (uint8_t)(MSB(report_desc_length())), // wDescriptorLength (MSB)

    ENDPOINT_DESCRIPTOR_LENGTH,         // bLength
    ENDPOINT_DESCRIPTOR,                // bDescriptorType
    _int_in,                            // bEndpointAddress
    E_INTERRUPT,                        // bmAttributes
    LSB(MAX_HID_REPORT_SIZE),           // wMaxPacketSize (LSB)
    MSB(MAX_HID_REPORT_SIZE),           // wMaxPacketSize (MSB)
    1,                                  // bInterval (milliseconds)

    ENDPOINT_DESCRIPTOR_LENGTH,         // bLength
    ENDPOINT_DESCRIPTOR,                // bDescriptorType
    _int_out,                           // bEndpointAddress
    E_INTERRUPT,                        // bmAttributes
    LSB(MAX_HID_REPORT_SIZE),           // wMaxPacketSize (LSB)
    MSB(MAX_HID_REPORT_SIZE),           // wMaxPacketSize (MSB)
    1,                                  // bInterval (milliseconds)
  };
  MBED_ASSERT(sizeof(configuration_descriptor_temp) == sizeof(_configuration_descriptor));
  memcpy(_configuration_descriptor, configuration_descriptor_temp, sizeof(_configuration_descriptor));
  return _configuration_descriptor;
}
