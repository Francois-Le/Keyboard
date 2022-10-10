#include "mbed.h"
using namespace mbed;

// From https://os.mbed.com/users/dewyatt/code/MCP23008/docs/tip/classMCP23008.html

/** MCP23008 class
 *
 * Allow access to an I2C connected MCP23008 8-bit I/O extender chip
 *
 */
class MCP23008 {
public:
    enum Pin {
        Pin_GP0 = 0x01,
        Pin_GP1 = 0x02,
        Pin_GP2 = 0x04,
        Pin_GP3 = 0x08,
        Pin_GP4 = 0x10,
        Pin_GP5 = 0x20,
        Pin_GP6 = 0x40,
        Pin_GP7 = 0x80,
        Pin_All = 0xFF
    };
    
    /** Constructor
     *
     * @param sda I2C sda pin
     * @param scl I2C scl pin
     * @param address The hardware address of the MCP23008. This is the 3-bit
     * value that is physically set via A0, A1, and A2.
     * @param freq The I2C frequency. Should probably be 100KHz or 400KHz.
     */
    MCP23008 (uint8_t address);

    /** Set pins to input mode
     *
     * This function is used to set which pins are inputs (if any). Example:
     * set_inputs ( Pin_GP0 | Pin_GP1 | Pin_GP2 );
     * Note that these are set to input in addition to the previously set.
     * In other words, the following:
     * set_inputs ( Pin_GP1 );
     * set_inputs ( Pin_GP2 );
     * Results in at least two pins set to input.
     *
     * @param pins A bitmask of pins to set to input mode.
     */
    void set_input_pins ( uint8_t pins );
    /** Set pins to output mode
     *
     * This function is used to set which pins are outputs (if any). Example:
     * set_outputs ( Pin_GP0 | Pin_GP1 | Pin_GP2 );
     * Note that these are set to output in addition to the previously set.
     * In other words, the following:
     * set_outputs ( Pin_GP1 );
     * set_outputs ( Pin_GP2 );
     * Results in at least two pins set to output.
     *
     * @param pins A bitmask of pins to set to output mode.
     */
    void set_output_pins ( uint8_t pins );

    /** Write to the output pins.
     *
     * This function is used to set output pins on or off.
     *
     * @param values A bitmask indicating whether a pin should be on or off.
     */
    void write_outputs ( uint8_t values );
    /** Read back the outputs.
     *
     * This function is used to read the last values written to the output pins.
     *
     * @returns The value from the OLAT register.
     */
    uint8_t read_outputs ();

    /** Read from the input pins.
     *
     * This function is used to read the values from the input pins.
     *
     * @returns A bitmask of the current state of the input pins.
     */
    uint8_t read_inputs ();

    /** Set the input pin polarity.
     *
     * This function sets the polarity of the input pins.
     * A 1 bit is inverted polarity, a 0 is normal.
     *
     * @param values A bitmask of the input polarity.
     */
    void set_input_polarity ( uint8_t values );
    /** Read back the current input pin polarity.
     *
     * This function reads the current state of the input pin polarity.
     *
     * @returns The value from the IPOL register.
     */
    uint8_t get_input_polarity ();

    /** Enable and disable the internal pull-up resistors for input pins.
     *
     * This function enables the internal 100 kΩ pull-up resistors.
     * A 1 bit enables the pull-up resistor for the corresponding input pin.
     *
     * @param values A bitmask indicating which pull-up resistors should be enabled/disabled.
     */
    void set_pullups ( uint8_t values );
    /** Get the current state of the internal pull-up resistors.
     *
     * @returns The current state of the pull-up resistors.
     */
    uint8_t get_pullups ();

    /** Generate an interrupt when a pin changes.
     *
     * This function enables interrupt generation for the specified pins.
     * The interrupt is active-low by default.
     * The function acknowledge_interrupt must be called before another
     * interrupt will be generated.
     * Example:
     * @code
     * InterruptIn in ( p16 );
     * MCP23008 mcp ( p9, p10, 0 );
     * in.fall ( &interrupt );
     * mcp.interrupt_on_changes ( MCP23008::Pin_GP0 );
     * while ( 1 ) {
     *      wait ( 1 );
     * }
     * @endcode
     * 
     * @param pins A bitmask of the pins that may generate an interrupt.
     */
    void interrupt_on_changes ( uint8_t pins );
    /** Disables interrupts for the specified pins.
     *
     * @param values A bitmask indicating which interrupts should be disabled.
     */
    void disable_interrupts ( uint8_t pins );

    /** Acknowledge a generated interrupt.
     *
     * This function must be called when an interrupt is generated to discover
     * which pin caused the interrupt and to enable future interrupts.
     *
     * @param pin An output paramter that specifies which pin generated the interrupt.
     * @param values The current state of the input pins.
     */
    void acknowledge_interrupt ( uint8_t &pin, uint8_t &values );

    inline bool isError() {return m_error;}

    void setI2C(I2C* i) { i2c = i; }
    void reset ();

private:
    uint8_t read_register ( uint8_t reg );
    void write_register ( uint8_t reg, uint8_t value );
    void write_mask ( uint8_t reg, uint8_t mask, bool value );

    I2C* i2c;
    uint8_t i2c_address;
    bool m_error;
};
