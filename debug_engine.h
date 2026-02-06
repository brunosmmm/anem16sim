/*
 * @file debug_engine.h
 * @brief ANEM16 debug engine — pure-logic debug layer with no I/O
 */

#ifndef DEBUG_ENGINE_H_
#define DEBUG_ENGINE_H_

#include "cpu.h"
#include "instrset.h"
#include "snapshot.h"
#include <set>
#include <vector>
#include <string>
#include <functional>
#include <iosfwd>
#include <optional>

enum class StopReason { None, Breakpoint, Watchpoint, Halted };

struct MemAccessInfo {
	addr_t address;
	data_t value;
	bool write;
};

struct StepResult {
	addr_t pc;
	unsigned long long cycle;
	bool halted;
	StopReason reason;
	std::optional<MemAccessInfo> watchAccess;
};

struct RegistersResult {
	addr_t pc;
	data_t gpr[16];
	data_t hi;
	data_t lo;
};

struct MemoryEntry {
	addr_t address;
	data_t value;
};

struct DisasmEntry {
	addr_t address;
	std::string asmText;
	uint16_t word;
	bool current;  // PC points here
};

struct PipelineStage {
	addr_t pc;
	std::string asmText;
	bool bubble;
	// Forwarding info (ID stage only)
	bool fwdAluAlu = false;
	bool fwdMemAlu = false;
};

struct PipelineResult {
	unsigned long long cycle;
	PipelineStage ifStage;
	PipelineStage idStage;
	PipelineStage exStage;
	PipelineStage memStage;
	bool stalled;
};

struct StatsResult {
	long long cycles;
	long long instructions;
	long long stalls;
	long long bubbles;
	long long fwdAluAlu;
	long long fwdMemAlu;
};

struct StatusResult {
	bool running;
	bool halted;
	addr_t pc;
	unsigned long long cycle;
};

using EventCallback = std::function<void(StopReason, addr_t pc,
                                          unsigned long long cycle,
                                          std::optional<MemAccessInfo>)>;
using TraceEventCallback = std::function<void(unsigned long long cycle,
                                               addr_t pc,
                                               const std::string& asmText)>;

class DebugEngine
{
private:
	ANEMCPU& cpu;
	std::set<addr_t> breakpoints;
	std::set<addr_t> watchpoints;
	bool traceEnabled = false;
	bool halted = false;

	EventCallback eventCallback;
	TraceEventCallback traceCallback;

	bool checkBreakpoints();
	bool checkWatchpoints();
	std::optional<MemAccessInfo> getWatchAccess();

public:
	DebugEngine(ANEMCPU& cpu, bool traceOn = false);

	// Execution
	StepResult step(unsigned int count = 1);
	StepResult runBatch(unsigned int batchSize);

	// Breakpoints
	void addBreakpoint(addr_t addr);
	bool removeBreakpoint(addr_t addr);
	std::vector<addr_t> listBreakpoints() const;

	// Watchpoints
	void addWatchpoint(addr_t addr);
	bool removeWatchpoint(addr_t addr);
	std::vector<addr_t> listWatchpoints() const;

	// Inspection
	RegistersResult getRegisters() const;
	data_t getRegister(uint8_t n) const;
	std::vector<MemoryEntry> getMemory(addr_t start, addr_t count) const;
	std::vector<DisasmEntry> disassemble(addr_t start, addr_t count) const;
	PipelineResult getPipeline() const;
	StatsResult getStats() const;
	void dumpFullStats(std::ostream& out) const;
	StatusResult getStatus() const;

	// Memory write
	void writeMemory(addr_t addr, data_t value);

	// Snapshots
	void saveSnapshot(const std::string& path, const std::string& programFile = "");
	void loadSnapshot(const std::string& path);

	// Control
	void setTrace(bool enabled);
	bool getTrace() const { return traceEnabled; }
	void reset();
	bool isHalted() const { return halted; }

	// Callbacks
	void setEventCallback(EventCallback cb) { eventCallback = std::move(cb); }
	void setTraceCallback(TraceEventCallback cb) { traceCallback = std::move(cb); }
};

#endif /* DEBUG_ENGINE_H_ */
