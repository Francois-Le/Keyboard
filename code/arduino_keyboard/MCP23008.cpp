#include "MCP23008.hpp"

namespace {
const uint8_t MCP23008_ADDRESS  = 0x20;

/* MCP23008 registers */
const uint8_t IODIR             = 0x00;
const uint8_t IPOL              = 0x01;
const uint8_t GPINTEN           = 0x02;
const uint8_t DEFVAL            = 0x03;
const uint8_t INTCON            = 0x04;
const uint8_t IOCON             = 0x05;
const uint8_t GPPU              = 0x06;
const uint8_t INTF              = 0x07;
const uint8_t INTCAP            = 0x08;
const uint8_t GPIO              = 0x09;
const uint8_t OLAT              = 0x0A;
};

MCP23008::MCP23008 (uint8_t address )
        : i2c_address ( (MCP23008_ADDRESS | address) << 1 ) {
    if ( address > 7 )
        error ( "MCP23008::MCP23008: address is out of range, must be <= 7\n" );
}

void MCP23008::set_input_pins ( uint8_t pins ) {
    uint8_t value = read_register ( IODIR );
    write_register ( IODIR, value | pins );
}

void MCP23008::set_output_pins ( uint8_t pins ) {
    uint8_t value = read_register ( IODIR );
    write_register ( IODIR, value & ~pins );
}

void MCP23008::write_outputs ( uint8_t values ) {
    write_register ( GPIO, values );
}

uint8_t MCP23008::read_outputs () {
    return read_register ( OLAT );
}

uint8_t MCP23008::read_inputs () {
    return read_register ( GPIO );
}

void MCP23008::set_input_polarity ( uint8_t values ) {
    write_register ( IPOL, values );
}

uint8_t MCP23008::get_input_polarity () {
    return read_register ( IPOL );
}

void MCP23008::set_pullups ( uint8_t values ) {
    write_register ( GPPU, values );
}

uint8_t MCP23008::get_pullups () {
    return read_register ( GPPU );
}

void MCP23008::interrupt_on_changes ( uint8_t pins ) {
    uint8_t value = read_register ( INTCON );
    value &= ~pins;
    write_register ( INTCON, value );
    if (m_error) return;
    
    value = read_register ( GPINTEN );
    value |= pins;
    write_register ( GPINTEN, value );
}

void MCP23008::disable_interrupts ( uint8_t pins ) {
    uint8_t value = read_register ( GPINTEN );
    if (m_error) return;
    
    value &= ~pins;
    write_register ( GPINTEN, value );
}

void MCP23008::acknowledge_interrupt ( uint8_t &pin, uint8_t &values ) {
    pin = read_register ( INTF );
    if (m_error) return;
    
    values = read_register ( INTCAP );
}

uint8_t MCP23008::read_register ( uint8_t reg ) {
    char data[] = {reg};
    if ( 0 != i2c->write ( i2c_address, data, 1 ) )
    {
        //error ( "MCP23008::read_register: Missing ACK for write\n" );
        m_error = true;
        return 0;
    }
    
    if ( 0 != i2c->read ( i2c_address, data, 1 ) )
    {
        //error ( "MCP23008:read_register: Missing ACK for read\n" );
        m_error = true;
        return 0;
    }
    
    return data[0];
}

void MCP23008::write_register ( uint8_t reg, uint8_t value ) {
    char data[] = {reg, value};
    if ( 0 != i2c->write ( i2c_address, data, 2 ) )
    {
        //error ( "MCP23008::write_register: Missing ACK for write\n" );
        m_error = true;
        return;
    }
}

void MCP23008::write_mask ( uint8_t reg, uint8_t mask, bool value ) {
    uint8_t val;
    val = read_register ( reg );
    if (m_error) return;
    
    if ( value )
        val |= mask;
    else
        val &= ~mask;

    write_register ( reg, val );
}

void MCP23008::reset ( ) {
    m_error = false;
    write_register ( IODIR, 0xFF );
    if (m_error) return;
    
    for ( uint8_t reg = IPOL; reg <= OLAT; reg++ )
    {
        write_register ( reg, 0 );
        if (m_error) return;
    }
}
