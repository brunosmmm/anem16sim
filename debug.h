/*
 * @file debug.h
 * @brief ANEM16 interactive debugger (REPL frontend)
 */

#ifndef DEBUG_H_
#define DEBUG_H_

#include "debug_engine.h"
#include <string>
#include <vector>
#include <functional>

class ANEMPeripheralGPIO;
class ANEMPeripheralTimer;
class ANEMPeripheralUART;

class ANEMDebugger
{
private:
	DebugEngine engine;

	// Command parsing
	std::vector<std::string> tokenize(const std::string& line) const;
	addr_t parseAddr(const std::string& s) const;

	// Commands
	void cmdStep(unsigned int n);
	void cmdContinue();
	void cmdBreakpoint(const std::vector<std::string>& args);
	void cmdDeleteBreakpoint(const std::vector<std::string>& args);
	void cmdWatchpoint(const std::vector<std::string>& args);
	void cmdDeleteWatchpoint(const std::vector<std::string>& args);
	void cmdRegisters(const std::vector<std::string>& args);
	void cmdMemory(const std::vector<std::string>& args);
	void cmdDisassemble(const std::vector<std::string>& args);
	void cmdPipeline();
	void cmdTrace(const std::vector<std::string>& args);
	void cmdStats();
	void cmdSave(const std::vector<std::string>& args);
	void cmdLoad(const std::vector<std::string>& args);
	void cmdReset();
	void cmdHelp();
	void cmdGPIO(const std::vector<std::string>& args);
	void cmdPeriph();
	void cmdUartTx(const std::vector<std::string>& args);

public:
	ANEMDebugger(ANEMCPU& cpu, bool traceOn = false);
	void run();  // main REPL loop

	// Peripheral support
	void setPeripheralClock(std::function<void()> fn) { engine.setPeripheralClock(std::move(fn)); }
	void setGPIO(ANEMPeripheralGPIO* p) { engine.setGPIO(p); }
	void setTimer(ANEMPeripheralTimer* p) { engine.setTimer(p); }
	void setUART(ANEMPeripheralUART* p) { engine.setUART(p); }
};

#endif /* DEBUG_H_ */
