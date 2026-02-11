/*
 * periph.h
 *
 *  Created on: 14/12/2014
 *      Author: bruno
 */

#ifndef PERIPH_H_
#define PERIPH_H_

#include "types.h"
#include <string>

class ANEMMemMappedPeripheral
{
protected:
	unsigned int length = 0;
	addr_t baseAddress = 0;

public:
	virtual ~ANEMMemMappedPeripheral() {}

	virtual data_t read(addr_t address) { return 0xFFFF; }
	virtual void write(addr_t address, data_t data) { }
	virtual std::string getName() const { return "Unknown"; }

	virtual void reset() {}
	virtual void clockCycle() {}
	virtual bool getInterrupt() { return false; }

	unsigned int getLength(void) { return length; }

	void setBaseAddress(addr_t addr) { baseAddress = addr; }
	addr_t getBaseAddress(void) { return baseAddress; }
};



#endif /* PERIPH_H_ */
