/*
 * @file uart.cpp
 * @brief ANEM16 UART peripheral implementation
 */

#include "uart.h"

void ANEMPeripheralUART::reset()
{
	ctrl_reg     = 0;
	status_reg   = UART_STATUS_TXRDY;
	baud_div     = 0;
	tx_state     = UartTxState::IDLE;
	tx_shift_reg = 0;
	tx_bit_count = 0;
	tx_tick_count = 0;
	rx_buffer    = 0;
	interrupt    = false;
}

data_t ANEMPeripheralUART::read(addr_t address)
{
	addr_t offset = address - baseAddress;

	switch (offset)
	{
	case 0: // DATA — read returns RX buffer, clears RXDA
	{
		data_t val = rx_buffer;
		status_reg &= ~UART_STATUS_RXDA;
		return val;
	}
	case 1: return ctrl_reg;
	case 2: return status_reg;
	case 3: return baud_div;
	default: return 0xFFFF;
	}
}

void ANEMPeripheralUART::write(addr_t address, data_t data)
{
	addr_t offset = address - baseAddress;

	switch (offset)
	{
	case 0: // DATA — TX: load shift reg if TXEN and TXRDY
		if ((ctrl_reg & UART_CTRL_TXEN) && (status_reg & UART_STATUS_TXRDY))
		{
			tx_shift_reg = (uint8_t)(data & 0xFF);
			status_reg &= ~UART_STATUS_TXRDY;
			tx_state = UartTxState::START;
			tx_tick_count = 0;
			tx_bit_count = 0;
		}
		break;
	case 1: // CTRL
		ctrl_reg = data;
		break;
	case 2: // STATUS — W1C for FE and OE only (TXRDY/RXDA are read-only)
		status_reg &= ~(data & (UART_STATUS_FE | UART_STATUS_OE));
		break;
	case 3: // BAUD
		baud_div = data;
		break;
	default:
		break;
	}
}

void ANEMPeripheralUART::clockCycle()
{
	// TX FSM
	uint16_t bit_period = getBitPeriod();

	switch (tx_state)
	{
	case UartTxState::START:
		tx_tick_count++;
		if (tx_tick_count >= bit_period)
		{
			tx_tick_count = 0;
			tx_state = UartTxState::DATA;
			tx_bit_count = 0;
		}
		break;

	case UartTxState::DATA:
		tx_tick_count++;
		if (tx_tick_count >= bit_period)
		{
			tx_tick_count = 0;
			tx_bit_count++;
			if (tx_bit_count >= 8)
			{
				tx_state = UartTxState::STOP;
			}
		}
		break;

	case UartTxState::STOP:
		tx_tick_count++;
		if (tx_tick_count >= bit_period)
		{
			tx_tick_count = 0;
			tx_state = UartTxState::IDLE;
			status_reg |= UART_STATUS_TXRDY;

			if (txCallback)
				txCallback(tx_shift_reg);
		}
		break;

	case UartTxState::IDLE:
		break;
	}

	// Compute interrupt (combinational)
	interrupt = ((status_reg & UART_STATUS_TXRDY) && (ctrl_reg & UART_CTRL_TXIE)) ||
	            ((status_reg & UART_STATUS_RXDA)  && (ctrl_reg & UART_CTRL_RXIE));
}

void ANEMPeripheralUART::injectRxByte(uint8_t byte)
{
	if (status_reg & UART_STATUS_RXDA)
		status_reg |= UART_STATUS_OE;  // Overrun

	rx_buffer = byte;
	status_reg |= UART_STATUS_RXDA;
}
