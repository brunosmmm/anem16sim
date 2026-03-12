/*
 * @file mem.cpp
 * @brief ANEM data and instruction memory emulation
 * @author Bruno Morais <brunosmmm@gmail.com>
 * @since 12/05/2014
 */

#include "mem.h"
#include "disasm.h"
#include <cstring>
#include <regex>
#include "except.h"
#include <set>
#include <map>
#include <cstdio>
#include <iostream>
#include <unordered_map>

/***
 * @brief Allocates memory for the instruction mem
 * @param size Memory size in words
 */
ANEMInstructionMemory::ANEMInstructionMemory(uint32_t size)
{

	this->size = size;
	///allocate
	this->imem = new ANEMInstruction[size];

	///initialize to zeros
	memset((void*)this->imem,0x00,sizeof(ANEMInstruction)*size);

}

ANEMDataMemory::ANEMDataMemory(uint32_t size)
{
	this->size = size;

	//allocate
	this->dmem = new dmem_t[size];

	//clear memory
	this->clearMem();

}

void ANEMDataMemory::clearMem(void)
{
	//set all to zeros
	memset((void*)this->dmem,0x0000,sizeof(dmem_t)*this->size);

}

data_t ANEMDataMemory::read(addr_t address)
{
	data_t value;

	if (address > this->size)
	{
		//look into peripherals
		auto it = this->vmem.find(address);

		if (it != this->vmem.end())
		{
			value = this->vmem[address]->read(address);
		}
		else
		{
			value = 0xFFFF;
		}
	}
	else
	{
		value = this->dmem[address];
	}

	if (this->accessLogEnabled)
	{
		unsigned long long cycle = this->cyclePtr ? *this->cyclePtr : 0;
		this->accessLog.push_back({cycle, address, value, false});
	}

	return value;
}

void ANEMDataMemory::write(addr_t address, dmem_t data)
{

	if (address > this->size)
	{

		//look into peripherals
		auto it = this->vmem.find(address);

		if (it != this->vmem.end())
		{

			this->vmem[address]->write(address,data);

		}

		//not found!
		if (this->accessLogEnabled)
		{
			unsigned long long cycle = this->cyclePtr ? *this->cyclePtr : 0;
			this->accessLog.push_back({cycle, address, data, true});
		}

		return;
	}

	this->dmem[address] = data;

	if (this->accessLogEnabled)
	{
		unsigned long long cycle = this->cyclePtr ? *this->cyclePtr : 0;
		this->accessLog.push_back({cycle, address, data, true});
	}
}

ANEMInstruction ANEMInstructionMemory::fetch(addr_t addr)
{

	if (addr > this->size)
	{
		//exception, do something here
		return ANEM_INSTRUCTION_NOP;
	}

	return this->imem[addr];

}

// Minimal ELF32 structures (avoids dependency on <elf.h>)
namespace {
struct Elf32_Ehdr {
	uint8_t  e_ident[16];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint32_t e_entry;
	uint32_t e_phoff;
	uint32_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct Elf32_Shdr {
	uint32_t sh_name;
	uint32_t sh_type;
	uint32_t sh_flags;
	uint32_t sh_addr;
	uint32_t sh_offset;
	uint32_t sh_size;
	uint32_t sh_link;
	uint32_t sh_info;
	uint32_t sh_addralign;
	uint32_t sh_entsize;
};

constexpr uint32_t SHT_PROGBITS = 1;
constexpr uint32_t SHT_NOBITS = 8;
constexpr uint32_t SHT_SYMTAB   = 2;
constexpr uint32_t SHT_RELA     = 4;

struct Elf32_Sym {
	uint32_t st_name;
	uint32_t st_value;
	uint32_t st_size;
	uint8_t  st_info;
	uint8_t  st_other;
	uint16_t st_shndx;
};

struct Elf32_Rela {
	uint32_t r_offset;
	uint32_t r_info;
	int32_t  r_addend;
};

} // anonymous namespace

// ---- DWARF .debug_line parser ----

static uint64_t readULEB128(const uint8_t*& p, const uint8_t* end)
{
	uint64_t result = 0;
	unsigned shift = 0;
	while (p < end) {
		uint8_t b = *p++;
		result |= (uint64_t)(b & 0x7F) << shift;
		if ((b & 0x80) == 0) break;
		shift += 7;
	}
	return result;
}

static int64_t readSLEB128(const uint8_t*& p, const uint8_t* end)
{
	int64_t result = 0;
	unsigned shift = 0;
	uint8_t b;
	do {
		if (p >= end) break;
		b = *p++;
		result |= (int64_t)(b & 0x7F) << shift;
		shift += 7;
	} while (b & 0x80);
	if (shift < 64 && (b & 0x40))
		result |= -(int64_t(1) << shift);
	return result;
}

static uint32_t readU32LE(const uint8_t* p) { return p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24); }
static uint16_t readU16LE(const uint8_t* p) { return p[0] | (p[1]<<8); }

static void parseDwarfLine(const std::vector<uint8_t>& data,
                            const std::vector<Elf32_Sym>& symtab,
                            const std::vector<Elf32_Rela>& relas)
{
	if (data.size() < 12) return;

	// Apply relocations to a mutable copy
	std::vector<uint8_t> buf = data;
	for (const auto& rela : relas) {
		uint32_t offset = rela.r_offset;
		uint32_t sym_idx = rela.r_info >> 8;
		if (sym_idx >= symtab.size() || offset + 4 > buf.size()) continue;
		uint32_t resolved = symtab[sym_idx].st_value + rela.r_addend;
		buf[offset]   = resolved & 0xFF;
		buf[offset+1] = (resolved >> 8) & 0xFF;
		buf[offset+2] = (resolved >> 16) & 0xFF;
		buf[offset+3] = (resolved >> 24) & 0xFF;
	}

	const uint8_t* p = buf.data();
	const uint8_t* end = p + buf.size();

	// Unit header
	uint32_t unit_length = readU32LE(p); p += 4;
	const uint8_t* unit_end = p + unit_length;
	if (unit_end > end) unit_end = end;

	uint16_t version = readU16LE(p); p += 2;
	if (version < 2 || version > 4) {
		std::cerr << "DWARF: unsupported .debug_line version " << version << std::endl;
		return;
	}

	uint32_t header_length = readU32LE(p); p += 4;
	const uint8_t* prog_start = p + header_length;

	uint8_t min_instr_length = *p++;
	// DWARF 4 has max_ops_per_instruction
	if (version >= 4) p++; // skip max_ops_per_instruction
	uint8_t default_is_stmt = *p++;
	int8_t line_base = (int8_t)*p++;
	uint8_t line_range = *p++;
	uint8_t opcode_base = *p++;

	// Standard opcode lengths
	std::vector<uint8_t> std_opcode_lengths(opcode_base);
	for (uint8_t i = 1; i < opcode_base; i++)
		std_opcode_lengths[i] = *p++;

	// Include directories (null-terminated strings, terminated by empty string)
	std::vector<std::string> include_dirs;
	include_dirs.push_back(""); // index 0 = compilation directory
	while (p < prog_start && *p != 0) {
		include_dirs.push_back(std::string((const char*)p));
		p += strlen((const char*)p) + 1;
	}
	if (p < prog_start) p++; // skip terminating 0

	// File names table
	std::vector<std::string> file_names;
	file_names.push_back(""); // index 0 unused (1-based)
	while (p < prog_start && *p != 0) {
		std::string name((const char*)p);
		p += name.size() + 1;
		readULEB128(p, prog_start); // dir index
		readULEB128(p, prog_start); // last modified
		readULEB128(p, prog_start); // file size

		// Extract just the filename (strip path)
		auto slash = name.rfind('/');
		if (slash != std::string::npos)
			name = name.substr(slash + 1);
		file_names.push_back(name);
	}
	if (p < prog_start) p++; // skip terminating 0

	// Execute the line program
	p = prog_start;

	uint32_t address = 0;
	unsigned file = 1;
	unsigned line = 1;
	bool is_stmt = default_is_stmt;
	bool end_sequence = false;

	auto emitRow = [&]() {
		if (file > 0 && file < file_names.size() && line > 0) {
			addr_t word_addr = address / 2; // byte addr → word addr
			addSourceLoc(word_addr, file_names[file], (int)line);
		}
	};

	while (p < unit_end) {
		uint8_t opcode = *p++;

		if (opcode == 0) {
			// Extended opcode
			uint64_t ext_len = readULEB128(p, unit_end);
			const uint8_t* ext_end = p + ext_len;
			if (ext_end > unit_end) break;
			if (ext_len == 0) continue;

			uint8_t ext_op = *p++;
			switch (ext_op) {
			case 1: // DW_LNE_end_sequence
				end_sequence = true;
				emitRow();
				address = 0; file = 1; line = 1;
				is_stmt = default_is_stmt; end_sequence = false;
				break;
			case 2: // DW_LNE_set_address
				if (ext_end - p >= 4)
					address = readU32LE(p);
				else if (ext_end - p >= 2)
					address = readU16LE(p);
				break;
			case 3: // DW_LNE_define_file
			{
				std::string name((const char*)p);
				p += name.size() + 1;
				readULEB128(p, ext_end);
				readULEB128(p, ext_end);
				readULEB128(p, ext_end);
				auto slash = name.rfind('/');
				if (slash != std::string::npos)
					name = name.substr(slash + 1);
				file_names.push_back(name);
				break;
			}
			case 4: // DW_LNE_set_discriminator
				readULEB128(p, ext_end);
				break;
			default:
				break;
			}
			p = ext_end;
		} else if (opcode < opcode_base) {
			// Standard opcode
			switch (opcode) {
			case 1: // DW_LNS_copy
				emitRow();
				break;
			case 2: // DW_LNS_advance_pc
				address += readULEB128(p, unit_end) * min_instr_length;
				break;
			case 3: // DW_LNS_advance_line
				line += readSLEB128(p, unit_end);
				break;
			case 4: // DW_LNS_set_file
				file = readULEB128(p, unit_end);
				break;
			case 5: // DW_LNS_set_column
				readULEB128(p, unit_end);
				break;
			case 6: // DW_LNS_negate_stmt
				is_stmt = !is_stmt;
				break;
			case 7: // DW_LNS_set_basic_block
				break;
			case 8: // DW_LNS_const_add_pc
				address += ((255 - opcode_base) / line_range) * min_instr_length;
				break;
			case 9: // DW_LNS_fixed_advance_pc
				address += readU16LE(p); p += 2;
				break;
			default:
				// Unknown standard opcode — skip its operands
				if (opcode < opcode_base)
					for (uint8_t j = 0; j < std_opcode_lengths[opcode]; j++)
						readULEB128(p, unit_end);
				break;
			}
		} else {
			// Special opcode
			uint8_t adjusted = opcode - opcode_base;
			address += (adjusted / line_range) * min_instr_length;
			line += line_base + (adjusted % line_range);
			emitRow();
		}
	}
}

// ---- DWARF .debug_info parser for variable-to-register mapping ----

// DWARF tag constants
static constexpr uint16_t DW_TAG_subprogram = 0x2e;
static constexpr uint16_t DW_TAG_variable = 0x34;
static constexpr uint16_t DW_TAG_formal_parameter = 0x05;

// DWARF attribute constants
static constexpr uint16_t DW_AT_name = 0x03;
static constexpr uint16_t DW_AT_low_pc = 0x11;
static constexpr uint16_t DW_AT_high_pc = 0x12;
static constexpr uint16_t DW_AT_location = 0x02;

// DWARF form constants
static constexpr uint8_t DW_FORM_addr = 0x01;
static constexpr uint8_t DW_FORM_data1 = 0x0b;
static constexpr uint8_t DW_FORM_data2 = 0x05;
static constexpr uint8_t DW_FORM_data4 = 0x06;
static constexpr uint8_t DW_FORM_data8 = 0x07;
static constexpr uint8_t DW_FORM_string = 0x08;
static constexpr uint8_t DW_FORM_block1 = 0x0a;
static constexpr uint8_t DW_FORM_block2 = 0x03;
static constexpr uint8_t DW_FORM_block4 = 0x04;
static constexpr uint8_t DW_FORM_block = 0x09;
static constexpr uint8_t DW_FORM_flag = 0x0c;
static constexpr uint8_t DW_FORM_strp = 0x0e;
static constexpr uint8_t DW_FORM_udata = 0x0f;
static constexpr uint8_t DW_FORM_sdata = 0x0d;
static constexpr uint8_t DW_FORM_ref1 = 0x11;
static constexpr uint8_t DW_FORM_ref2 = 0x12;
static constexpr uint8_t DW_FORM_ref4 = 0x13;
static constexpr uint8_t DW_FORM_ref8 = 0x14;
static constexpr uint8_t DW_FORM_ref_udata = 0x15;
static constexpr uint8_t DW_FORM_sec_offset = 0x17;
static constexpr uint8_t DW_FORM_exprloc = 0x18;
static constexpr uint8_t DW_FORM_flag_present = 0x19;
static constexpr uint8_t DW_FORM_ref_addr = 0x10;

// DWARF expression opcodes
static constexpr uint8_t DW_OP_fbreg = 0x91;
// DW_OP_reg0..DW_OP_reg31: 0x50..0x6F
// DW_OP_breg0..DW_OP_breg31: 0x70..0x8F
static constexpr uint8_t DW_OP_regx = 0x90;

struct AbbrevEntry {
	uint64_t tag;
	bool hasChildren;
	struct AttrSpec { uint64_t attr; uint64_t form; };
	std::vector<AttrSpec> attrs;
};

static std::unordered_map<uint64_t, AbbrevEntry> parseAbbrevTable(
	const uint8_t* p, const uint8_t* end)
{
	std::unordered_map<uint64_t, AbbrevEntry> table;
	while (p < end) {
		uint64_t code = readULEB128(p, end);
		if (code == 0) break;
		AbbrevEntry entry;
		entry.tag = readULEB128(p, end);
		entry.hasChildren = (*p++ != 0);
		while (p < end) {
			uint64_t attr = readULEB128(p, end);
			uint64_t form = readULEB128(p, end);
			if (attr == 0 && form == 0) break;
			entry.attrs.push_back({attr, form});
		}
		table[code] = std::move(entry);
	}
	return table;
}

// Parse a DWARF expression to extract register or stack location
static std::string parseDwarfExpr(const uint8_t* p, size_t len)
{
	const uint8_t* end = p + len;
	if (p >= end) return {};

	uint8_t op = *p++;
	if (op >= 0x50 && op <= 0x6F) {
		// DW_OP_reg0..DW_OP_reg31
		return "R" + std::to_string(op - 0x50);
	}
	if (op == DW_OP_regx) {
		uint64_t reg = readULEB128(p, end);
		return "R" + std::to_string(reg);
	}
	if (op == DW_OP_fbreg) {
		int64_t offset = readSLEB128(p, end);
		return "stack" + ((offset >= 0) ? ("+" + std::to_string(offset)) : std::to_string(offset));
	}
	if (op >= 0x70 && op <= 0x8F) {
		// DW_OP_breg0..DW_OP_breg31: register + offset (less common for simple vars)
		int regnum = op - 0x70;
		int64_t offset = readSLEB128(p, end);
		if (offset == 0)
			return "R" + std::to_string(regnum);
		return "R" + std::to_string(regnum) + ((offset >= 0) ? "+" : "") + std::to_string(offset);
	}
	return {};
}

// Skip a DWARF form value, returning how many bytes were consumed
static void skipForm(uint8_t form, const uint8_t*& p, const uint8_t* end,
                     uint8_t addrSize, uint16_t version)
{
	switch (form) {
	case DW_FORM_addr:       p += addrSize; break;
	case DW_FORM_data1:
	case DW_FORM_flag:
	case DW_FORM_ref1:       p += 1; break;
	case DW_FORM_data2:
	case DW_FORM_ref2:       p += 2; break;
	case DW_FORM_data4:
	case DW_FORM_ref4:
	case DW_FORM_strp:       p += 4; break;
	case DW_FORM_data8:
	case DW_FORM_ref8:       p += 8; break;
	case DW_FORM_string:     while (p < end && *p) p++; if (p < end) p++; break;
	case DW_FORM_udata:
	case DW_FORM_ref_udata:  readULEB128(p, end); break;
	case DW_FORM_sdata:      readSLEB128(p, end); break;
	case DW_FORM_block1:     { uint8_t n = *p++; p += n; break; }
	case DW_FORM_block2:     { uint16_t n = readU16LE(p); p += 2 + n; break; }
	case DW_FORM_block4:     { uint32_t n = readU32LE(p); p += 4 + n; break; }
	case DW_FORM_block:
	case DW_FORM_exprloc:    { uint64_t n = readULEB128(p, end); p += n; break; }
	case DW_FORM_flag_present: break; // zero size
	case DW_FORM_sec_offset: p += 4; break; // 32-bit DWARF
	case DW_FORM_ref_addr:   p += (version >= 3 ? 4 : addrSize); break;
	default:                 break; // unknown form — can't skip reliably
	}
}

// Parse a .debug_loc location list entry at the given offset.
// Returns all (startPC, endPC, location) tuples from the list.
static std::vector<std::tuple<uint32_t, uint32_t, std::string>>
parseLocList(const std::vector<uint8_t>& locData, uint32_t offset, uint8_t addrSize)
{
	std::vector<std::tuple<uint32_t, uint32_t, std::string>> result;
	if (offset >= locData.size()) return result;

	const uint8_t* p = locData.data() + offset;
	const uint8_t* end = locData.data() + locData.size();

	while (p + 2 * addrSize <= end) {
		uint32_t begin, finish;
		if (addrSize == 2) {
			begin = readU16LE(p); p += 2;
			finish = readU16LE(p); p += 2;
		} else {
			begin = readU32LE(p); p += 4;
			finish = readU32LE(p); p += 4;
		}
		// End of list
		if (begin == 0 && finish == 0) break;
		// Base address selector (all 1s)
		if ((addrSize == 2 && begin == 0xFFFF) || (addrSize == 4 && begin == 0xFFFFFFFF)) {
			// base = finish; skip for now
			continue;
		}
		if (p + 2 > end) break;
		uint16_t exprLen = readU16LE(p); p += 2;
		if (p + exprLen > end) break;
		std::string loc = parseDwarfExpr(p, exprLen);
		p += exprLen;
		if (!loc.empty())
			result.emplace_back(begin, finish, loc);
	}
	return result;
}

static void parseDwarfInfo(const std::vector<uint8_t>& infoData,
                            const std::vector<uint8_t>& abbrevData,
                            const std::vector<uint8_t>& strData,
                            const std::vector<uint8_t>& locData,
                            const std::vector<Elf32_Sym>& symtab,
                            const std::vector<Elf32_Rela>& relas)
{
	if (infoData.size() < 11) return;

	// Apply relocations
	std::vector<uint8_t> buf = infoData;
	for (const auto& rela : relas) {
		uint32_t offset = rela.r_offset;
		uint32_t sym_idx = rela.r_info >> 8;
		if (sym_idx >= symtab.size() || offset + 4 > buf.size()) continue;
		uint32_t resolved = symtab[sym_idx].st_value + rela.r_addend;
		buf[offset]   = resolved & 0xFF;
		buf[offset+1] = (resolved >> 8) & 0xFF;
		buf[offset+2] = (resolved >> 16) & 0xFF;
		buf[offset+3] = (resolved >> 24) & 0xFF;
	}

	const uint8_t* p = buf.data();
	const uint8_t* end = p + buf.size();

	// Compilation unit header
	uint32_t unit_length = readU32LE(p); p += 4;
	const uint8_t* unit_end = p + unit_length;
	if (unit_end > end) unit_end = end;

	uint16_t version = readU16LE(p); p += 2;
	if (version < 2 || version > 4) {
		std::cerr << "DWARF: unsupported .debug_info version " << version << std::endl;
		return;
	}

	/*uint32_t abbrev_offset =*/ readU32LE(p); p += 4;
	uint8_t addr_size = *p++;

	// Parse abbreviation table
	auto abbrevTable = parseAbbrevTable(abbrevData.data(), abbrevData.data() + abbrevData.size());

	// Track function PC ranges for variables without explicit ranges
	uint32_t funcLowPC = 0, funcHighPC = 0;
	bool inFunction = false;

	// Depth tracking for has_children
	std::vector<bool> depthStack; // true if parent had children

	while (p < unit_end) {
		uint64_t abbrevCode = readULEB128(p, unit_end);
		if (abbrevCode == 0) {
			// Null entry — end of children
			if (!depthStack.empty()) {
				depthStack.pop_back();
				if (depthStack.empty() || depthStack.back() == false)
					inFunction = false;
			}
			continue;
		}

		auto it = abbrevTable.find(abbrevCode);
		if (it == abbrevTable.end()) break;

		const auto& abbrev = it->second;
		bool isSubprogram = (abbrev.tag == DW_TAG_subprogram);
		bool isVar = (abbrev.tag == DW_TAG_variable || abbrev.tag == DW_TAG_formal_parameter);

		// Values we want to extract from attributes
		std::string varName;
		uint32_t lowPC = 0, highPC = 0;
		bool hasLowPC = false, hasHighPC = false;
		bool highPCIsOffset = false;
		std::string location;

		for (const auto& attr : abbrev.attrs) {
			const uint8_t* attrStart = p;

			if (attr.attr == DW_AT_name) {
				if (attr.form == DW_FORM_string) {
					varName = std::string((const char*)p);
					while (p < unit_end && *p) p++;
					if (p < unit_end) p++;
				} else if (attr.form == DW_FORM_strp) {
					uint32_t offset = readU32LE(p); p += 4;
					if (offset < strData.size())
						varName = std::string((const char*)&strData[offset]);
				} else {
					skipForm(attr.form, p, unit_end, addr_size, version);
				}
			} else if (attr.attr == DW_AT_low_pc) {
				if (attr.form == DW_FORM_addr) {
					lowPC = (addr_size == 2) ? readU16LE(p) : readU32LE(p);
					p += addr_size;
					hasLowPC = true;
				} else {
					skipForm(attr.form, p, unit_end, addr_size, version);
				}
			} else if (attr.attr == DW_AT_high_pc) {
				if (attr.form == DW_FORM_addr) {
					highPC = (addr_size == 2) ? readU16LE(p) : readU32LE(p);
					p += addr_size;
					hasHighPC = true;
				} else if (attr.form == DW_FORM_data1 || attr.form == DW_FORM_data2 ||
				           attr.form == DW_FORM_data4 || attr.form == DW_FORM_udata) {
					// high_pc as offset from low_pc
					if (attr.form == DW_FORM_data1) { highPC = *p++; }
					else if (attr.form == DW_FORM_data2) { highPC = readU16LE(p); p += 2; }
					else if (attr.form == DW_FORM_data4) { highPC = readU32LE(p); p += 4; }
					else { highPC = (uint32_t)readULEB128(p, unit_end); }
					hasHighPC = true;
					highPCIsOffset = true;
				} else {
					skipForm(attr.form, p, unit_end, addr_size, version);
				}
			} else if (attr.attr == DW_AT_location && isVar) {
				if (attr.form == DW_FORM_exprloc) {
					uint64_t len = readULEB128(p, unit_end);
					location = parseDwarfExpr(p, len);
					p += len;
				} else if (attr.form == DW_FORM_block1) {
					uint8_t len = *p++;
					location = parseDwarfExpr(p, len);
					p += len;
				} else if (attr.form == DW_FORM_block2) {
					uint16_t len = readU16LE(p); p += 2;
					location = parseDwarfExpr(p, len);
					p += len;
				} else if (attr.form == DW_FORM_block4) {
					uint32_t len = readU32LE(p); p += 4;
					location = parseDwarfExpr(p, len);
					p += len;
				} else if (attr.form == DW_FORM_block) {
					uint64_t len = readULEB128(p, unit_end);
					location = parseDwarfExpr(p, len);
					p += len;
				} else if (attr.form == DW_FORM_sec_offset && !locData.empty()) {
					uint32_t locOffset = readU32LE(p); p += 4;
					auto locEntries = parseLocList(locData, locOffset, addr_size);
					if (!locEntries.empty())
						location = std::get<2>(locEntries[0]);
				} else {
					skipForm(attr.form, p, unit_end, addr_size, version);
				}
			} else {
				skipForm(attr.form, p, unit_end, addr_size, version);
			}

			// Safety: if we didn't advance and the form isn't zero-size, we're stuck
			if (p == attrStart && attr.form != DW_FORM_flag_present) break;
		}

		// Process the DIE
		if (isSubprogram) {
			inFunction = true;
			if (hasLowPC) {
				funcLowPC = lowPC;
				funcHighPC = hasHighPC ? (highPCIsOffset ? lowPC + highPC : highPC) : lowPC + 0xFFFF;
			}
		}



		if (isVar && !varName.empty() && !location.empty()) {
			uint32_t start, finish;
			if (hasLowPC) {
				start = lowPC;
				finish = hasHighPC ? (highPCIsOffset ? lowPC + highPC : highPC) : lowPC + 0xFFFF;
			} else if (inFunction) {
				start = funcLowPC;
				finish = funcHighPC;
			} else {
				start = 0;
				finish = 0xFFFF;
			}
			// Byte addresses → word addresses
			addVarMapping(start / 2, finish / 2, varName, location);
		}

		if (abbrev.hasChildren)
			depthStack.push_back(isSubprogram);
	}
}

bool ANEMInstructionMemory::loadELF(const std::string &fileName, addr_t &entryAddr,
                                     ANEMDataMemory *dmem)
{
	entryAddr = 0;
	FILE *f = fopen(fileName.c_str(), "rb");
	if (!f)
		return false;

	// Read and validate ELF header
	Elf32_Ehdr ehdr;
	if (fread(&ehdr, sizeof(ehdr), 1, f) != 1) {
		fclose(f);
		return false;
	}

	// Verify ELF magic
	if (ehdr.e_ident[0] != 0x7F || ehdr.e_ident[1] != 'E' ||
	    ehdr.e_ident[2] != 'L'  || ehdr.e_ident[3] != 'F') {
		fclose(f);
		return false;
	}

	// Verify ELF32, little-endian
	if (ehdr.e_ident[4] != 1 || ehdr.e_ident[5] != 1) {
		std::cerr << "ELF: expected ELF32 little-endian" << std::endl;
		fclose(f);
		return false;
	}

	if (ehdr.e_shoff == 0 || ehdr.e_shnum == 0) {
		std::cerr << "ELF: no section headers" << std::endl;
		fclose(f);
		return false;
	}

	// Read all section headers
	std::vector<Elf32_Shdr> shdrs(ehdr.e_shnum);
	fseek(f, ehdr.e_shoff, SEEK_SET);
	for (uint16_t i = 0; i < ehdr.e_shnum; i++) {
		fseek(f, ehdr.e_shoff + i * ehdr.e_shentsize, SEEK_SET);
		if (fread(&shdrs[i], sizeof(Elf32_Shdr), 1, f) != 1) {
			fclose(f);
			return false;
		}
	}

	// Read section header string table
	auto &shstr_hdr = shdrs[ehdr.e_shstrndx];
	std::vector<char> shstrtab(shstr_hdr.sh_size);
	fseek(f, shstr_hdr.sh_offset, SEEK_SET);
	if (fread(shstrtab.data(), shstr_hdr.sh_size, 1, f) != 1) {
		fclose(f);
		return false;
	}

	// Find .text section and load it
	int text_idx = -1;
	std::vector<uint8_t> text_data;

	for (uint16_t i = 0; i < ehdr.e_shnum; i++) {
		if (shdrs[i].sh_type != SHT_PROGBITS)
			continue;
		const char *name = &shstrtab[shdrs[i].sh_name];
		if (strcmp(name, ".text") != 0)
			continue;

		text_idx = i;
		text_data.resize(shdrs[i].sh_size);
		fseek(f, shdrs[i].sh_offset, SEEK_SET);
		if (fread(text_data.data(), shdrs[i].sh_size, 1, f) != 1) {
			fclose(f);
			return false;
		}
		break;
	}

	if (text_idx < 0) {
		std::cerr << "ELF: no .text section found" << std::endl;
		fclose(f);
		return false;
	}

	// Find and load data sections (.rodata, .data) into data memory.
	// Assign data memory addresses starting from DATA_BASE.
	constexpr addr_t DATA_BASE = 0x2000;
	// Map section index → assigned data memory base address (word addr)
	std::map<int, addr_t> dataSectionBases;
	addr_t nextDataAddr = DATA_BASE;

	if (dmem) {
		for (uint16_t i = 0; i < ehdr.e_shnum; i++) {
			if (shdrs[i].sh_size == 0)
				continue;
			const char *name = &shstrtab[shdrs[i].sh_name];

			if (shdrs[i].sh_type == SHT_PROGBITS &&
				(strcmp(name, ".rodata") == 0 || strcmp(name, ".data") == 0)) {
				// Read section content
				std::vector<uint8_t> secData(shdrs[i].sh_size);
				fseek(f, shdrs[i].sh_offset, SEEK_SET);
				if (fread(secData.data(), shdrs[i].sh_size, 1, f) != 1)
					continue;

				// Assign base address and record mapping
				dataSectionBases[i] = nextDataAddr;

				// Load 16-bit LE words contiguously into data memory.
				uint32_t numWords = shdrs[i].sh_size / 2;
				for (uint32_t w = 0; w < numWords; w++) {
					uint16_t word = secData[w * 2] | (secData[w * 2 + 1] << 8);
					dmem->writeDirect(nextDataAddr + w, word);
				}

				std::cerr << "ELF: loaded " << name << " (" << numWords
				          << " words) at data address 0x" << std::hex
				          << nextDataAddr << std::dec << std::endl;

				nextDataAddr += numWords;
			} else if (shdrs[i].sh_type == SHT_NOBITS &&
					   strcmp(name, ".bss") == 0) {
				// BSS: assign address (already zeroed in dmem), no content to load
				dataSectionBases[i] = nextDataAddr;
				uint32_t numWords = shdrs[i].sh_size / 2;

				std::cerr << "ELF: reserved " << name << " (" << numWords
				          << " words) at data address 0x" << std::hex
				          << nextDataAddr << std::dec << std::endl;

				nextDataAddr += numWords;
			}
		}
	}

	// Find symbol table (needed for relocations and entry point)
	std::vector<Elf32_Sym> symtab;
	std::vector<char> strtab;
	for (uint16_t i = 0; i < ehdr.e_shnum; i++) {
		if (shdrs[i].sh_type != SHT_SYMTAB)
			continue;
		uint32_t num_syms = shdrs[i].sh_size / sizeof(Elf32_Sym);
		symtab.resize(num_syms);
		fseek(f, shdrs[i].sh_offset, SEEK_SET);
		if (fread(symtab.data(), sizeof(Elf32_Sym), num_syms, f) != num_syms) {
			symtab.clear();
		}
		// sh_link points to the associated string table
		if (shdrs[i].sh_link < ehdr.e_shnum) {
			auto &strhdr = shdrs[shdrs[i].sh_link];
			strtab.resize(strhdr.sh_size);
			fseek(f, strhdr.sh_offset, SEEK_SET);
			if (fread(strtab.data(), strhdr.sh_size, 1, f) != 1)
				strtab.clear();
		}
		break;
	}

	// Find _start symbol entry point
	for (size_t i = 0; i < symtab.size(); i++) {
		if (symtab[i].st_name < strtab.size() &&
		    strcmp(&strtab[symtab[i].st_name], "_start") == 0) {
			entryAddr = symtab[i].st_value / 2;  // byte addr → word addr
			break;
		}
	}

	// Helper: resolve symbol address, accounting for data section relocation.
	// Returns a BYTE address — the LLVM backend inserts SRL-by-1 at runtime
	// to convert byte addresses to hardware word addresses.
	auto resolveSymAddr = [&](uint32_t sym_idx, int32_t addend) -> uint32_t {
		auto &sym = symtab[sym_idx];

		// Check if symbol is in a data section that was assigned an address
		uint16_t shndx = sym.st_shndx;
		auto it = dataSectionBases.find(shndx);
		if (it != dataSectionBases.end()) {
			// Data loaded contiguously at word address DATA_BASE.
			// Byte address = word_address * 2 = DATA_BASE * 2 + st_value + addend
			// (st_value is already a byte offset within the section)
			return it->second * 2 + sym.st_value + addend;
		}

		// Symbol in .text — return byte address as-is (for PC-relative calc)
		return sym.st_value + addend;
	};

	// Apply RELA relocations for .text
	for (uint16_t i = 0; i < ehdr.e_shnum; i++) {
		if (shdrs[i].sh_type != SHT_RELA)
			continue;
		// sh_info points to the section being relocated
		if (shdrs[i].sh_info != (uint32_t)text_idx)
			continue;

		uint32_t num_relas = shdrs[i].sh_size / sizeof(Elf32_Rela);
		std::vector<Elf32_Rela> relas(num_relas);
		fseek(f, shdrs[i].sh_offset, SEEK_SET);
		if (fread(relas.data(), sizeof(Elf32_Rela), num_relas, f) != num_relas)
			continue;

		for (auto &rela : relas) {
			uint32_t sym_idx = rela.r_info >> 8;
			uint8_t rtype = rela.r_info & 0xFF;

			if (sym_idx >= symtab.size())
				continue;

			uint32_t fixup_addr = rela.r_offset;

			if (rtype == 0 || rtype == 1) {
				// PC-relative fixup (types 0 and 1)
				// Type 0 (J/JAL):   target = PC + 1 + offset → offset = Value/2 - 1
				// Type 1 (BZ/BHLEQ): target = PC + 2 + offset → offset = Value/2 - 2
				uint32_t target_addr = symtab[sym_idx].st_value + rela.r_addend;
				int32_t value = (int32_t)target_addr - (int32_t)fixup_addr;
				int32_t adjustment = (rtype == 1) ? 2 : 1;
				int32_t word_offset = value / 2 - adjustment;
				uint16_t encoded = (uint16_t)word_offset & 0xFFF;

				// Patch lower 12 bits of the 16-bit LE instruction
				if (fixup_addr + 1 < text_data.size()) {
					text_data[fixup_addr] = encoded & 0xFF;
					text_data[fixup_addr + 1] =
						(text_data[fixup_addr + 1] & 0xF0) | ((encoded >> 8) & 0x0F);
				}
			} else if (rtype == 2) {
				// hi8: upper byte of absolute data address (for LIU)
				uint32_t addr = resolveSymAddr(sym_idx, rela.r_addend);
				if (fixup_addr < text_data.size()) {
					text_data[fixup_addr] = (addr >> 8) & 0xFF;
				}
			} else if (rtype == 3) {
				// lo8: lower byte of absolute data address (for LIL)
				uint32_t addr = resolveSymAddr(sym_idx, rela.r_addend);
				if (fixup_addr < text_data.size()) {
					text_data[fixup_addr] = addr & 0xFF;
				}
			}
		}
	}

	// Export symbols to disassembler
	for (size_t i = 0; i < symtab.size(); i++) {
		uint8_t type = symtab[i].st_info & 0xF;
		// Include FUNC, OBJECT, and NOTYPE symbols with non-empty names
		if (type == 0 || type == 1 || type == 2) {
			if (symtab[i].st_name < strtab.size()) {
				const char* name = &strtab[symtab[i].st_name];
				if (name[0] != '\0') {
					addr_t addr = symtab[i].st_value / 2;  // byte → word addr
					addSymbol(addr, name);
				}
			}
		}
	}

	// Parse DWARF .debug_line for source location mapping
	for (uint16_t i = 0; i < ehdr.e_shnum; i++) {
		if (shdrs[i].sh_name >= shstrtab.size()) continue;
		const char* name = &shstrtab[shdrs[i].sh_name];
		if (strcmp(name, ".debug_line") != 0) continue;

		std::vector<uint8_t> debugLineData(shdrs[i].sh_size);
		fseek(f, shdrs[i].sh_offset, SEEK_SET);
		if (fread(debugLineData.data(), shdrs[i].sh_size, 1, f) != 1) break;

		// Find .rela.debug_line
		std::vector<Elf32_Rela> debugLineRelas;
		for (uint16_t j = 0; j < ehdr.e_shnum; j++) {
			if (shdrs[j].sh_type == SHT_RELA && shdrs[j].sh_info == i) {
				uint32_t num = shdrs[j].sh_size / sizeof(Elf32_Rela);
				debugLineRelas.resize(num);
				fseek(f, shdrs[j].sh_offset, SEEK_SET);
				if (fread(debugLineRelas.data(), sizeof(Elf32_Rela), num, f) != num)
					debugLineRelas.clear();
				break;
			}
		}

		parseDwarfLine(debugLineData, symtab, debugLineRelas);
		std::cerr << "ELF: parsed .debug_line section" << std::endl;
		break;
	}

	// Parse DWARF .debug_info for variable-to-register mapping
	for (uint16_t i = 0; i < ehdr.e_shnum; i++) {
		if (shdrs[i].sh_name >= shstrtab.size()) continue;
		const char* name = &shstrtab[shdrs[i].sh_name];
		if (strcmp(name, ".debug_info") != 0) continue;

		std::vector<uint8_t> debugInfoData(shdrs[i].sh_size);
		fseek(f, shdrs[i].sh_offset, SEEK_SET);
		if (fread(debugInfoData.data(), shdrs[i].sh_size, 1, f) != 1) break;

		// Find .debug_abbrev
		std::vector<uint8_t> abbrevData;
		for (uint16_t j = 0; j < ehdr.e_shnum; j++) {
			if (shdrs[j].sh_name >= shstrtab.size()) continue;
			if (strcmp(&shstrtab[shdrs[j].sh_name], ".debug_abbrev") == 0) {
				abbrevData.resize(shdrs[j].sh_size);
				fseek(f, shdrs[j].sh_offset, SEEK_SET);
				if (fread(abbrevData.data(), shdrs[j].sh_size, 1, f) != 1)
					abbrevData.clear();
				break;
			}
		}

		// Find .debug_str (optional)
		std::vector<uint8_t> strData;
		for (uint16_t j = 0; j < ehdr.e_shnum; j++) {
			if (shdrs[j].sh_name >= shstrtab.size()) continue;
			if (strcmp(&shstrtab[shdrs[j].sh_name], ".debug_str") == 0) {
				strData.resize(shdrs[j].sh_size);
				fseek(f, shdrs[j].sh_offset, SEEK_SET);
				if (fread(strData.data(), shdrs[j].sh_size, 1, f) != 1)
					strData.clear();
				break;
			}
		}

		// Find .debug_loc (optional)
		std::vector<uint8_t> locData;
		for (uint16_t j = 0; j < ehdr.e_shnum; j++) {
			if (shdrs[j].sh_name >= shstrtab.size()) continue;
			if (strcmp(&shstrtab[shdrs[j].sh_name], ".debug_loc") == 0) {
				locData.resize(shdrs[j].sh_size);
				fseek(f, shdrs[j].sh_offset, SEEK_SET);
				if (fread(locData.data(), shdrs[j].sh_size, 1, f) != 1)
					locData.clear();
				break;
			}
		}

		// Find .rela.debug_info
		std::vector<Elf32_Rela> debugInfoRelas;
		for (uint16_t j = 0; j < ehdr.e_shnum; j++) {
			if (shdrs[j].sh_type == SHT_RELA && shdrs[j].sh_info == i) {
				uint32_t num = shdrs[j].sh_size / sizeof(Elf32_Rela);
				debugInfoRelas.resize(num);
				fseek(f, shdrs[j].sh_offset, SEEK_SET);
				if (fread(debugInfoRelas.data(), sizeof(Elf32_Rela), num, f) != num)
					debugInfoRelas.clear();
				break;
			}
		}

		if (!abbrevData.empty()) {
			parseDwarfInfo(debugInfoData, abbrevData, strData, locData, symtab, debugInfoRelas);
			std::cerr << "ELF: parsed .debug_info section (variable mappings)" << std::endl;
		}
		break;
	}

	fclose(f);

	// Load patched .text data into instruction memory
	uint32_t base_addr = shdrs[text_idx].sh_addr / 2;  // byte addr → word addr
	uint32_t num_words = text_data.size() / 2;
	for (uint32_t w = 0; w < num_words; w++) {
		uint16_t word = text_data[w * 2] | (text_data[w * 2 + 1] << 8);
		if (base_addr + w < this->size)
			this->imem[base_addr + w] = ANEMInstruction(word);
	}

	return true;
}

addr_t ANEMInstructionMemory::loadProgram(std::string fileName, ANEMDataMemory *dmem)
{
	// Try ELF format first (binary file with 0x7F ELF magic)
	addr_t entry = 0;
	if (this->loadELF(fileName, entry, dmem))
		return entry;

	std::ifstream file(fileName);
	std::string line;
	std::smatch sm;

	//hex file format regex
	std::regex ihex("^:([a-fA-F0-9]{2})([a-fA-F0-9]{4})([a-fA-F0-9]{2})([a-fA-F0-9]*)([a-fA-F0-9]{2})");
	//binary format regex (address + instruction)
	std::regex bin("([01]+)[\\t ]+([01]{16})$");
	//plain sequential binary (instruction only, no address)
	std::regex plainbin("^([01]{16})$");

	//line count
	unsigned int i = 0;
	bool ihex_f = false, bin_f = false, plainbin_f = false;

	if (file.is_open() == false)
	{

		//bad
		throw ANEM_PROGRAM_LOAD_EXCEPT;

	}

	while (std::getline(file,line))
	{

		if (i == 0)
		{
			//first line, try to match format
			std::regex_match(line,sm,ihex);

			if (sm.size() > 0)
			{

				//ihex format
				ihex_f = true;
			}
			else
			{

				std::regex_match(line,sm,bin);

				if (sm.size() > 0)
				{

					//bin format
					bin_f = true;
				}
				else
				{
					std::regex_match(line,sm,plainbin);
					if (sm.size() > 0)
					{
						plainbin_f = true;
					}
				}

			}

		}

		//parse line and add instructions
		if (ihex_f)
		{

			//parse ihex
			std::regex_match(line,sm,ihex);

			//first group is data field size
			unsigned int d_size = std::stoi(sm[1].str(),nullptr,16);

			//second group is starting address
			uint32_t s_addr = std::stoi(sm[2].str(),nullptr,16);

			//third group is data type
			unsigned int d_type = std::stoi(sm[3].str(),nullptr,16);

			//verify data type
			if (d_type == 1)
			{

				//end of file
				return 0;

			}

			//fourth group is instructions, must divide into substrings and convert
			std::string sub;
			unsigned int substr = 0;
			while (d_size > 0)
			{

				sub = sm[4].str().substr(substr,2);

				this->imem[s_addr++] = ANEMInstruction(std::stoi(sub,nullptr,16));

				substr += 2;

				d_size -= 2;

			}

			//fifth group is checksum
			(void)std::stoi(sm[5].str(),nullptr,16);

		} else
		{

			if (bin_f)
			{

				//parse bin
				std::regex_match(line,sm,bin);

				//first group is the address
				addr_t i_addr = std::stoi(sm[1].str(),nullptr,2);

				//second group is the instruction
				this->imem[i_addr] = ANEMInstruction(std::stoi(sm[2].str(),nullptr,2));

			}
			else if (plainbin_f)
			{
				//plain sequential binary — instruction only, address = line number
				std::regex_match(line,sm,plainbin);
				if (sm.size() > 0)
					this->imem[i] = ANEMInstruction(std::stoi(sm[1].str(),nullptr,2));
			}
			else
			{

				//dont know what is this, exception
				///@todo insert exception here

			}

		}

		i++;

	}

	return 0;
}

bool ANEMDataMemory::attachPeripheral(addr_t address, ANEMMemMappedPeripheral *p)
{

	unsigned int a_range = 0;

	//must be outside of memory range
	if (address <= this->size) return false;

	//verify if this address is available
	auto it = this->vmem.find(address);

	if (it != this->vmem.end())
	{

		//already allocated, fail
		return false;

	}

	//check if all addresses in range are available
	for (unsigned int i = 1; i < p->getLength(); i++)
	{
		it = this->vmem.find(address + i);
		if (it != this->vmem.end())
			return false;
	}

	//all good, set base address
	p->setBaseAddress(address);

	//register all addresses in range
	a_range = p->getLength();

	while (a_range > 0)
	{
		this->vmem[address+(--a_range)] = p;
	}

	return true;
}

std::vector<std::pair<addr_t, std::string>> ANEMDataMemory::listPeripherals() const
{
	// Each peripheral may be registered at multiple addresses (one per byte in range).
	// Deduplicate by pointer to get unique peripherals with their base address.
	std::set<ANEMMemMappedPeripheral*> seen;
	std::vector<std::pair<addr_t, std::string>> result;

	for (const auto& kv : this->vmem)
	{
		if (seen.insert(kv.second).second)
		{
			result.push_back({kv.second->getBaseAddress(), kv.second->getName()});
		}
	}

	return result;
}
