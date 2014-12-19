/*
 * mac.h
 *
 *  Created on: 18/12/2014
 *      Author: bruno
 */

#ifndef PERIPH_MAC_H_
#define PERIPH_MAC_H_

#include "../periph.h"

#define MAC_BASE_ADDR 0xFFD4
#define MAC_CONFIG_DEFAULT 0b0101000000000000
#define MAC_CONFIG_W_MASK 0b1010111100000111

#define MAC_CONFIG_MOK (1<<6)
#define MAC_CONFIG_INT (1<<7)
#define MAC_CONFIG_IE  (1<<11)
#define MAC_CONFIG_OVR (1<<5)
#define MAC_CONFIG_COUT (1<<3)
#define MAC_CONFIG_Z  (1<<4)
#define MAC_CONFIG_ACR (1<<10)
#define MAC_CONFIG_OPACC ((1<<15) | (1<<14))
#define MAC_CONFIG_OPMULT ((1<<13) | (1<<12))

enum MACSTATE {macWAIT, macMULT, macACC};
enum MACOPMULT {multU, multS, multNOP};
enum MACOPACC {accU, accS, accNOP};

class MAC_MULT
{
private:
	bool ready = false;
	int32_t data_out = 0x00000000;

	data_t opa_reg = 0, opb_reg = 0;
	MACOPMULT op_reg;

	void operate(MACOPMULT op, data_t opA, data_t opB);
public:
	bool dataReady(void) {return this->ready; }
	int32_t getResult(void) { return this->data_out; }

	void setInputs(MACOPMULT op, data_t opA, data_t opB) { this->op_reg = op; this->opa_reg = opA; this->opb_reg = opB; }
	void clockCycle(void);

	void reset(void) { this->ready = false; this->data_out = 0; }

};

class MAC_ACC
{
private:
	int32_t data_out = 0x00000000;
	bool ready = false;
	bool carry = false;
	bool ovr = false;
	bool z = false;

	int32_t data_in = 0x00000000;
	MACOPACC op_reg;

	void operate(MACOPACC op, int32_t data);
public:
	bool dataReady(void) { return this->ready; }
	bool overflow(void) { return this->ovr; }
	bool carryOut(void) { return this->carry; }
	bool zero(void) { return this->z; }
	int32_t getResult(void) { return this->data_out; }

	void setInputs(MACOPACC op, int32_t data) { this->op_reg = op; this->data_in = data; }
	void clockCyle(void);

	void reset(void) { this->ready = false; this->data_out = 0; }

};

class ANEMPeripheralMAC : public ANEMMemMappedPeripheral
{
private:
	data_t config_reg = 0x0000;
	data_t su_reg = 0x0000;
	data_t sl_reg = 0x0000;
	data_t a_reg = 0x0000, b_reg = 0x0000;

	int32_t mult_out = 0x00000000;

	//state machine
	MACSTATE state = macWAIT;

	//multiplier emulation
	MAC_MULT mult;
	MAC_ACC acc;

	bool interrupt = false;

public:
	ANEMPeripheralMAC(void) { this->length = 3; }

	using ANEMMemMappedPeripheral::read;
	data_t read(addr_t address);
	void  write(addr_t address, data_t data);

	void reset(void);
	void clockCycle(void);

	bool getInterrupt(void) { return this->interrupt; }

};



#endif /* PERIPH_MAC_H_ */
