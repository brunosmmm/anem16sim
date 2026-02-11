/*
 * @file gpio.cpp
 * @brief ANEM16 GPIO peripheral implementation
 */

#include "gpio.h"

void ANEMPeripheralGPIO::reset()
{
	port_data[0] = port_data[1] = 0;
	port_dir[0]  = port_dir[1]  = 0;
	ext_pins[0]  = ext_pins[1]  = 0;
}

data_t ANEMPeripheralGPIO::read(addr_t address)
{
	addr_t offset = address - baseAddress;

	switch (offset)
	{
	case 0: // PORTA_DATA
		return (port_data[0] & port_dir[0]) | (ext_pins[0] & ~port_dir[0]);
	case 1: // PORTA_DIR
		return port_dir[0];
	case 2: // PORTB_DATA
		return (port_data[1] & port_dir[1]) | (ext_pins[1] & ~port_dir[1]);
	case 3: // PORTB_DIR
		return port_dir[1];
	default:
		return 0xFFFF;
	}
}

void ANEMPeripheralGPIO::write(addr_t address, data_t data)
{
	addr_t offset = address - baseAddress;

	switch (offset)
	{
	case 0: port_data[0] = data; break;
	case 1: port_dir[0]  = data; break;
	case 2: port_data[1] = data; break;
	case 3: port_dir[1]  = data; break;
	default: break;
	}
}

void ANEMPeripheralGPIO::setExternalPins(int port, data_t value)
{
	if (port >= 0 && port <= 1)
		ext_pins[port] = value;
}

data_t ANEMPeripheralGPIO::getExternalPins(int port) const
{
	if (port >= 0 && port <= 1)
		return ext_pins[port];
	return 0;
}

data_t ANEMPeripheralGPIO::getOutputLatch(int port) const
{
	if (port >= 0 && port <= 1)
		return port_data[port];
	return 0;
}

data_t ANEMPeripheralGPIO::getDirection(int port) const
{
	if (port >= 0 && port <= 1)
		return port_dir[port];
	return 0;
}

data_t ANEMPeripheralGPIO::getReadback(int port) const
{
	if (port >= 0 && port <= 1)
		return (port_data[port] & port_dir[port]) | (ext_pins[port] & ~port_dir[port]);
	return 0;
}
