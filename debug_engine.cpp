/*
 * @file debug_engine.cpp
 * @brief ANEM16 debug engine — pure-logic debug layer with no I/O
 */

#include "debug_engine.h"
#include "snapshot.h"
#include "disasm.h"

DebugEngine::DebugEngine(ANEMCPU& cpu, bool traceOn)
	: cpu(cpu), traceEnabled(traceOn)
{
}

bool DebugEngine::checkBreakpoints()
{
	return breakpoints.count(cpu.getPC()) > 0;
}

bool DebugEngine::checkWatchpoints()
{
	if (watchpoints.empty()) return false;
	const auto& log = cpu.getDataMemory().getAccessLog();
	if (log.empty()) return false;
	const auto& last = log.back();
	return watchpoints.count(last.address) > 0;
}

std::optional<MemAccessInfo> DebugEngine::getWatchAccess()
{
	const auto& log = cpu.getDataMemory().getAccessLog();
	if (log.empty()) return std::nullopt;
	const auto& last = log.back();
	return MemAccessInfo{last.address, last.value, last.write};
}

StepResult DebugEngine::step(unsigned int count)
{
	StepResult result{};
	result.reason = StopReason::None;
	result.halted = false;

	for (unsigned int i = 0; i < count; i++)
	{
		if (cpu.programEnd())
		{
			halted = true;
			result.halted = true;
			result.reason = StopReason::Halted;
			result.pc = cpu.getPC();
			result.cycle = cpu.getCycleCount();
			return result;
		}

		cpu.getDataMemory().clearAccessLog();
		cpu.clockCycle();
		if (periphClockFn) periphClockFn();

		if (traceEnabled && traceCallback)
		{
			traceCallback(cpu.getCycleCount(), cpu.getPC(),
			              ::disassemble(cpu.getFetchToDecode().ireg, cpu.getFetchToDecode().pc));
		}

		if (i < count - 1 && checkBreakpoints())
		{
			result.reason = StopReason::Breakpoint;
			result.pc = cpu.getPC();
			result.cycle = cpu.getCycleCount();
			result.halted = false;
			return result;
		}

		if (!watchpoints.empty() && checkWatchpoints())
		{
			result.reason = StopReason::Watchpoint;
			result.watchAccess = getWatchAccess();
			result.pc = cpu.getPC();
			result.cycle = cpu.getCycleCount();
			result.halted = false;
			return result;
		}
	}

	result.pc = cpu.getPC();
	result.cycle = cpu.getCycleCount();
	result.halted = cpu.programEnd();
	if (result.halted)
	{
		halted = true;
		result.reason = StopReason::Halted;
	}
	return result;
}

StepResult DebugEngine::runBatch(unsigned int batchSize)
{
	StepResult result{};
	result.reason = StopReason::None;
	result.halted = false;

	bool hadLog = !watchpoints.empty();
	if (hadLog)
		cpu.getDataMemory().setAccessLog(true);

	for (unsigned int i = 0; i < batchSize; i++)
	{
		if (cpu.programEnd())
		{
			halted = true;
			result.halted = true;
			result.reason = StopReason::Halted;
			break;
		}

		cpu.getDataMemory().clearAccessLog();
		cpu.clockCycle();
		if (periphClockFn) periphClockFn();

		if (traceEnabled && traceCallback)
		{
			traceCallback(cpu.getCycleCount(), cpu.getPC(),
			              ::disassemble(cpu.getFetchToDecode().ireg, cpu.getFetchToDecode().pc));
		}

		if (checkBreakpoints())
		{
			result.reason = StopReason::Breakpoint;
			break;
		}

		if (hadLog && checkWatchpoints())
		{
			result.reason = StopReason::Watchpoint;
			result.watchAccess = getWatchAccess();
			break;
		}
	}

	result.pc = cpu.getPC();
	result.cycle = cpu.getCycleCount();
	if (result.reason == StopReason::None && cpu.programEnd())
	{
		halted = true;
		result.halted = true;
		result.reason = StopReason::Halted;
	}
	return result;
}

// Breakpoints

void DebugEngine::addBreakpoint(addr_t addr)
{
	breakpoints.insert(addr);
}

bool DebugEngine::removeBreakpoint(addr_t addr)
{
	return breakpoints.erase(addr) > 0;
}

std::vector<addr_t> DebugEngine::listBreakpoints() const
{
	return {breakpoints.begin(), breakpoints.end()};
}

// Watchpoints

void DebugEngine::addWatchpoint(addr_t addr)
{
	watchpoints.insert(addr);
	cpu.getDataMemory().setAccessLog(true);
}

bool DebugEngine::removeWatchpoint(addr_t addr)
{
	bool removed = watchpoints.erase(addr) > 0;
	if (watchpoints.empty())
		cpu.getDataMemory().setAccessLog(false);
	return removed;
}

std::vector<addr_t> DebugEngine::listWatchpoints() const
{
	return {watchpoints.begin(), watchpoints.end()};
}

// Inspection

RegistersResult DebugEngine::getRegisters() const
{
	RegistersResult r{};
	r.pc = cpu.getPC();
	for (int i = 0; i < 16; i++)
		r.gpr[i] = cpu.readRegister(i);
	r.hi = cpu.getHI();
	r.lo = cpu.getLO();
	r.sp = cpu.getSP();
	r.epc = cpu.getEPC();
	r.eca = cpu.getECA();
	r.ien = cpu.getIEN();
	return r;
}

data_t DebugEngine::getRegister(uint8_t n) const
{
	return cpu.readRegister(n);
}

std::vector<MemoryEntry> DebugEngine::getMemory(addr_t start, addr_t count) const
{
	std::vector<MemoryEntry> entries;
	entries.reserve(count);
	for (addr_t i = start; i < start + count; i++)
		entries.push_back({i, cpu.readDataMem(i)});
	return entries;
}

std::vector<DisasmEntry> DebugEngine::disassemble(addr_t start, addr_t count) const
{
	std::vector<DisasmEntry> entries;
	entries.reserve(count);
	for (addr_t i = start; i < start + count; i++)
	{
		ANEMInstruction instr = cpu.readInstrMem(i);
		entries.push_back({i, ::disassemble(instr, i), instr.toWord(), i == cpu.getPC()});
	}
	return entries;
}

PipelineResult DebugEngine::getPipeline() const
{
	auto& f = cpu.getFetchToDecode();
	auto& d = cpu.getDecodeToExec();
	auto& e = cpu.getExecToMem();
	auto& m = cpu.getMemToWB();

	PipelineResult r{};
	r.cycle = cpu.getCycleCount();

	r.ifStage = {f.pc, f.bubble ? "<bubble>" : ::disassemble(f.ireg, f.pc), f.bubble};
	r.idStage = {d.pc, d.bubble ? "<bubble>" : ::disassemble(d.ireg, d.pc), d.bubble,
	             d.fwd_alu_alua || d.fwd_alu_alub,
	             d.fwd_mem_alua || d.fwd_mem_alub};
	r.exStage = {e.pc, e.bubble ? "<bubble>" : ::disassemble(e.ireg, e.pc), e.bubble};
	r.memStage = {m.pc, m.bubble ? "<bubble>" : ::disassemble(m.ireg, m.pc), m.bubble};
	r.stalled = cpu.isStalled();

	return r;
}

StatsResult DebugEngine::getStats() const
{
	const auto& c = cpu.getCounters();
	return {
		c.getCycleCount(),
		c.getCycleCount(),  // instructions decoded tracked same as cycles in counters
		c.getStallCount(),
		c.getBubbleCount(),
		c.getForwardAluAluCount(),
		c.getForwardMemAluCount()
	};
}

void DebugEngine::dumpFullStats(std::ostream& out) const
{
	cpu.dumpStats(out);
}

StatusResult DebugEngine::getStatus() const
{
	return {false, halted, cpu.getPC(), cpu.getCycleCount()};
}

// Memory write

void DebugEngine::writeMemory(addr_t addr, data_t value)
{
	cpu.getDataMemory().write(addr, value);
}

// Interrupt control

void DebugEngine::assertInterrupt()
{
	cpu.setIntPin(true);
}

void DebugEngine::deassertInterrupt()
{
	cpu.setIntPin(false);
}

// Control

void DebugEngine::setTrace(bool enabled)
{
	traceEnabled = enabled;
}

// Snapshots

void DebugEngine::saveSnapshot(const std::string& path, const std::string& programFile)
{
	snapshot::saveToFile(cpu, path, programFile);
}

void DebugEngine::loadSnapshot(const std::string& path)
{
	snapshot::loadFromFile(cpu, path);
	halted = false;
}

void DebugEngine::reset()
{
	cpu.reset();
	halted = false;
}
