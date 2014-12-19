/*
 * mac.cpp
 *
 *  Created on: 18/12/2014
 *      Author: bruno
 */

#include "mac.h"
#include <stdlib.h>

data_t ANEMPeripheralMAC::read(addr_t address)
{

	data_t retval;

	if (address == this->baseAddress)
	{

		retval =  this->config_reg;

		//clear flags
		this->config_reg &= ~MAC_CONFIG_INT;
		this->config_reg &= ~MAC_CONFIG_MOK;
		this->config_reg &= ~MAC_CONFIG_OVR;
		this->config_reg &= ~MAC_CONFIG_COUT;
		this->config_reg &= ~MAC_CONFIG_Z;


	}else if (address == (this->baseAddress + 1))
	{
		retval = this->su_reg;

	}else if (address == (this->baseAddress + 2))
	{

		retval = this->sl_reg;

	}else
	{

		//error
		retval = 0xFFFF;
	}

	return retval;

}

void ANEMPeripheralMAC::write(addr_t addr, data_t data)
{
	if (addr == this->baseAddress)
	{

		//config
		data &= MAC_CONFIG_W_MASK;
		this->config_reg = data;

	}else if (addr == (this->baseAddress + 1))
	{

		this->a_reg = data;


	}else if (addr == (this->baseAddress + 2))
	{

		this->b_reg = data;

		//multiplication init
		this->state = macMULT;

		switch(this->config_reg & MAC_CONFIG_OPMULT)
		{
		case 1:
			this->mult.setInputs(multU,this->a_reg,this->b_reg);
			break;
		case 3:
			this->mult.setInputs(multS,this->a_reg,this->b_reg);
			break;
		default:
			this->mult.setInputs(multNOP,0,0);
			break;
		}

	}else
	{

		//error

	}

}

void ANEMPeripheralMAC::reset(void)
{

	this->state = macWAIT;
	this->config_reg = MAC_CONFIG_DEFAULT;
	this->su_reg = 0x0000;
	this->sl_reg = 0x0000;

	this->mult.reset();
	this->acc.reset();

}

void ANEMPeripheralMAC::clockCycle(void)
{

	uint32_t acc_out;
	//state machine
	///@todo emulate behavior

	this->mult.clockCycle();
	this->acc.clockCyle();

	switch(this->state)
	{
	case macACC:

		if (this->acc.dataReady())
		{

			this->state = macWAIT;

			//flag generation
			///@todo check if this is the correct way to do it
			//this->config_reg = MAC_CONFIG_DEFAULT;

			if (this->acc.overflow())
			{
				this->config_reg |= MAC_CONFIG_OVR;
				this->config_reg &= ~MAC_CONFIG_MOK;
			}
			else this->config_reg |= MAC_CONFIG_MOK;

			if (this->acc.carryOut()) this->config_reg |= MAC_CONFIG_COUT;
			if (this->acc.zero()) this->config_reg |= MAC_CONFIG_Z;

			//interrupt
			if (this->config_reg & MAC_CONFIG_IE) this->config_reg |= MAC_CONFIG_INT;

			//get data
			acc_out = this->acc.getResult();
			this->su_reg = (data_t)((acc_out & 0xFFFF0000)>>16);
			this->sl_reg = (data_t)(acc_out & 0x0000FFFF);

		}

		break;
	case macMULT:

		if (this->mult.dataReady())
		{
			//multiplication ended
			this->mult_out = this->mult.getResult();

			//enable accumulator
			this->state = macACC;
			switch(this->config_reg & MAC_CONFIG_OPACC)
			{
			case 1:
				this->acc.setInputs(accU,this->mult_out);
				break;
			case 3:
				this->acc.setInputs(accS,this->mult_out);
				break;
			default:
				this->acc.setInputs(accNOP,0);
				break;
			}
			//disable mult
			this->mult.setInputs(multNOP,0,0);

		}

		break;
	case macWAIT:
		break;
	default:
		break;
	}


}

void MAC_MULT::operate(MACOPMULT op, data_t opA, data_t opB)
{

	switch(op)
	{
	case multU:
		this->data_out = abs((int32_t)((uint32_t)opA*(uint32_t)opB));
		this->ready = true;
		break;
	case multS:
		this->data_out = (int32_t)opA*(int32_t)opB;
		this->ready = true;
		break;

	case multNOP:
	default:
		this->ready = false;
		break;

	}

}

void MAC_ACC::operate(MACOPACC op, int32_t data)
{
	uint32_t d_op;

	switch(op)
	{
	case accU:
		d_op = (uint32_t)data + (uint32_t)this->data_out;

		this->carry = (bool)(d_op & 0xEFFFFFFF);
		this->ovr = false;

		//this->data_out = abs() ?

		this->z = (this->data_out == 0) ? true : false;
		this->ready = true;

		break;
	case accS:
		this->carry = false;

		///@todo overflow logic

		this->ready = true;
		break;
	case accNOP:
	default:
		this->ready = false;
		break;

	}

}

void MAC_ACC::clockCyle(void)
{

	this->operate(this->op_reg,this->data_in);

}

void MAC_MULT::clockCycle(void)
{

	this->operate(this->op_reg,this->opa_reg,this->opb_reg);

}

