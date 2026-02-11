/*
 * @file uart.h
 * @brief ANEM16 UART peripheral
 *
 * Addresses: BASE+0 DATA, BASE+1 CTRL, BASE+2 STATUS, BASE+3 BAUD
 *
 * CTRL bits: TXEN(0), RXEN(1), TXIE(2), RXIE(3)
 * STATUS bits: TXRDY(0, R), RXDA(1, R), FE(2, W1C), OE(3, W1C)
 *
 * TX: full bit-serial FSM (IDLE→START→DATA×8→STOP→IDLE)
 * RX: byte injection via injectRxByte() (full RX FSM deferred)
 */

#ifndef PERIPH_UART_H_
#define PERIPH_UART_H_

#include "../periph.h"
#include <functional>

#define UART_BASE_ADDR 0xFFDB

// CTRL register bits
#define UART_CTRL_TXEN (1 << 0)
#define UART_CTRL_RXEN (1 << 1)
#define UART_CTRL_TXIE (1 << 2)
#define UART_CTRL_RXIE (1 << 3)

// STATUS register bits
#define UART_STATUS_TXRDY (1 << 0)
#define UART_STATUS_RXDA  (1 << 1)
#define UART_STATUS_FE    (1 << 2)
#define UART_STATUS_OE    (1 << 3)

enum class UartTxState { IDLE, START, DATA, STOP };

class ANEMPeripheralUART : public ANEMMemMappedPeripheral
{
private:
	data_t ctrl_reg   = 0;
	data_t status_reg = UART_STATUS_TXRDY;
	data_t baud_div   = 0;

	// TX FSM
	UartTxState tx_state = UartTxState::IDLE;
	uint8_t tx_shift_reg = 0;
	uint8_t tx_bit_count = 0;
	uint16_t tx_tick_count = 0;

	// RX buffer
	data_t rx_buffer = 0;

	bool interrupt = false;

	std::function<void(uint8_t)> txCallback;

	uint16_t getBitPeriod() const { return 16 * (baud_div + 1); }

public:
	ANEMPeripheralUART() { this->length = 4; }

	data_t read(addr_t address) override;
	void write(addr_t address, data_t data) override;
	std::string getName() const override { return "UART"; }

	void reset() override;
	void clockCycle() override;
	bool getInterrupt() override { return interrupt; }

	// Console I/O
	void setTxCallback(std::function<void(uint8_t)> fn) { txCallback = std::move(fn); }

	// Debug API — inject byte into RX buffer
	void injectRxByte(uint8_t byte);

	// Debug inspection
	data_t getCtrl() const { return ctrl_reg; }
	data_t getStatusReg() const { return status_reg; }
	data_t getBaudDiv() const { return baud_div; }
	UartTxState getTxState() const { return tx_state; }
};

#endif /* PERIPH_UART_H_ */
