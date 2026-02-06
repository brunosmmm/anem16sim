/*
 * @file debug.h
 * @brief ANEM16 interactive debugger
 */

#ifndef DEBUG_H_
#define DEBUG_H_

#include "cpu.h"
#include <set>
#include <string>
#include <vector>

class ANEMDebugger
{
private:
	ANEMCPU& cpu;
	std::set<addr_t> breakpoints;
	std::set<addr_t> watchpoints;
	bool traceEnabled;
	bool halted = false;

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
	void cmdReset();
	void cmdHelp();

	// Trace callback
	void printTrace(unsigned long long cycle, addr_t pc,
	                const ANEMInstruction& instr);

	// Check stop conditions (breakpoints, watchpoints)
	bool checkBreakpoints();
	bool checkWatchpoints();

public:
	ANEMDebugger(ANEMCPU& cpu, bool traceOn = false);
	void run();  // main REPL loop
};

#endif /* DEBUG_H_ */
