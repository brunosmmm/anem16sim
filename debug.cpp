/*
 * @file debug.cpp
 * @brief ANEM16 interactive debugger
 */

#include "debug.h"
#include "disasm.h"
#include <iostream>
#include <iomanip>
#include <sstream>

ANEMDebugger::ANEMDebugger(ANEMCPU& cpu, bool traceOn)
	: cpu(cpu), traceEnabled(traceOn)
{
}

std::vector<std::string> ANEMDebugger::tokenize(const std::string& line) const
{
	std::vector<std::string> tokens;
	std::istringstream iss(line);
	std::string tok;
	while (iss >> tok)
		tokens.push_back(tok);
	return tokens;
}

addr_t ANEMDebugger::parseAddr(const std::string& s) const
{
	if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		return (addr_t)std::stoul(s, nullptr, 16);
	return (addr_t)std::stoul(s, nullptr, 10);
}

void ANEMDebugger::printTrace(unsigned long long cycle, addr_t pc,
                               const ANEMInstruction& instr)
{
	std::cout << "[" << std::setw(6) << cycle << "] "
	          << "PC=0x" << std::hex << std::setfill('0') << std::setw(4) << pc
	          << " " << std::setfill(' ') << std::dec
	          << disassemble(instr) << std::endl;
}

bool ANEMDebugger::checkBreakpoints()
{
	return breakpoints.count(cpu.getPC()) > 0;
}

bool ANEMDebugger::checkWatchpoints()
{
	// Watchpoints check: look at the last memory access log entries
	if (watchpoints.empty()) return false;

	const auto& log = cpu.getDataMemory().getAccessLog();
	if (log.empty()) return false;

	// Check if any recent access hit a watchpoint
	// We check the last entry since we clear after each step
	const auto& last = log.back();
	return watchpoints.count(last.address) > 0;
}

void ANEMDebugger::cmdStep(unsigned int n)
{
	for (unsigned int i = 0; i < n; i++)
	{
		if (cpu.programEnd())
		{
			std::cout << "Program halted." << std::endl;
			halted = true;
			return;
		}

		cpu.getDataMemory().clearAccessLog();
		cpu.clockCycle();

		if (traceEnabled)
			printTrace(cpu.getCycleCount(), cpu.getPC(),
			           cpu.getFetchToDecode().ireg);

		if (i < n - 1 && checkBreakpoints())
		{
			std::cout << "Breakpoint hit at PC=0x" << std::hex << std::setw(4)
			          << std::setfill('0') << cpu.getPC() << std::dec
			          << std::setfill(' ') << std::endl;
			return;
		}

		if (!watchpoints.empty() && checkWatchpoints())
		{
			const auto& last = cpu.getDataMemory().getAccessLog().back();
			std::cout << "Watchpoint hit: " << (last.write ? "write" : "read")
			          << " at addr=0x" << std::hex << std::setw(4)
			          << std::setfill('0') << last.address
			          << " val=0x" << std::setw(4) << last.value
			          << std::dec << std::setfill(' ') << std::endl;
			return;
		}
	}
}

void ANEMDebugger::cmdContinue()
{
	// Enable access logging for watchpoints
	bool hadLog = !watchpoints.empty();
	if (hadLog)
		cpu.getDataMemory().setAccessLog(true);

	while (!cpu.programEnd())
	{
		cpu.getDataMemory().clearAccessLog();
		cpu.clockCycle();

		if (traceEnabled)
			printTrace(cpu.getCycleCount(), cpu.getPC(),
			           cpu.getFetchToDecode().ireg);

		if (checkBreakpoints())
		{
			std::cout << "Breakpoint hit at PC=0x" << std::hex << std::setw(4)
			          << std::setfill('0') << cpu.getPC() << std::dec
			          << std::setfill(' ') << std::endl;
			return;
		}

		if (hadLog && checkWatchpoints())
		{
			const auto& last = cpu.getDataMemory().getAccessLog().back();
			std::cout << "Watchpoint hit: " << (last.write ? "write" : "read")
			          << " at addr=0x" << std::hex << std::setw(4)
			          << std::setfill('0') << last.address
			          << " val=0x" << std::setw(4) << last.value
			          << std::dec << std::setfill(' ') << std::endl;
			return;
		}
	}

	std::cout << "Program halted." << std::endl;
	halted = true;
}

void ANEMDebugger::cmdBreakpoint(const std::vector<std::string>& args)
{
	if (args.size() < 2)
	{
		// List breakpoints
		if (breakpoints.empty())
		{
			std::cout << "No breakpoints set." << std::endl;
			return;
		}
		std::cout << "Breakpoints:" << std::endl;
		for (addr_t bp : breakpoints)
			std::cout << "  0x" << std::hex << std::setfill('0')
			          << std::setw(4) << bp << std::dec
			          << std::setfill(' ') << std::endl;
		return;
	}
	addr_t addr = parseAddr(args[1]);
	breakpoints.insert(addr);
	std::cout << "Breakpoint set at 0x" << std::hex << std::setfill('0')
	          << std::setw(4) << addr << std::dec
	          << std::setfill(' ') << std::endl;
}

void ANEMDebugger::cmdDeleteBreakpoint(const std::vector<std::string>& args)
{
	if (args.size() < 2)
	{
		std::cout << "Usage: db <addr>" << std::endl;
		return;
	}
	addr_t addr = parseAddr(args[1]);
	if (breakpoints.erase(addr))
		std::cout << "Breakpoint deleted." << std::endl;
	else
		std::cout << "No breakpoint at that address." << std::endl;
}

void ANEMDebugger::cmdWatchpoint(const std::vector<std::string>& args)
{
	if (args.size() < 2)
	{
		if (watchpoints.empty())
		{
			std::cout << "No watchpoints set." << std::endl;
			return;
		}
		std::cout << "Watchpoints:" << std::endl;
		for (addr_t wp : watchpoints)
			std::cout << "  0x" << std::hex << std::setfill('0')
			          << std::setw(4) << wp << std::dec
			          << std::setfill(' ') << std::endl;
		return;
	}
	addr_t addr = parseAddr(args[1]);
	watchpoints.insert(addr);
	cpu.getDataMemory().setAccessLog(true);
	std::cout << "Watchpoint set at 0x" << std::hex << std::setfill('0')
	          << std::setw(4) << addr << std::dec
	          << std::setfill(' ') << std::endl;
}

void ANEMDebugger::cmdDeleteWatchpoint(const std::vector<std::string>& args)
{
	if (args.size() < 2)
	{
		std::cout << "Usage: dw <addr>" << std::endl;
		return;
	}
	addr_t addr = parseAddr(args[1]);
	if (watchpoints.erase(addr))
	{
		std::cout << "Watchpoint deleted." << std::endl;
		if (watchpoints.empty())
			cpu.getDataMemory().setAccessLog(false);
	}
	else
		std::cout << "No watchpoint at that address." << std::endl;
}

void ANEMDebugger::cmdRegisters(const std::vector<std::string>& args)
{
	if (args.size() >= 2)
	{
		uint8_t reg = (uint8_t)std::stoul(args[1]);
		if (reg >= 16)
		{
			std::cout << "Register index must be 0-15." << std::endl;
			return;
		}
		std::cout << regName(reg) << " = 0x" << std::hex << std::setfill('0')
		          << std::setw(4) << cpu.readRegister(reg) << std::dec
		          << std::setfill(' ') << std::endl;
		return;
	}

	std::cout << "PC  = 0x" << std::hex << std::setfill('0') << std::setw(4) << cpu.getPC() << std::endl;
	for (int i = 0; i < 16; i++)
	{
		std::cout << std::left << std::setw(4) << std::setfill(' ') << regName(i)
		          << std::right << "= 0x" << std::hex << std::setfill('0') << std::setw(4)
		          << cpu.readRegister(i) << std::setfill(' ') << std::dec;
		if ((i % 4) == 3)
			std::cout << std::endl;
		else
			std::cout << "  ";
	}
	std::cout << std::right << "HI  = 0x" << std::hex << std::setfill('0') << std::setw(4) << cpu.getHI()
	          << "  LO  = 0x" << std::setw(4) << cpu.getLO() << std::dec
	          << std::setfill(' ') << std::endl;
}

void ANEMDebugger::cmdMemory(const std::vector<std::string>& args)
{
	if (args.size() < 2)
	{
		std::cout << "Usage: m <start> [count]" << std::endl;
		return;
	}
	addr_t start = parseAddr(args[1]);
	addr_t count = (args.size() >= 3) ? (addr_t)std::stoul(args[2]) : 16;

	for (addr_t i = start; i < start + count; i++)
	{
		std::cout << "  [0x" << std::hex << std::setfill('0') << std::setw(4) << i
		          << "] = 0x" << std::setw(4) << cpu.readDataMem(i)
		          << std::dec << std::setfill(' ') << std::endl;
	}
}

void ANEMDebugger::cmdDisassemble(const std::vector<std::string>& args)
{
	if (args.size() < 2)
	{
		std::cout << "Usage: d <start> [count]" << std::endl;
		return;
	}
	addr_t start = parseAddr(args[1]);
	addr_t count = (args.size() >= 3) ? (addr_t)std::stoul(args[2]) : 16;

	for (addr_t i = start; i < start + count; i++)
	{
		ANEMInstruction instr = cpu.readInstrMem(i);
		std::string marker = (i == cpu.getPC()) ? " >> " : "    ";
		std::cout << marker << "[0x" << std::hex << std::setfill('0') << std::setw(4) << i
		          << "] " << std::setfill(' ') << std::dec
		          << disassemble(instr) << std::endl;
	}
}

void ANEMDebugger::cmdPipeline()
{
	auto& f = cpu.getFetchToDecode();
	auto& d = cpu.getDecodeToExec();
	auto& e = cpu.getExecToMem();
	auto& m = cpu.getMemToWB();

	std::cout << "=== Pipeline (cycle " << cpu.getCycleCount() << ") ===" << std::endl;

	// IF stage: what was just fetched
	std::cout << "  IF:  [0x" << std::hex << std::setfill('0') << std::setw(4) << f.pc
	          << "] " << std::setfill(' ') << std::dec;
	if (f.bubble) std::cout << "<bubble>";
	else std::cout << disassemble(f.ireg);
	std::cout << std::endl;

	// ID stage
	std::cout << "  ID:  [0x" << std::hex << std::setfill('0') << std::setw(4) << d.pc
	          << "] " << std::setfill(' ') << std::dec;
	if (d.bubble) std::cout << "<bubble>";
	else std::cout << disassemble(d.ireg);
	// Show forwarding info
	if (d.fwd_alu_alua || d.fwd_alu_alub)
		std::cout << "  [fwd: ALU->ALU]";
	if (d.fwd_mem_alua || d.fwd_mem_alub)
		std::cout << "  [fwd: MEM->ALU]";
	std::cout << std::endl;

	// EX stage
	std::cout << "  EX:  [0x" << std::hex << std::setfill('0') << std::setw(4) << e.pc
	          << "] " << std::setfill(' ') << std::dec;
	if (e.bubble) std::cout << "<bubble>";
	else std::cout << disassemble(e.ireg);
	std::cout << std::endl;

	// MEM stage
	std::cout << "  MEM: [0x" << std::hex << std::setfill('0') << std::setw(4) << m.pc
	          << "] " << std::setfill(' ') << std::dec;
	if (m.bubble) std::cout << "<bubble>";
	else std::cout << disassemble(m.ireg);
	std::cout << std::endl;

	// Stall status
	std::cout << "  Stall: " << (cpu.isStalled() ? "yes" : "none") << std::endl;
}

void ANEMDebugger::cmdTrace(const std::vector<std::string>& args)
{
	if (args.size() >= 2)
	{
		if (args[1] == "on") traceEnabled = true;
		else if (args[1] == "off") traceEnabled = false;
		else std::cout << "Usage: t [on|off]" << std::endl;
	}
	else
	{
		traceEnabled = !traceEnabled;
	}
	std::cout << "Trace " << (traceEnabled ? "enabled" : "disabled") << std::endl;
}

void ANEMDebugger::cmdStats()
{
	cpu.dumpStats(std::cout);
}

void ANEMDebugger::cmdReset()
{
	cpu.reset();
	halted = false;
	std::cout << "CPU reset." << std::endl;
}

void ANEMDebugger::cmdHelp()
{
	std::cout << "Commands:" << std::endl;
	std::cout << "  s [N]          Step N cycles (default 1)" << std::endl;
	std::cout << "  c              Continue until breakpoint/halt" << std::endl;
	std::cout << "  b [addr]       Set breakpoint / list breakpoints" << std::endl;
	std::cout << "  db <addr>      Delete breakpoint" << std::endl;
	std::cout << "  w [addr]       Set watchpoint / list watchpoints" << std::endl;
	std::cout << "  dw <addr>      Delete watchpoint" << std::endl;
	std::cout << "  r [N]          Show registers / show register N" << std::endl;
	std::cout << "  m <start> [N]  Dump data memory" << std::endl;
	std::cout << "  d <start> [N]  Disassemble instruction memory" << std::endl;
	std::cout << "  p              Show pipeline state" << std::endl;
	std::cout << "  t [on|off]     Toggle execution trace" << std::endl;
	std::cout << "  stats          Show statistics" << std::endl;
	std::cout << "  reset          Reset CPU" << std::endl;
	std::cout << "  q              Quit" << std::endl;
	std::cout << "Addresses can be decimal or hex (0x prefix)." << std::endl;
}

void ANEMDebugger::run()
{
	std::string line;

	std::cout << "ANEM16 Debugger. Type 'h' for help." << std::endl;
	std::cout << "Program loaded, PC=0x" << std::hex << std::setfill('0')
	          << std::setw(4) << cpu.getPC() << std::dec
	          << std::setfill(' ') << std::endl;

	while (true)
	{
		std::cout << "anem> " << std::flush;
		if (!std::getline(std::cin, line))
			break;

		auto tokens = tokenize(line);
		if (tokens.empty()) continue;

		const std::string& cmd = tokens[0];

		try
		{
			if (cmd == "q" || cmd == "quit")
				break;
			else if (cmd == "h" || cmd == "help")
				cmdHelp();
			else if (cmd == "s" || cmd == "step")
			{
				unsigned int n = 1;
				if (tokens.size() >= 2) n = std::stoul(tokens[1]);
				cmdStep(n);
			}
			else if (cmd == "c" || cmd == "continue")
				cmdContinue();
			else if (cmd == "b")
				cmdBreakpoint(tokens);
			else if (cmd == "db")
				cmdDeleteBreakpoint(tokens);
			else if (cmd == "w")
				cmdWatchpoint(tokens);
			else if (cmd == "dw")
				cmdDeleteWatchpoint(tokens);
			else if (cmd == "r" || cmd == "reg")
				cmdRegisters(tokens);
			else if (cmd == "m" || cmd == "mem")
				cmdMemory(tokens);
			else if (cmd == "d" || cmd == "dis")
				cmdDisassemble(tokens);
			else if (cmd == "p" || cmd == "pipe")
				cmdPipeline();
			else if (cmd == "t" || cmd == "trace")
				cmdTrace(tokens);
			else if (cmd == "stats")
				cmdStats();
			else if (cmd == "reset")
				cmdReset();
			else
				std::cout << "Unknown command: " << cmd << ". Type 'h' for help." << std::endl;
		}
		catch (const std::exception& ex)
		{
			std::cout << "Error: " << ex.what() << std::endl;
		}
	}
}
