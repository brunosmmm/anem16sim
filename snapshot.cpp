/*
 * @file snapshot.cpp
 * @brief CPU state snapshot save/load (JSON)
 */

#include "snapshot.h"
#include "cpu.h"
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

using json = nlohmann::json;

// ---- Pipeline register serialization ----

static json serializeF2D(const f2d& r)
{
	return {
		{"pc", r.pc},
		{"savedpc", r.savedpc},
		{"bubble", r.bubble},
		{"ireg", r.ireg.toWord()}
	};
}

static json serializeD2E(const d2e& r)
{
	return {
		{"reg_ctl", (int)r.reg_ctl},
		{"alu_ctl", (int)r.alu_ctl},
		{"alu_func", (int)r.alu_func},
		{"alu_shamt", r.alu_shamt},
		{"rega_sel", r.rega_sel},
		{"regb_sel", r.regb_sel},
		{"rega_out", r.rega_out},
		{"regb_out", r.regb_out},
		{"mem_enable", r.mem_enable},
		{"mem_write", r.mem_write},
		{"imm_val", r.imm_val},
		{"off_4", r.off_4},
		{"j_flag", r.j_flag},
		{"jr_flag", r.jr_flag},
		{"bz_flag", r.bz_flag},
		{"bhleq_flag", r.bhleq_flag},
		{"j_dest", r.j_dest},
		{"bz_offset", r.bz_offset},
		{"fwd_alu_alua", r.fwd_alu_alua},
		{"fwd_alu_alub", r.fwd_alu_alub},
		{"fwd_mem_alua", r.fwd_mem_alua},
		{"fwd_mem_alub", r.fwd_mem_alub},
		{"fwd_alua", r.fwd_alua},
		{"fwd_alub", r.fwd_alub},
		{"bubble", r.bubble},
		{"hictl", (int)r.hictl},
		{"loctl", (int)r.loctl},
		{"hiout", r.hiout},
		{"loout", r.loout},
		{"savedpc", r.savedpc},
		{"sp_ctl", (int)r.sp_ctl},
		{"sp_val", r.sp_val},
		{"alu_imm_sel", r.alu_imm_sel},
		{"exc_ctl", r.exc_ctl},
		{"syscall_flag", r.syscall_flag},
		{"reti_flag", r.reti_flag},
		{"pc", r.pc},
		{"ireg", r.ireg.toWord()}
	};
}

static json serializeE2M(const e2m& r)
{
	return {
		{"reg_ctl", (int)r.reg_ctl},
		{"alu_out_value", r.alu_out.value},
		{"alu_out_z", (bool)(r.alu_out.flags & ANEM_ALU_Z)},
		{"alu_out_flags", r.alu_out.flags},
		{"alu_out_hiout", r.alu_out.hiout},
		{"rega_sel", r.rega_sel},
		{"rega_out", r.rega_out},
		{"mem_enable", r.mem_enable},
		{"mem_write", r.mem_write},
		{"imm_val", r.imm_val},
		{"bubble", r.bubble},
		{"hictl", (int)r.hictl},
		{"loctl", (int)r.loctl},
		{"hiout", r.hiout},
		{"loout", r.loout},
		{"savedpc", r.savedpc},
		{"sp_ctl", (int)r.sp_ctl},
		{"sp_new", r.sp_new},
		{"sp_addr", r.sp_addr},
		{"exc_ctl", r.exc_ctl},
		{"pc", r.pc},
		{"ireg", r.ireg.toWord()}
	};
}

static json serializeM2W(const m2w& r)
{
	return {
		{"reg_ctl", (int)r.reg_ctl},
		{"alu_out_value", r.alu_out.value},
		{"alu_out_z", (bool)(r.alu_out.flags & ANEM_ALU_Z)},
		{"alu_out_flags", r.alu_out.flags},
		{"alu_out_hiout", r.alu_out.hiout},
		{"mem_out", r.mem_out},
		{"rega_sel", r.rega_sel},
		{"rega_out", r.rega_out},
		{"imm_val", r.imm_val},
		{"bubble", r.bubble},
		{"hictl", (int)r.hictl},
		{"loctl", (int)r.loctl},
		{"hiout", r.hiout},
		{"loout", r.loout},
		{"savedpc", r.savedpc},
		{"sp_ctl", (int)r.sp_ctl},
		{"sp_new", r.sp_new},
		{"exc_ctl", r.exc_ctl},
		{"pc", r.pc},
		{"ireg", r.ireg.toWord()}
	};
}

// ---- Pipeline register deserialization ----

static f2d deserializeF2D(const json& j)
{
	f2d r{};
	r.pc = j.value("pc", (addr_t)0);
	r.savedpc = j.value("savedpc", (addr_t)0);
	r.bubble = j.value("bubble", false);
	r.ireg = ANEMInstruction((uint16_t)j.value("ireg", 0));
	return r;
}

static d2e deserializeD2E(const json& j)
{
	d2e r{};
	r.reg_ctl = (ANEMRegBnkOp)j.value("reg_ctl", 0);
	r.alu_ctl = (ANEMAluOp)j.value("alu_ctl", 0);
	r.alu_func = (ANEMAluFunc)j.value("alu_func", 0);
	r.alu_shamt = j.value("alu_shamt", (uint8_t)0);
	r.rega_sel = j.value("rega_sel", (uint8_t)0);
	r.regb_sel = j.value("regb_sel", (uint8_t)0);
	r.rega_out = j.value("rega_out", (data_t)0);
	r.regb_out = j.value("regb_out", (data_t)0);
	r.mem_enable = j.value("mem_enable", false);
	r.mem_write = j.value("mem_write", false);
	r.imm_val = j.value("imm_val", (uint8_t)0);
	r.off_4 = j.value("off_4", (uint8_t)0);
	r.j_flag = j.value("j_flag", false);
	r.jr_flag = j.value("jr_flag", false);
	r.bz_flag = j.value("bz_flag", false);
	r.bhleq_flag = j.value("bhleq_flag", false);
	r.j_dest = j.value("j_dest", (addr_t)0);
	r.bz_offset = j.value("bz_offset", (uint16_t)0);
	r.fwd_alu_alua = j.value("fwd_alu_alua", false);
	r.fwd_alu_alub = j.value("fwd_alu_alub", false);
	r.fwd_mem_alua = j.value("fwd_mem_alua", false);
	r.fwd_mem_alub = j.value("fwd_mem_alub", false);
	r.fwd_alua = j.value("fwd_alua", (data_t)0);
	r.fwd_alub = j.value("fwd_alub", (data_t)0);
	r.bubble = j.value("bubble", false);
	r.hictl = (ANEMHILOOp)j.value("hictl", 0);
	r.loctl = (ANEMHILOOp)j.value("loctl", 0);
	r.hiout = j.value("hiout", (data_t)0);
	r.loout = j.value("loout", (data_t)0);
	r.savedpc = j.value("savedpc", (addr_t)0);
	r.sp_ctl = (ANEMSPCtl)j.value("sp_ctl", 0);
	r.sp_val = j.value("sp_val", (data_t)0);
	r.alu_imm_sel = j.value("alu_imm_sel", false);
	r.exc_ctl = j.value("exc_ctl", (uint8_t)0);
	r.syscall_flag = j.value("syscall_flag", false);
	r.reti_flag = j.value("reti_flag", false);
	r.pc = j.value("pc", (addr_t)0);
	r.ireg = ANEMInstruction((uint16_t)j.value("ireg", 0));
	return r;
}

static e2m deserializeE2M(const json& j)
{
	e2m r{};
	r.reg_ctl = (ANEMRegBnkOp)j.value("reg_ctl", 0);
	r.alu_out.value = j.value("alu_out_value", (data_t)0);
	r.alu_out.flags = j.value("alu_out_flags", (uint8_t)0);
	r.alu_out.hiout = j.value("alu_out_hiout", (data_t)0);
	r.rega_sel = j.value("rega_sel", (uint8_t)0);
	r.rega_out = j.value("rega_out", (data_t)0);
	r.mem_enable = j.value("mem_enable", false);
	r.mem_write = j.value("mem_write", false);
	r.imm_val = j.value("imm_val", (uint8_t)0);
	r.bubble = j.value("bubble", false);
	r.hictl = (ANEMHILOOp)j.value("hictl", 0);
	r.loctl = (ANEMHILOOp)j.value("loctl", 0);
	r.hiout = j.value("hiout", (data_t)0);
	r.loout = j.value("loout", (data_t)0);
	r.savedpc = j.value("savedpc", (addr_t)0);
	r.sp_ctl = (ANEMSPCtl)j.value("sp_ctl", 0);
	r.sp_new = j.value("sp_new", (data_t)0);
	r.sp_addr = j.value("sp_addr", (data_t)0);
	r.exc_ctl = j.value("exc_ctl", (uint8_t)0);
	r.pc = j.value("pc", (addr_t)0);
	r.ireg = ANEMInstruction((uint16_t)j.value("ireg", 0));
	return r;
}

static m2w deserializeM2W(const json& j)
{
	m2w r{};
	r.reg_ctl = (ANEMRegBnkOp)j.value("reg_ctl", 0);
	r.alu_out.value = j.value("alu_out_value", (data_t)0);
	r.alu_out.flags = j.value("alu_out_flags", (uint8_t)0);
	r.alu_out.hiout = j.value("alu_out_hiout", (data_t)0);
	r.mem_out = j.value("mem_out", (data_t)0);
	r.rega_sel = j.value("rega_sel", (uint8_t)0);
	r.rega_out = j.value("rega_out", (data_t)0);
	r.imm_val = j.value("imm_val", (uint8_t)0);
	r.bubble = j.value("bubble", false);
	r.hictl = (ANEMHILOOp)j.value("hictl", 0);
	r.loctl = (ANEMHILOOp)j.value("loctl", 0);
	r.hiout = j.value("hiout", (data_t)0);
	r.loout = j.value("loout", (data_t)0);
	r.savedpc = j.value("savedpc", (addr_t)0);
	r.sp_ctl = (ANEMSPCtl)j.value("sp_ctl", 0);
	r.sp_new = j.value("sp_new", (data_t)0);
	r.exc_ctl = j.value("exc_ctl", (uint8_t)0);
	r.pc = j.value("pc", (addr_t)0);
	r.ireg = ANEMInstruction((uint16_t)j.value("ireg", 0));
	return r;
}

// ---- Timestamp helper ----

static std::string isoTimestamp()
{
	auto now = std::chrono::system_clock::now();
	auto time = std::chrono::system_clock::to_time_t(now);
	std::tm tm{};
	localtime_r(&time, &tm);
	std::ostringstream ss;
	ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
	return ss.str();
}

// ---- Public API ----

json snapshot::save(const ANEMCPU& cpu, const std::string& programFile)
{
	json j;

	// Metadata
	j["anem16sim_snapshot"] = {
		{"version", 1},
		{"timestamp", isoTimestamp()},
		{"program", programFile},
		{"cycle", cpu.getTotalCycles()}
	};

	// CPU scalar state
	json cpuState;
	cpuState["pc"] = cpu.getPC();
	cpuState["hi"] = cpu.getHI();
	cpuState["lo"] = cpu.getLO();
	cpuState["sp"] = cpu.getSP();
	cpuState["epc"] = cpu.getEPC();
	cpuState["eca"] = cpu.getECA();
	cpuState["ien"] = cpu.getIEN();

	// GPR
	json gpr = json::array();
	for (int i = 0; i < 16; i++)
		gpr.push_back(cpu.readRegister(i));
	cpuState["gpr"] = gpr;

	cpuState["fw_enable"] = cpu.getFwEnable();
	cpuState["max_cycles"] = cpu.getMaxCycles();
	cpuState["total_cycles"] = cpu.getTotalCycles();

	// Stall state
	cpuState["stall"] = {
		{"if", cpu.getStallIF()},
		{"id", cpu.getStallID()},
		{"ex", cpu.getStallEX()},
		{"mem", cpu.getStallMEM()},
		{"wb", cpu.getStallWB()},
		{"master", cpu.getStallMaster()},
		{"counter", cpu.getStallCounter()}
	};

	// Forwarding flags
	cpuState["forwarding"] = {
		{"alu_alua", cpu.getFwdAluAluA()},
		{"alu_alub", cpu.getFwdAluAluB()},
		{"mem_alua", cpu.getFwdMemAluA()},
		{"mem_alub", cpu.getFwdMemAluB()}
	};

	// Halt detection
	cpuState["halt_detection"] = {
		{"last_pc", cpu.getLastPC()},
		{"prev_pc", cpu.getPrevPC()},
		{"same_pc_count", cpu.getSamePCCount()},
		{"drain_cycles", cpu.getDrainCycles()}
	};

	j["cpu"] = cpuState;

	// Pipeline registers
	j["pipeline"] = {
		{"fetch_to_decode", serializeF2D(cpu.getFetchToDecode())},
		{"decode_to_exec", serializeD2E(cpu.getDecodeToExec())},
		{"exec_to_mem", serializeE2M(cpu.getExecToMem())},
		{"mem_to_wb", serializeM2W(cpu.getMemToWB())}
	};

	// Data memory (sparse: only non-zero entries)
	// Use const_cast to access getDataMemory which is non-const
	auto& dmem = const_cast<ANEMCPU&>(cpu).getDataMemory();
	uint32_t memSize = dmem.getSize();
	json memData = json::object();
	for (uint32_t addr = 0; addr < memSize; addr++)
	{
		data_t val = dmem.readDirect(addr);
		if (val != 0)
			memData[std::to_string(addr)] = val;
	}
	j["memory"] = {
		{"data", memData},
		{"data_size", memSize}
	};

	// Counters
	j["counters"] = cpu.getCounters().toJson();

	return j;
}

void snapshot::restore(ANEMCPU& cpu, const json& j)
{
	// CPU scalar state
	const auto& cpuState = j.at("cpu");
	cpu.setPC(cpuState.at("pc").get<addr_t>());
	cpu.setHI(cpuState.at("hi").get<data_t>());
	cpu.setLO(cpuState.at("lo").get<data_t>());
	cpu.setSP(cpuState.value("sp", (data_t)0xFFCF));
	cpu.setEPC(cpuState.value("epc", (data_t)0));
	cpu.setECA(cpuState.value("eca", (data_t)0));
	cpu.setIEN(cpuState.value("ien", false));

	// GPR
	const auto& gpr = cpuState.at("gpr");
	for (int i = 0; i < 16; i++)
		cpu.writeRegister(i, gpr[i].get<data_t>());

	cpu.setMaxCycles(cpuState.value("max_cycles", (unsigned int)10000));

	// Stall state
	if (cpuState.contains("stall"))
	{
		const auto& s = cpuState["stall"];
		cpu.setStallState(
			s.value("if", false),
			s.value("id", false),
			s.value("ex", false),
			s.value("mem", false),
			s.value("wb", false),
			s.value("master", false),
			s.value("counter", (unsigned int)0)
		);
	}

	// Forwarding flags
	if (cpuState.contains("forwarding"))
	{
		const auto& f = cpuState["forwarding"];
		cpu.setForwardingState(
			f.value("alu_alua", false),
			f.value("alu_alub", false),
			f.value("mem_alua", false),
			f.value("mem_alub", false)
		);
	}

	// Halt detection
	if (cpuState.contains("halt_detection"))
	{
		const auto& h = cpuState["halt_detection"];
		cpu.setHaltDetection(
			cpuState.value("total_cycles", (unsigned long long)0),
			h.value("last_pc", (addr_t)0),
			h.value("prev_pc", (addr_t)0),
			h.value("same_pc_count", (unsigned int)0),
			h.value("drain_cycles", -1)
		);
	}

	// Pipeline registers
	if (j.contains("pipeline"))
	{
		const auto& p = j["pipeline"];
		if (p.contains("fetch_to_decode"))
			cpu.setFetchToDecode(deserializeF2D(p["fetch_to_decode"]));
		if (p.contains("decode_to_exec"))
			cpu.setDecodeToExec(deserializeD2E(p["decode_to_exec"]));
		if (p.contains("exec_to_mem"))
			cpu.setExecToMem(deserializeE2M(p["exec_to_mem"]));
		if (p.contains("mem_to_wb"))
			cpu.setMemToWB(deserializeM2W(p["mem_to_wb"]));
	}

	// Data memory
	if (j.contains("memory"))
	{
		auto& dmem = cpu.getDataMemory();
		// Clear first
		dmem.clearMem();

		const auto& memData = j["memory"]["data"];
		for (auto& [key, val] : memData.items())
		{
			addr_t addr = (addr_t)std::stoul(key);
			dmem.writeDirect(addr, val.get<data_t>());
		}
	}

	// Counters
	if (j.contains("counters"))
		cpu.getCountersMut().fromJson(j["counters"]);
}

void snapshot::saveToFile(const ANEMCPU& cpu, const std::string& path,
                          const std::string& programFile)
{
	json j = save(cpu, programFile);
	std::ofstream out(path);
	out << j.dump(2) << std::endl;
}

void snapshot::loadFromFile(ANEMCPU& cpu, const std::string& path)
{
	std::ifstream in(path);
	json j = json::parse(in);
	restore(cpu, j);
}
