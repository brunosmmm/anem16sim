/*
 * @file gdb_stub.cpp
 * @brief GDB Remote Serial Protocol stub for ANEM16 simulator
 *
 * Implements enough of the GDB RSP to support:
 *   - Register read/write (g/G/p/P)
 *   - Memory read/write (m/M)
 *   - Single step (s) and continue (c)
 *   - Software breakpoints (Z0/z0)
 *   - Target description XML (qXfer)
 *   - No-ack mode (QStartNoAckMode)
 *
 * Register layout (18 x 16-bit, transmitted as little-endian hex):
 *   0-15: R0..R15 (GPR)
 *   16:   PC  (byte address)
 *   17:   SP  (byte address)
 */

#include "gdb_stub.h"
#include <iostream>
#include <cstring>
#include <cerrno>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>

// ---------------------------------------------------------------------------
// Target description XML — tells GDB our register layout
// ---------------------------------------------------------------------------
// Target XML loaded from file at startup

// ---------------------------------------------------------------------------
// Hex helpers
// ---------------------------------------------------------------------------
static const char hexchars[] = "0123456789abcdef";

std::string GDBStub::toHex8(uint8_t val)
{
	std::string s(2, '0');
	s[0] = hexchars[(val >> 4) & 0xF];
	s[1] = hexchars[val & 0xF];
	return s;
}

std::string GDBStub::toHex16LE(uint16_t val)
{
	// Little-endian: low byte first
	return toHex8(val & 0xFF) + toHex8((val >> 8) & 0xFF);
}

static int hexVal(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return 0;
}

uint8_t GDBStub::fromHex8(const std::string& hex)
{
	if (hex.size() < 2) return 0;
	return (hexVal(hex[0]) << 4) | hexVal(hex[1]);
}

uint16_t GDBStub::fromHex16LE(const std::string& hex)
{
	if (hex.size() < 4) return 0;
	uint8_t lo = fromHex8(hex.substr(0, 2));
	uint8_t hi = fromHex8(hex.substr(2, 2));
	return (uint16_t)hi << 8 | lo;
}

std::string GDBStub::hexEncode(const std::string& str)
{
	std::string result;
	result.reserve(str.size() * 2);
	for (char c : str) {
		result += hexchars[(uint8_t)c >> 4];
		result += hexchars[(uint8_t)c & 0xF];
	}
	return result;
}

std::string GDBStub::hexDecode(const std::string& hex)
{
	std::string result;
	result.reserve(hex.size() / 2);
	for (size_t i = 0; i + 1 < hex.size(); i += 2)
		result += (char)((hexVal(hex[i]) << 4) | hexVal(hex[i + 1]));
	return result;
}

// ---------------------------------------------------------------------------
// Packet I/O
// ---------------------------------------------------------------------------
static uint8_t computeChecksum(const std::string& data)
{
	uint8_t sum = 0;
	for (char c : data)
		sum += (uint8_t)c;
	return sum;
}

std::string GDBStub::readPacket()
{
	// Read until we get a '$' ... '#' xx packet
	// Handle '+'/'-' ack bytes and Ctrl-C (0x03)
	std::string buf;
	enum { WAIT_START, IN_BODY, CHECKSUM1, CHECKSUM2 } state = WAIT_START;
	char csHex[2] = {};

	while (true) {
		char c;
		ssize_t n = recv(clientFd, &c, 1, 0);
		if (n <= 0)
			return ""; // Connection closed

		switch (state) {
		case WAIT_START:
			if (c == '$') {
				buf.clear();
				state = IN_BODY;
			} else if (c == '+' || c == '-') {
				// Ack/nack — ignore
			} else if (c == 0x03) {
				// Ctrl-C — interrupt
				return "\x03";
			}
			break;
		case IN_BODY:
			if (c == '#')
				state = CHECKSUM1;
			else
				buf += c;
			break;
		case CHECKSUM1:
			csHex[0] = c;
			state = CHECKSUM2;
			break;
		case CHECKSUM2: {
			csHex[1] = c;
			uint8_t expected = (hexVal(csHex[0]) << 4) | hexVal(csHex[1]);
			uint8_t actual = computeChecksum(buf);
			if (!noAckMode) {
				if (expected == actual)
					send(clientFd, "+", 1, 0);
				else
					send(clientFd, "-", 1, 0);
			}
			if (expected == actual)
				return buf;
			// Bad checksum — wait for retransmit
			state = WAIT_START;
			buf.clear();
			break;
		}
		}
	}
}

void GDBStub::sendPacket(const std::string& data)
{
	uint8_t cs = computeChecksum(data);
	std::string pkt = "$" + data + "#" + toHex8(cs);
	send(clientFd, pkt.c_str(), pkt.size(), 0);
}

void GDBStub::sendReply(const std::string& data)
{
	sendPacket(data);
}

// ---------------------------------------------------------------------------
// Stop reply
// ---------------------------------------------------------------------------
std::string GDBStub::stopReply(StopReason reason)
{
	switch (reason) {
	case StopReason::Breakpoint:
		return "T05swbreak:;"; // SIGTRAP with swbreak
	case StopReason::Halted:
		return "W00"; // Process exited with status 0
	default:
		return "T05"; // SIGTRAP (generic stop)
	}
}

// ---------------------------------------------------------------------------
// Address conversion: ANEM16 is word-addressed, GDB uses byte addresses
// ---------------------------------------------------------------------------
static uint32_t wordToByte(uint16_t wordAddr) { return (uint32_t)wordAddr * 2; }
static addr_t byteToWord(uint32_t byteAddr) { return (addr_t)(byteAddr / 2); }

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------
std::string GDBStub::handleReadRegisters()
{
	auto regs = engine.getRegisters();
	std::string result;
	for (int i = 0; i < 16; i++)
		result += toHex16LE(regs.gpr[i]);
	result += toHex16LE((uint16_t)wordToByte(regs.pc));  // PC: word → byte
	result += toHex16LE((uint16_t)wordToByte(regs.sp));   // SP: word → byte
	return result;
}

std::string GDBStub::handleWriteRegisters(const std::string& data)
{
	// Not fully supported — we'd need setters for all GPRs
	// For now, just acknowledge
	return "OK";
}

std::string GDBStub::handleReadRegister(const std::string& cmd)
{
	unsigned int regnum = 0;
	sscanf(cmd.c_str(), "%x", &regnum);

	auto regs = engine.getRegisters();
	if (regnum < 16)
		return toHex16LE(regs.gpr[regnum]);
	else if (regnum == 16)
		return toHex16LE((uint16_t)wordToByte(regs.pc));
	else if (regnum == 17)
		return toHex16LE((uint16_t)wordToByte(regs.sp));
	else
		return "E00"; // Bad register
}

std::string GDBStub::handleWriteRegister(const std::string& cmd)
{
	// P<regnum>=<hexvalue>
	auto eqPos = cmd.find('=');
	if (eqPos == std::string::npos)
		return "E01";

	unsigned int regnum = 0;
	sscanf(cmd.c_str(), "%x", &regnum);
	uint16_t val = fromHex16LE(cmd.substr(eqPos + 1));

	if (regnum < 16) {
		engine.writeRegister(regnum, val);
	} else if (regnum == 16) {
		// PC: byte address → word address
		engine.setPC(byteToWord(val));
	} else if (regnum == 17) {
		// SP: byte address → word address — not yet writable
		return "E00";
	} else {
		return "E00";
	}
	return "OK";
}

std::string GDBStub::handleReadMemory(const std::string& cmd)
{
	// m<byteAddr>,<byteLength>
	// GDB sends byte addresses and byte lengths.
	// ANEM16 memory is word-addressed (16-bit words, little-endian).
	unsigned int addrU = 0, length = 0;
	sscanf(cmd.c_str(), "%x,%x", &addrU, &length);

	std::string result;
	for (unsigned int i = 0; i < length; i++) {
		uint32_t byteAddr = addrU + i;
		addr_t wordAddr = byteToWord(byteAddr);
		auto mem = engine.getMemory(wordAddr, 1);
		uint16_t word = mem.empty() ? 0 : mem[0].value;
		// Extract the requested byte (little-endian: even=low, odd=high)
		uint8_t byte = (byteAddr & 1) ? (word >> 8) & 0xFF : word & 0xFF;
		result += toHex8(byte);
	}
	return result;
}

std::string GDBStub::handleWriteMemory(const std::string& cmd)
{
	// M<byteAddr>,<byteLength>:<hex bytes>
	auto colonPos = cmd.find(':');
	if (colonPos == std::string::npos)
		return "E01";

	unsigned int addrU = 0, length = 0;
	sscanf(cmd.c_str(), "%x,%x", &addrU, &length);

	std::string hexData = cmd.substr(colonPos + 1);
	for (unsigned int i = 0; i < length && (i * 2 + 1) < hexData.size(); i++) {
		uint32_t byteAddr = addrU + i;
		addr_t wordAddr = byteToWord(byteAddr);
		// Read-modify-write: preserve the other byte
		auto mem = engine.getMemory(wordAddr, 1);
		uint16_t word = mem.empty() ? 0 : mem[0].value;
		uint8_t newByte = fromHex8(hexData.substr(i * 2, 2));
		if (byteAddr & 1)
			word = (word & 0x00FF) | ((uint16_t)newByte << 8);
		else
			word = (word & 0xFF00) | newByte;
		engine.writeMemory(wordAddr, word);
	}
	return "OK";
}

std::string GDBStub::handleStep()
{
	auto result = engine.step(1);
	return stopReply(result.reason);
}

std::string GDBStub::handleContinue()
{
	while (true) {
		auto result = engine.runBatch(128);
		if (result.reason != StopReason::None)
			return stopReply(result.reason);

		// Check for Ctrl-C from GDB (non-blocking)
		char c;
		ssize_t n = recv(clientFd, &c, 1, MSG_DONTWAIT);
		if (n > 0 && c == 0x03) {
			return "T02"; // SIGINT
		}
	}
}

std::string GDBStub::handleBreakpoint(const std::string& cmd, bool insert)
{
	// Z0,<byteAddr>,<kind> or z0,<byteAddr>,<kind>
	// We only support software breakpoints (type 0)
	unsigned int type = 0, addr = 0, kind = 0;
	sscanf(cmd.c_str(), "%x,%x,%x", &type, &addr, &kind);

	if (type != 0)
		return ""; // Unsupported type — empty reply means unsupported

	addr_t wordAddr = byteToWord(addr);
	if (insert)
		engine.addBreakpoint(wordAddr);
	else
		engine.removeBreakpoint(wordAddr);
	return "OK";
}

std::string GDBStub::handleQuery(const std::string& cmd)
{
	if (cmd.rfind("qSupported", 0) == 0) {
		return "PacketSize=4000;swbreak+;hwbreak-;"
		       "qXfer:features:read+;"
		       "QStartNoAckMode+;"
		       "vContSupported+";
	}
	if (cmd == "QStartNoAckMode") {
		noAckMode = true;
		return "OK";
	}
	if (cmd.rfind("qXfer:features:read:target.xml:", 0) == 0) {
		// qXfer:features:read:target.xml:<offset>,<length>
		unsigned int offset = 0, length = 0;
		sscanf(cmd.c_str() + 31, "%x,%x", &offset, &length);
		std::string xml(targetXML);
		if (offset >= xml.size())
			return "l"; // Done, no more data
		std::string chunk = xml.substr(offset, length);
		if (offset + length >= xml.size())
			return "l" + chunk; // Last chunk
		return "m" + chunk; // More data available
	}
	if (cmd == "qAttached")
		return "1"; // Attached to existing process
	if (cmd == "qTStatus")
		return ""; // No tracepoint support
	if (cmd.rfind("qRcmd,", 0) == 0)
		return handleMonitor(cmd.substr(6));
	if (cmd == "qfThreadInfo")
		return "m1"; // Single thread
	if (cmd == "qsThreadInfo")
		return "l"; // No more threads
	if (cmd == "qC")
		return "QC1"; // Current thread ID
	if (cmd.rfind("qOffsets", 0) == 0)
		return "Text=0;Data=0;Bss=0";
	return ""; // Unsupported query — empty response
}

std::string GDBStub::handleVCommand(const std::string& cmd)
{
	if (cmd == "vCont?")
		return "vCont;c;s";
	if (cmd.rfind("vCont;c", 0) == 0)
		return handleContinue();
	if (cmd.rfind("vCont;s", 0) == 0)
		return handleStep();
	if (cmd == "vMustReplyEmpty")
		return "";
	return "";
}

std::string GDBStub::handleMonitor(const std::string& hexCmd)
{
	std::string cmd = hexDecode(hexCmd);

	// Trim whitespace
	while (!cmd.empty() && cmd.back() == ' ') cmd.pop_back();

	if (cmd == "help") {
		std::string help =
			"Monitor commands:\n"
			"  monitor reset       - Reset CPU and peripherals\n"
			"  monitor trace on    - Enable execution trace\n"
			"  monitor trace off   - Disable execution trace\n"
			"  monitor stats       - Show pipeline statistics\n"
			"  monitor pipeline    - Show pipeline stage contents\n"
			"  monitor interrupt   - Assert external interrupt\n"
			"  monitor deassert    - Deassert external interrupt\n"
			"  monitor regs        - Show all registers (including HI/LO/EPC/ECA)\n"
			"  monitor help        - This help\n";
		return "O" + hexEncode(help);
	}

	if (cmd == "reset") {
		engine.reset();
		return "O" + hexEncode("CPU reset.\n");
	}

	if (cmd == "trace on") {
		engine.setTrace(true);
		return "O" + hexEncode("Trace enabled.\n");
	}

	if (cmd == "trace off") {
		engine.setTrace(false);
		return "O" + hexEncode("Trace disabled.\n");
	}

	if (cmd == "stats") {
		auto s = engine.getStats();
		char buf[512];
		snprintf(buf, sizeof(buf),
			"Cycles:     %lld\n"
			"Stalls:     %lld\n"
			"Bubbles:    %lld\n"
			"Fwd ALU→ALU: %lld\n"
			"Fwd MEM→ALU: %lld\n",
			s.cycles, s.stalls, s.bubbles, s.fwdAluAlu, s.fwdMemAlu);
		return "O" + hexEncode(std::string(buf));
	}

	if (cmd == "pipeline") {
		auto p = engine.getPipeline();
		auto fmtStage = [](const char* name, const PipelineStage& s) {
			char buf[128];
			snprintf(buf, sizeof(buf), "  %-4s PC=0x%04X %s%s\n",
				name, s.pc, s.bubble ? "<bubble>" : s.asmText.c_str(),
				s.fwdAluAlu ? " [fwd:ALU]" : (s.fwdMemAlu ? " [fwd:MEM]" : ""));
			return std::string(buf);
		};
		std::string out = "Pipeline:\n";
		out += fmtStage("IF", p.ifStage);
		out += fmtStage("ID", p.idStage);
		out += fmtStage("EX", p.exStage);
		out += fmtStage("MEM", p.memStage);
		out += fmtStage("WB", p.wbStage);
		char zline[64];
		snprintf(zline, sizeof(zline), "  Z=%d Stalled=%d\n", p.zFlag, p.stalled);
		out += zline;
		return "O" + hexEncode(out);
	}

	if (cmd == "interrupt") {
		engine.assertInterrupt();
		return "O" + hexEncode("External interrupt asserted.\n");
	}

	if (cmd == "deassert") {
		engine.deassertInterrupt();
		return "O" + hexEncode("External interrupt deasserted.\n");
	}

	if (cmd == "regs") {
		auto r = engine.getRegisters();
		char buf[512];
		int n = 0;
		for (int i = 0; i < 16; i++) {
			n += snprintf(buf + n, sizeof(buf) - n, "R%-2d=0x%04X  ", i, r.gpr[i]);
			if ((i & 3) == 3) n += snprintf(buf + n, sizeof(buf) - n, "\n");
		}
		n += snprintf(buf + n, sizeof(buf) - n,
			"PC=0x%04X  SP=0x%04X  HI=0x%04X  LO=0x%04X\n"
			"EPC=0x%04X ECA=0x%04X IEN=%d Z=%d\n",
			r.pc, r.sp, r.hi, r.lo, r.epc, r.eca, r.ien, r.zFlag);
		return "O" + hexEncode(std::string(buf));
	}

	return "O" + hexEncode("Unknown monitor command. Try 'monitor help'.\n");
}

// ---------------------------------------------------------------------------
// Main dispatch
// ---------------------------------------------------------------------------
std::string GDBStub::handleCommand(const std::string& pkt)
{
	if (pkt.empty())
		return "";
	if (pkt == "\x03")
		return "T02"; // Ctrl-C → SIGINT stop reply

	char cmd = pkt[0];
	std::string args = pkt.substr(1);

	switch (cmd) {
	case '?':
		return "T05"; // Initial stop reason: SIGTRAP
	case 'g':
		return handleReadRegisters();
	case 'G':
		return handleWriteRegisters(args);
	case 'p':
		return handleReadRegister(args);
	case 'P':
		return handleWriteRegister(args);
	case 'm':
		return handleReadMemory(args);
	case 'M':
		return handleWriteMemory(args);
	case 's':
		return handleStep();
	case 'c':
		return handleContinue();
	case 'Z':
		return handleBreakpoint(args, true);
	case 'z':
		return handleBreakpoint(args, false);
	case 'D':
		sendReply("OK");
		return ""; // Detach — caller will close
	case 'k':
		return ""; // Kill
	case 'q':
	case 'Q':
		return handleQuery(pkt);
	case 'v':
		return handleVCommand(pkt);
	case 'H':
		return "OK"; // Set thread — we have one thread
	default:
		return ""; // Unsupported — empty reply
	}
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
// Embedded target description — works with any GDB (including gdb-multiarch)
// and with our custom GDB that has native anem16 architecture support.
static const char anem16_target_xml[] =
	"<?xml version=\"1.0\"?>\n"
	"<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n"
	"<target version=\"1.0\">\n"
	"  <architecture>anem16</architecture>\n"
	"  <feature name=\"org.gnu.gdb.anem16.core\">\n"
	"    <reg name=\"r0\"  bitsize=\"16\" regnum=\"0\"  type=\"uint16\"   group=\"general\"/>\n"
	"    <reg name=\"r1\"  bitsize=\"16\" regnum=\"1\"  type=\"uint16\"   group=\"general\"/>\n"
	"    <reg name=\"r2\"  bitsize=\"16\" regnum=\"2\"  type=\"uint16\"   group=\"general\"/>\n"
	"    <reg name=\"r3\"  bitsize=\"16\" regnum=\"3\"  type=\"uint16\"   group=\"general\"/>\n"
	"    <reg name=\"r4\"  bitsize=\"16\" regnum=\"4\"  type=\"uint16\"   group=\"general\"/>\n"
	"    <reg name=\"r5\"  bitsize=\"16\" regnum=\"5\"  type=\"uint16\"   group=\"general\"/>\n"
	"    <reg name=\"r6\"  bitsize=\"16\" regnum=\"6\"  type=\"uint16\"   group=\"general\"/>\n"
	"    <reg name=\"r7\"  bitsize=\"16\" regnum=\"7\"  type=\"uint16\"   group=\"general\"/>\n"
	"    <reg name=\"r8\"  bitsize=\"16\" regnum=\"8\"  type=\"uint16\"   group=\"general\"/>\n"
	"    <reg name=\"r9\"  bitsize=\"16\" regnum=\"9\"  type=\"uint16\"   group=\"general\"/>\n"
	"    <reg name=\"r10\" bitsize=\"16\" regnum=\"10\" type=\"uint16\"   group=\"general\"/>\n"
	"    <reg name=\"r11\" bitsize=\"16\" regnum=\"11\" type=\"uint16\"   group=\"general\"/>\n"
	"    <reg name=\"r12\" bitsize=\"16\" regnum=\"12\" type=\"uint16\"   group=\"general\"/>\n"
	"    <reg name=\"r13\" bitsize=\"16\" regnum=\"13\" type=\"data_ptr\" group=\"general\"/>\n"
	"    <reg name=\"r14\" bitsize=\"16\" regnum=\"14\" type=\"uint16\"   group=\"general\"/>\n"
	"    <reg name=\"r15\" bitsize=\"16\" regnum=\"15\" type=\"code_ptr\" group=\"general\"/>\n"
	"    <reg name=\"pc\"  bitsize=\"16\" regnum=\"16\" type=\"code_ptr\" group=\"general\"/>\n"
	"    <reg name=\"sp\"  bitsize=\"16\" regnum=\"17\" type=\"data_ptr\" group=\"general\"/>\n"
	"  </feature>\n"
	"</target>\n";

GDBStub::GDBStub(DebugEngine& engine, int port)
	: engine(engine), port(port), targetXML(anem16_target_xml)
{
	engine.setDebugMode(true);
}

GDBStub::~GDBStub()
{
	if (clientFd >= 0) close(clientFd);
	if (listenFd >= 0) close(listenFd);
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
void GDBStub::run()
{
	signal(SIGPIPE, SIG_IGN);

	listenFd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenFd < 0) {
		std::cerr << "GDB stub: socket() failed: " << strerror(errno) << std::endl;
		return;
	}

	int opt = 1;
	setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	if (bind(listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		std::cerr << "GDB stub: bind() failed on port " << port << ": "
		          << strerror(errno) << std::endl;
		close(listenFd);
		listenFd = -1;
		return;
	}

	listen(listenFd, 1);
	std::cout << "GDB stub listening on port " << port << std::endl;
	std::cout << "Connect with: target remote :" << port << std::endl;

	while (true) {
		struct sockaddr_in clientAddr{};
		socklen_t clientLen = sizeof(clientAddr);
		clientFd = accept(listenFd, (struct sockaddr*)&clientAddr, &clientLen);
		if (clientFd < 0) {
			std::cerr << "GDB stub: accept() failed: " << strerror(errno) << std::endl;
			break;
		}

		std::cerr << "GDB client connected from "
		          << inet_ntoa(clientAddr.sin_addr) << std::endl;
		noAckMode = false;

		// Process packets until disconnect
		while (true) {
			std::string pkt = readPacket();
			if (pkt.empty()) {
				std::cerr << "GDB client disconnected" << std::endl;
				break;
			}

			std::string reply = handleCommand(pkt);
			if (pkt[0] == 'D' || pkt[0] == 'k')
				break;

			sendReply(reply);
		}

		close(clientFd);
		clientFd = -1;
	}
}
