/*
 * @file timer.cpp
 * @brief ANEM16 Timer peripheral implementation
 */

#include "timer.h"

void ANEMPeripheralTimer::reset()
{
	count_reg   = 0;
	ctrl_reg    = 0;
	status_reg  = 0;
	compare_reg = 0;
	reload_val  = 0;
	psc_counter = 0;
	interrupt   = false;
}

uint16_t ANEMPeripheralTimer::getPrescalerTop() const
{
	uint8_t psc = (ctrl_reg >> 1) & 0x03;
	switch (psc)
	{
	case 0: return 1;
	case 1: return 4;
	case 2: return 16;
	case 3: return 256;
	default: return 1;
	}
}

data_t ANEMPeripheralTimer::read(addr_t address)
{
	addr_t offset = address - baseAddress;

	switch (offset)
	{
	case 0: return count_reg;
	case 1: return ctrl_reg;
	case 2: return status_reg;
	case 3: return compare_reg;
	default: return 0xFFFF;
	}
}

void ANEMPeripheralTimer::write(addr_t address, data_t data)
{
	addr_t offset = address - baseAddress;

	switch (offset)
	{
	case 0: // COUNT — also sets reload value
		count_reg  = data;
		reload_val = data;
		break;
	case 1: // CTRL
		ctrl_reg = data;
		break;
	case 2: // STATUS — write-1-to-clear
		status_reg &= ~data;
		break;
	case 3: // COMPARE
		compare_reg = data;
		break;
	default:
		break;
	}
}

void ANEMPeripheralTimer::clockCycle()
{
	// Compute interrupt output (combinational, always visible)
	interrupt = ((status_reg & TIMER_STATUS_OVF) && (ctrl_reg & TIMER_CTRL_OIE)) ||
	            ((status_reg & TIMER_STATUS_CMF) && (ctrl_reg & TIMER_CTRL_CIE));

	// If not enabled, nothing else to do
	if (!(ctrl_reg & TIMER_CTRL_EN))
		return;

	// Increment prescaler
	psc_counter++;

	uint16_t psc_top = getPrescalerTop();
	if (psc_counter < psc_top)
		return;

	// Prescaler tick
	psc_counter = 0;

	// Check compare match (before incrementing)
	data_t next_count = count_reg + 1;
	if (next_count == compare_reg)
		status_reg |= TIMER_STATUS_CMF;

	// Check overflow
	if (count_reg == 0xFFFF)
	{
		status_reg |= TIMER_STATUS_OVF;

		if (ctrl_reg & TIMER_CTRL_AR)
			count_reg = reload_val;  // Auto-reload
		else
			ctrl_reg &= ~TIMER_CTRL_EN;  // One-shot: disable

		return;
	}

	count_reg = next_count;
}
