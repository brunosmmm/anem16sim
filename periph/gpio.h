/*
 * @file gpio.h
 * @brief ANEM16 GPIO peripheral
 *
 * Two 16-bit ports (A, B) with per-bit direction control.
 * Addresses: BASE+0 PORTA_DATA, BASE+1 PORTA_DIR, BASE+2 PORTB_DATA, BASE+3 PORTB_DIR
 * Direction: 1=output, 0=input
 * Read: output bits return latch, input bits return external pins
 */

#ifndef PERIPH_GPIO_H_
#define PERIPH_GPIO_H_

#include "../periph.h"

#define GPIO_BASE_ADDR 0xFFD0

class ANEMPeripheralGPIO : public ANEMMemMappedPeripheral
{
private:
	data_t port_data[2] = {0, 0};
	data_t port_dir[2]  = {0, 0};
	data_t ext_pins[2]  = {0, 0};

public:
	ANEMPeripheralGPIO() { this->length = 4; }

	data_t read(addr_t address) override;
	void write(addr_t address, data_t data) override;
	std::string getName() const override { return "GPIO"; }

	void reset() override;
	void clockCycle() override {}
	bool getInterrupt() override { return false; }

	// Debug API
	void setExternalPins(int port, data_t value);
	data_t getExternalPins(int port) const;
	data_t getOutputLatch(int port) const;
	data_t getDirection(int port) const;
	data_t getReadback(int port) const;
};

#endif /* PERIPH_GPIO_H_ */
