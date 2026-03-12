/*
 * @file gdb_stub.h
 * @brief GDB Remote Serial Protocol stub for ANEM16 simulator
 */

#ifndef GDB_STUB_H_
#define GDB_STUB_H_

#include "debug_engine.h"
#include <string>

class GDBStub
{
private:
	DebugEngine& engine;
	int port;
	int listenFd = -1;
	int clientFd = -1;
	bool noAckMode = false;

	// Target description XML (loaded from target.xml)
	std::string targetXML;

	// Packet I/O
	std::string readPacket();
	void sendPacket(const std::string& data);
	void sendReply(const std::string& data);

	// Command dispatch
	std::string handleCommand(const std::string& pkt);

	// GDB RSP handlers
	std::string handleQuery(const std::string& cmd);
	std::string handleReadRegisters();
	std::string handleWriteRegisters(const std::string& data);
	std::string handleReadRegister(const std::string& cmd);
	std::string handleWriteRegister(const std::string& cmd);
	std::string handleReadMemory(const std::string& cmd);
	std::string handleWriteMemory(const std::string& cmd);
	std::string handleStep();
	std::string handleContinue();
	std::string handleBreakpoint(const std::string& cmd, bool insert);
	std::string handleVCommand(const std::string& cmd);
	std::string handleMonitor(const std::string& hexCmd);

	// Helpers
	static std::string hexEncode(const std::string& str);
	static std::string hexDecode(const std::string& hex);
	static std::string toHex8(uint8_t val);
	static std::string toHex16LE(uint16_t val);
	static uint16_t fromHex16LE(const std::string& hex);
	static uint8_t fromHex8(const std::string& hex);
	std::string stopReply(StopReason reason);

public:
	GDBStub(DebugEngine& engine, int port = 1234);
	~GDBStub();
	void run();
};

#endif /* GDB_STUB_H_ */
