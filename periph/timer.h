/*
 * @file timer.h
 * @brief ANEM16 Timer peripheral
 *
 * 16-bit timer with prescaler, compare match, and auto-reload.
 * Addresses: BASE+0 COUNT, BASE+1 CTRL, BASE+2 STATUS, BASE+3 COMPARE
 *
 * CTRL bits: EN(0), PSC[2:1] (00=/1, 01=/4, 10=/16, 11=/256), AR(3), OIE(4), CIE(5)
 * STATUS bits: OVF(0), CMF(1) — write-1-to-clear
 */

#ifndef PERIPH_TIMER_H_
#define PERIPH_TIMER_H_

#include "../periph.h"

#define TIMER_BASE_ADDR 0xFFD7

// CTRL register bits
#define TIMER_CTRL_EN   (1 << 0)
#define TIMER_CTRL_PSC0 (1 << 1)
#define TIMER_CTRL_PSC1 (1 << 2)
#define TIMER_CTRL_AR   (1 << 3)
#define TIMER_CTRL_OIE  (1 << 4)
#define TIMER_CTRL_CIE  (1 << 5)

// STATUS register bits
#define TIMER_STATUS_OVF (1 << 0)
#define TIMER_STATUS_CMF (1 << 1)

class ANEMPeripheralTimer : public ANEMMemMappedPeripheral
{
private:
	data_t count_reg   = 0;
	data_t ctrl_reg    = 0;
	data_t status_reg  = 0;
	data_t compare_reg = 0;
	data_t reload_val  = 0;
	uint8_t psc_counter = 0;

	bool interrupt = false;

	uint16_t getPrescalerTop() const;

public:
	ANEMPeripheralTimer() { this->length = 4; }

	data_t read(addr_t address) override;
	void write(addr_t address, data_t data) override;
	std::string getName() const override { return "Timer"; }

	void reset() override;
	void clockCycle() override;
	bool getInterrupt() override { return interrupt; }

	// Debug API
	data_t getCount() const { return count_reg; }
	data_t getCtrl() const { return ctrl_reg; }
	data_t getStatus() const { return status_reg; }
	data_t getCompare() const { return compare_reg; }
};

#endif /* PERIPH_TIMER_H_ */
