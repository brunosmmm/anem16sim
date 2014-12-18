/*
 * periph.h
 *
 *  Created on: 14/12/2014
 *      Author: bruno
 */

#ifndef PERIPH_H_
#define PERIPH_H_

#include "types.h"

class ANEMMemMappedPeripheral
{
protected:
	unsigned int length;
	addr_t baseAddress;

public:
	data_t read(addr_t address) { return 0xFFFF; }
	void write(addr_t address, data_t data) { }

	unsigned int getLength(void) { return length; }

	void setBaseAddress(addr_t addr) { baseAddress = addr; }
	addr_t getBaseAddress(void) { return baseAddress; }
};



#endif /* PERIPH_H_ */
