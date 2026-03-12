/*
 * @file web_server.cpp
 * @brief ANEM16 web debug server using libwebsockets
 */

#include "web_server.h"
#include "disasm.h"
#include "periph/gpio.h"
#include "periph/timer.h"
#include "periph/uart.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <filesystem>

using json = jsonrpc::json;

// ============================================================
// Single lws callback — handles both HTTP and WebSocket
// ============================================================

int ANEMWebServer::lwsCallback(struct lws* wsi, enum lws_callback_reasons reason,
                                void* user, void* in, size_t len)
{
	auto* self = (ANEMWebServer*)lws_context_user(lws_get_context(wsi));

	switch (reason) {

	// ---- HTTP: serve static files ----
	case LWS_CALLBACK_HTTP: {
		if (!self) return -1;

		// Route request to content
		const std::string* content = &self->htmlContent;
		const char* contentType = "text/html; charset=utf-8";
		size_t contentTypeLen = 24;

		{
			// lws mount may strip the leading '/'; use the HTTP header for reliable URI
			char uriBuf[256] = {};
			lws_hdr_copy(wsi, uriBuf, sizeof(uriBuf), WSI_TOKEN_GET_URI);
			std::string uri(uriBuf);
			if (uri == "/isa_ref.json") {
				content = &self->isaRefContent;
				contentType = "application/json";
				contentTypeLen = 16;
			}
		}

		uint8_t buf[LWS_PRE + 512];
		uint8_t *start = &buf[LWS_PRE], *p = start;
		uint8_t *end = &buf[sizeof(buf) - 1];

		if (lws_add_http_header_status(wsi, HTTP_STATUS_OK, &p, end) ||
		    lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE,
		        (const uint8_t*)contentType, (int)contentTypeLen, &p, end) ||
		    lws_add_http_header_content_length(wsi, (lws_filepos_t)content->size(), &p, end) ||
		    lws_finalize_http_header(wsi, &p, end))
			return 1;

		if (lws_write(wsi, start, (size_t)(p - start), LWS_WRITE_HTTP_HEADERS) < 0)
			return 1;

		self->httpSessions[wsi] = {0, content};
		lws_callback_on_writable(wsi);
		return 0;
	}

	case LWS_CALLBACK_HTTP_WRITEABLE: {
		if (!self) return -1;
		auto it = self->httpSessions.find(wsi);
		if (it == self->httpSessions.end()) return -1;

		auto& sess = it->second;
		const size_t totalLen = sess.content->size();
		size_t pos = sess.pos;

		if (pos >= totalLen) {
			self->httpSessions.erase(it);
			if (lws_http_transaction_completed(wsi))
				return -1;
			return 0;
		}

		size_t remaining = totalLen - pos;
		size_t chunk = std::min(remaining, (size_t)4096);

		std::vector<uint8_t> writeBuf(LWS_PRE + chunk);
		memcpy(writeBuf.data() + LWS_PRE, sess.content->data() + pos, chunk);

		int writeFlags = LWS_WRITE_HTTP;
		if (pos + chunk >= totalLen)
			writeFlags |= LWS_WRITE_HTTP_FINAL;

		int n = lws_write(wsi, writeBuf.data() + LWS_PRE, chunk,
		                  (enum lws_write_protocol)writeFlags);
		if (n < 0) {
			self->httpSessions.erase(it);
			return -1;
		}

		sess.pos += chunk;
		if (sess.pos < totalLen)
			lws_callback_on_writable(wsi);
		else {
			self->httpSessions.erase(it);
			if (lws_http_transaction_completed(wsi))
				return -1;
		}
		return 0;
	}

	case LWS_CALLBACK_CLOSED_HTTP:
		if (self) self->httpSessions.erase(wsi);
		return 0;

	// ---- WebSocket ----
	case LWS_CALLBACK_ESTABLISHED:
		if (!self) return 0;
		self->wsClients.push_back(wsi);
		self->wsSendQueues[wsi] = {};
		return 0;

	case LWS_CALLBACK_CLOSED:
		if (!self) return 0;
		self->wsClients.erase(
			std::remove(self->wsClients.begin(), self->wsClients.end(), wsi),
			self->wsClients.end());
		self->wsSendQueues.erase(wsi);
		self->wsRecvBufs.erase(wsi);
		return 0;

	case LWS_CALLBACK_RECEIVE: {
		if (!self) return 0;
		std::string msg;
		if (!lws_is_final_fragment(wsi)) {
			self->wsRecvBufs[wsi].append((const char*)in, len);
			return 0;
		}

		auto bufIt = self->wsRecvBufs.find(wsi);
		if (bufIt != self->wsRecvBufs.end()) {
			bufIt->second.append((const char*)in, len);
			msg = std::move(bufIt->second);
			self->wsRecvBufs.erase(bufIt);
		} else {
			msg.assign((const char*)in, len);
		}

		auto req = jsonrpc::parseRequest(msg);
		json response;
		if (!req.valid) {
			response = jsonrpc::makeError(nullptr, -32700,
				"Parse error: " + req.parseError);
		} else {
			try {
				response = self->dispatch(req.method, req.params, req.id);
			} catch (const json::exception& e) {
				response = jsonrpc::makeError(req.id, -32602,
					std::string("Invalid params: ") + e.what());
			} catch (const std::exception& e) {
				response = jsonrpc::makeError(req.id, -1,
					std::string("Simulator error: ") + e.what());
			}
		}

		self->wsSendQueues[wsi].push(response.dump());
		lws_callback_on_writable(wsi);
		return 0;
	}

	case LWS_CALLBACK_SERVER_WRITEABLE: {
		if (!self) return 0;
		auto qIt = self->wsSendQueues.find(wsi);
		if (qIt == self->wsSendQueues.end() || qIt->second.empty())
			return 0;

		const std::string& msg = qIt->second.front();
		std::vector<uint8_t> buf(LWS_PRE + msg.size());
		memcpy(buf.data() + LWS_PRE, msg.data(), msg.size());

		int n = lws_write(wsi, buf.data() + LWS_PRE, msg.size(), LWS_WRITE_TEXT);
		qIt->second.pop();
		if (n < 0) return -1;

		if (!qIt->second.empty())
			lws_callback_on_writable(wsi);
		return 0;
	}

	default:
		break;
	}

	return lws_callback_http_dummy(wsi, reason, user, in, len);
}

// ============================================================
// Constructor / Destructor
// ============================================================

ANEMWebServer::ANEMWebServer(DebugEngine& engine, int port)
	: engine(engine)
	, port(port)
{
	// Subscribe to state changes from any source (e.g. GDB stub)
	engine.addStateChangeCallback([this]() {
		if (simRunning.load()) return; // web UI sim loop handles its own updates
		externalStateChanged.store(true, std::memory_order_relaxed);
		if (lwsCtx)
			lws_cancel_service(lwsCtx);
	});
}

ANEMWebServer::~ANEMWebServer()
{
	if (simThread.joinable()) {
		simThread.request_stop();
		simThread.join();
	}
	if (lwsCtx)
		lws_context_destroy(lwsCtx);
}

// ============================================================
// Main Event Loop
// ============================================================

void ANEMWebServer::run()
{
	// Locate web/index.html: try CWD, then relative to executable
	namespace fs = std::filesystem;
	std::vector<fs::path> searchPaths = {
		fs::current_path() / "web" / "index.html",
		fs::current_path() / ".." / "web" / "index.html",
	};
	// Also try relative to /proc/self/exe on Linux
	if (fs::exists("/proc/self/exe")) {
		auto exeDir = fs::read_symlink("/proc/self/exe").parent_path();
		searchPaths.push_back(exeDir / ".." / "web" / "index.html");
		searchPaths.push_back(exeDir / "web" / "index.html");
	}

	bool loaded = false;
	for (const auto& path : searchPaths) {
		std::ifstream f(path);
		if (f.good()) {
			htmlContent.assign(std::istreambuf_iterator<char>(f), {});
			webRoot = path.parent_path().string();
			htmlFilePath = fs::canonical(path).string();
			htmlLastMod = fs::last_write_time(htmlFilePath);
			std::cerr << "Web UI loaded from " << htmlFilePath << std::endl;
			loaded = true;
			break;
		}
	}
	// Load ISA reference JSON from same directory
	{
		auto isaPath = fs::path(webRoot) / "isa_ref.json";
		std::ifstream f(isaPath);
		if (f.good()) {
			isaRefContent.assign(std::istreambuf_iterator<char>(f), {});
			std::cerr << "ISA reference loaded from " << isaPath << std::endl;
		}
	}

	if (!loaded) {
		std::cerr << "Error: could not find web/index.html" << std::endl;
		std::cerr << "Searched:" << std::endl;
		for (const auto& p : searchPaths)
			std::cerr << "  " << p << std::endl;
		return;
	}

	static struct lws_protocols protocols[] = {
		{ "http", lwsCallback, 0, 65536 },
		{ NULL, NULL, 0, 0 }
	};

	static struct lws_http_mount mount;
	memset(&mount, 0, sizeof(mount));
	mount.mountpoint = "/";
	mount.protocol = "http";
	mount.origin_protocol = LWSMPRO_CALLBACK;
	mount.mountpoint_len = 1;

	struct lws_context_creation_info info;
	memset(&info, 0, sizeof(info));
	info.port = port;
	info.protocols = protocols;
	info.mounts = &mount;
	info.user = this;

	lws_set_log_level(LLL_ERR | LLL_WARN, nullptr);

	lwsCtx = lws_create_context(&info);
	if (!lwsCtx) {
		std::cerr << "Failed to create lws context" << std::endl;
		return;
	}

	std::cout << "ANEM16 Web UI: http://localhost:" << port << std::endl;

	while (true) {
		lws_service(lwsCtx, 50);
		drainNotifications();
		checkLiveReload();

		// Push update to all WS clients when state changed externally (e.g. GDB)
		if (externalStateChanged.exchange(false)) {
			std::string notify = R"({"jsonrpc":"2.0","method":"stateChanged","params":{}})";
			for (auto* wsi : wsClients) {
				wsSendQueues[wsi].push(notify);
				lws_callback_on_writable(wsi);
			}
		}
	}
}

// ============================================================
// Notification Drain + Broadcast
// ============================================================

void ANEMWebServer::drainNotifications()
{
	std::lock_guard<std::mutex> lock(notifyMutex);
	while (!notifyQueue.empty()) {
		std::string msg = std::move(notifyQueue.front());
		notifyQueue.pop();
		for (auto* wsi : wsClients) {
			wsSendQueues[wsi].push(msg);
			lws_callback_on_writable(wsi);
		}
	}
}

// ============================================================
// Live Reload
// ============================================================

void ANEMWebServer::checkLiveReload()
{
	if (htmlFilePath.empty()) return;

	namespace fs = std::filesystem;
	std::error_code ec;
	auto mod = fs::last_write_time(htmlFilePath, ec);
	if (ec || mod == htmlLastMod) return;

	// File changed — re-read it
	std::ifstream f(htmlFilePath);
	if (!f.good()) return;

	htmlContent.assign(std::istreambuf_iterator<char>(f), {});
	htmlLastMod = mod;
	std::cerr << "Live reload: " << htmlFilePath << std::endl;

	// Notify all WebSocket clients to reload
	json note = jsonrpc::makeNotification("reload", json::object());
	std::string msg = note.dump();
	for (auto* wsi : wsClients) {
		wsSendQueues[wsi].push(msg);
		lws_callback_on_writable(wsi);
	}
}

// ============================================================
// Source File Cache
// ============================================================

static const std::string emptyLine;

const std::string& ANEMWebServer::getSourceLine(const std::string& file, int line)
{
	auto it = sourceCache.find(file);
	if (it == sourceCache.end()) {
		// Try to load the file — search source root, CWD, and common relative paths
		namespace fs = std::filesystem;
		std::vector<fs::path> searchPaths;
		if (!sourceRoot.empty())
			searchPaths.push_back(fs::path(sourceRoot) / file);
		searchPaths.push_back(fs::current_path() / file);
		searchPaths.push_back(fs::current_path() / ".." / file);
		// Also try the file as an absolute path
		searchPaths.push_back(fs::path(file));
		if (fs::exists("/proc/self/exe")) {
			auto exeDir = fs::read_symlink("/proc/self/exe").parent_path();
			searchPaths.push_back(exeDir / ".." / file);
		}

		std::vector<std::string> lines;
		for (const auto& path : searchPaths) {
			std::ifstream f(path);
			if (f.good()) {
				std::string l;
				while (std::getline(f, l))
					lines.push_back(l);
				std::cerr << "Source loaded: " << fs::canonical(path)
				          << " (" << lines.size() << " lines)" << std::endl;
				break;
			}
		}
		it = sourceCache.emplace(file, std::move(lines)).first;
	}

	const auto& lines = it->second;
	if (line > 0 && (size_t)line <= lines.size())
		return lines[line - 1]; // 1-based to 0-based
	return emptyLine;
}

// ============================================================
// Helpers
// ============================================================

std::string ANEMWebServer::toHex(uint32_t val, int width)
{
	std::ostringstream ss;
	ss << "0x" << std::hex << std::setfill('0') << std::setw(width) << val;
	return ss.str();
}

uint32_t ANEMWebServer::fromHex(const json& val)
{
	if (val.is_string()) {
		std::string s = val.get<std::string>();
		return (uint32_t)std::stoul(s, nullptr, 0);
	}
	return val.get<uint32_t>();
}

json ANEMWebServer::serializeStepResult(const StepResult& r)
{
	json result;
	result["pc"] = toHex(r.pc);
	result["cycle"] = r.cycle;
	result["halted"] = r.halted;

	switch (r.reason) {
	case StopReason::None:       result["stopped_reason"] = "none"; break;
	case StopReason::Breakpoint: result["stopped_reason"] = "breakpoint"; break;
	case StopReason::Watchpoint: result["stopped_reason"] = "watchpoint"; break;
	case StopReason::Halted:     result["stopped_reason"] = "halted"; break;
	}

	if (r.watchAccess) {
		result["watch_access"] = {
			{"address", toHex(r.watchAccess->address)},
			{"value", toHex(r.watchAccess->value)},
			{"write", r.watchAccess->write}
		};
	}
	return result;
}

// ============================================================
// JSON-RPC Dispatch
// ============================================================

json ANEMWebServer::dispatch(const std::string& method, const json& params,
                              const json& id)
{
	if (simRunning.load()) {
		if (method == "pause")
			return jsonrpc::makeResponse(id, handlePause());
		if (method == "status")
			return jsonrpc::makeResponse(id, handleStatus());
		if (method == "breakpoint.add")
			return jsonrpc::makeResponse(id, handleBreakpointAdd(params));
		if (method == "breakpoint.remove")
			return jsonrpc::makeResponse(id, handleBreakpointRemove(params));
		if (method == "breakpoint.list")
			return jsonrpc::makeResponse(id, handleBreakpointList());
		if (method == "watchpoint.add")
			return jsonrpc::makeResponse(id, handleWatchpointAdd(params));
		if (method == "watchpoint.remove")
			return jsonrpc::makeResponse(id, handleWatchpointRemove(params));
		if (method == "watchpoint.list")
			return jsonrpc::makeResponse(id, handleWatchpointList());
		if (method == "interrupt")
			return jsonrpc::makeResponse(id, handleInterrupt(params));
		return jsonrpc::makeError(id, -2, "Simulator is running");
	}

	if (method == "step")           return jsonrpc::makeResponse(id, handleStep(params));
	if (method == "continue")       return handleContinue(id);
	if (method == "pause")          return jsonrpc::makeError(id, -3, "Not running");
	if (method == "registers")      return jsonrpc::makeResponse(id, handleRegisters(params));
	if (method == "memory.read")    return jsonrpc::makeResponse(id, handleMemoryRead(params));
	if (method == "memory.write")   return jsonrpc::makeResponse(id, handleMemoryWrite(params));
	if (method == "disassemble")    return jsonrpc::makeResponse(id, handleDisassemble(params));
	if (method == "pipeline")       return jsonrpc::makeResponse(id, handlePipeline());
	if (method == "stats")          return jsonrpc::makeResponse(id, handleStats());
	if (method == "status")         return jsonrpc::makeResponse(id, handleStatus());
	if (method == "reset")          return jsonrpc::makeResponse(id, handleReset());
	if (method == "trace")          return jsonrpc::makeResponse(id, handleTrace(params));
	if (method == "breakpoint.add") return jsonrpc::makeResponse(id, handleBreakpointAdd(params));
	if (method == "breakpoint.remove") return jsonrpc::makeResponse(id, handleBreakpointRemove(params));
	if (method == "breakpoint.list") return jsonrpc::makeResponse(id, handleBreakpointList());
	if (method == "watchpoint.add") return jsonrpc::makeResponse(id, handleWatchpointAdd(params));
	if (method == "watchpoint.remove") return jsonrpc::makeResponse(id, handleWatchpointRemove(params));
	if (method == "watchpoint.list") return jsonrpc::makeResponse(id, handleWatchpointList());
	if (method == "interrupt")      return jsonrpc::makeResponse(id, handleInterrupt(params));
	if (method == "periph.list")    return jsonrpc::makeResponse(id, handlePeriphList());
	if (method == "gpio.read")      return jsonrpc::makeResponse(id, handleGPIORead(params));
	if (method == "gpio.write")     return jsonrpc::makeResponse(id, handleGPIOWrite(params));
	if (method == "uart.inject")    return jsonrpc::makeResponse(id, handleUartInject(params));
	if (method == "snapshot.save")  return jsonrpc::makeResponse(id, handleSnapshotSave(params));
	if (method == "snapshot.load")  return jsonrpc::makeResponse(id, handleSnapshotLoad(params));

	return jsonrpc::makeError(id, -32601, "Method not found: " + method);
}

// ============================================================
// RPC Handlers
// ============================================================

json ANEMWebServer::handleStep(const json& params)
{
	unsigned int count = params.value("count", 1);
	auto result = engine.step(count);
	return serializeStepResult(result);
}

json ANEMWebServer::handleContinue(const json& id)
{
	simRunning.store(true);
	pauseRequested.store(false);

	if (simThread.joinable())
		simThread.join();

	simThread = std::jthread([this](std::stop_token stoken) {
		simLoop(stoken);
	});

	return jsonrpc::makeResponse(id, json{{"status", "running"}});
}

json ANEMWebServer::handlePause()
{
	if (!simRunning.load())
		return json{{"error", "not running"}};

	pauseRequested.store(true);
	if (simThread.joinable())
		simThread.join();

	return json{{"pc", toHex(engine.getRegisters().pc)},
	            {"cycle", engine.getStatus().cycle}};
}

json ANEMWebServer::handleRegisters(const json& params)
{
	if (params.contains("register")) {
		uint8_t reg = params["register"].get<uint8_t>();
		if (reg >= 16) return json{{"error", "Register index must be 0-15"}};
		return json{{"register", reg}, {"value", toHex(engine.getRegister(reg))}};
	}

	auto regs = engine.getRegisters();
	json gpr = json::array();
	for (int i = 0; i < 16; i++)
		gpr.push_back(toHex(regs.gpr[i]));

	// Look up variable mappings at current PC
	auto vars = lookupVarsAtPC(regs.pc);
	json varAnnotations = json::object();
	for (const auto& v : vars) {
		// Map location string to register index or stack
		if (v.location.size() >= 2 && v.location[0] == 'R') {
			varAnnotations[v.location] = v.name;
		} else if (v.location.substr(0, 5) == "stack") {
			varAnnotations[v.location] = v.name;
		}
	}

	json result = {
		{"pc", toHex(regs.pc)},
		{"gpr", gpr},
		{"hi", toHex(regs.hi)},
		{"lo", toHex(regs.lo)},
		{"sp", toHex(regs.sp)},
		{"epc", toHex(regs.epc)},
		{"eca", toHex(regs.eca)},
		{"ien", regs.ien},
		{"zFlag", regs.zFlag}
	};
	if (!varAnnotations.empty())
		result["vars"] = varAnnotations;
	return result;
}

json ANEMWebServer::handleMemoryRead(const json& params)
{
	addr_t addr = fromHex(params.at("address"));
	addr_t count = params.value("count", 1);
	auto entries = engine.getMemory(addr, count);
	json data = json::array();
	for (auto& e : entries) {
		json j = {{"addr", toHex(e.address)}, {"value", toHex(e.value)}};
		if (e.heat > 0) {
			j["heat"] = e.heat;
			j["write"] = e.lastWrite;
		}
		data.push_back(j);
	}
	return json{{"data", data}};
}

json ANEMWebServer::handleMemoryWrite(const json& params)
{
	addr_t addr = fromHex(params.at("address"));
	data_t value = (data_t)fromHex(params.at("value"));
	engine.writeMemory(addr, value);
	return json{{"address", toHex(addr)}, {"value", toHex(value)}};
}

json ANEMWebServer::handleDisassemble(const json& params)
{
	addr_t addr = fromHex(params.at("address"));
	addr_t count = params.value("count", 16);
	auto entries = engine.disassemble(addr, count);
	json instrs = json::array();
	for (auto& e : entries) {
		json j = {
			{"addr", toHex(e.address)},
			{"asm", e.asmText},
			{"word", toHex(e.word)},
			{"current", e.current}
		};
		if (!e.symbol.empty())
			j["symbol"] = e.symbol;
		if (!e.srcFile.empty()) {
			j["srcFile"] = e.srcFile;
			j["srcLine"] = e.srcLine;
			const auto& srcText = getSourceLine(e.srcFile, e.srcLine);
			if (!srcText.empty())
				j["srcText"] = srcText;
		}
		instrs.push_back(j);
	}
	return json{{"instructions", instrs}};
}

json ANEMWebServer::handlePipeline()
{
	auto p = engine.getPipeline();

	auto serializeStage = [](const PipelineStage& s) -> json {
		json j = json{
			{"pc", toHex(s.pc)},
			{"asm", s.asmText},
			{"bubble", s.bubble}
		};
		if (s.destReg >= 0)
			j["destReg"] = s.destReg;
		return j;
	};

	json idStage = serializeStage(p.idStage);
	idStage["fwd_alu_alu"] = p.idStage.fwdAluAlu;
	idStage["fwd_mem_alu"] = p.idStage.fwdMemAlu;

	json wbStage = serializeStage(p.wbStage);
	wbStage["committed"] = p.hasCommitted && !p.wbStage.bubble;

	// History: last N committed instructions
	json histArr = json::array();
	for (const auto& h : engine.getHistory()) {
		json entry = json{{"pc", toHex(h.pc)}, {"asm", h.asmText}};
		if (h.destReg >= 0) entry["destReg"] = h.destReg;
		histArr.push_back(entry);
	}

	return json{
		{"cycle", p.cycle},
		{"stages", {
			{"IF", serializeStage(p.ifStage)},
			{"ID", idStage},
			{"EX", serializeStage(p.exStage)},
			{"MEM", serializeStage(p.memStage)},
			{"WB", wbStage}
		}},
		{"stalled", p.stalled},
		{"zFlag", p.zFlag},
		{"history", histArr}
	};
}

json ANEMWebServer::handleStats()
{
	auto s = engine.getStats();
	return json{
		{"cycles", s.cycles},
		{"instructions", s.instructions},
		{"stalls", s.stalls},
		{"bubbles", s.bubbles},
		{"fwd_alu_alu", s.fwdAluAlu},
		{"fwd_mem_alu", s.fwdMemAlu}
	};
}

json ANEMWebServer::handleStatus()
{
	auto s = engine.getStatus();
	return json{
		{"running", simRunning.load()},
		{"halted", s.halted},
		{"pc", toHex(s.pc)},
		{"cycle", s.cycle}
	};
}

json ANEMWebServer::handleReset()
{
	engine.reset();
	return json{{"pc", toHex(engine.getRegisters().pc)}};
}

json ANEMWebServer::handleBreakpointAdd(const json& params)
{
	addr_t addr = fromHex(params.at("address"));
	engine.addBreakpoint(addr);
	return json{{"address", toHex(addr)}};
}

json ANEMWebServer::handleBreakpointRemove(const json& params)
{
	addr_t addr = fromHex(params.at("address"));
	bool removed = engine.removeBreakpoint(addr);
	return json{{"removed", removed}};
}

json ANEMWebServer::handleBreakpointList()
{
	auto bps = engine.listBreakpoints();
	json arr = json::array();
	for (addr_t bp : bps) arr.push_back(toHex(bp));
	return json{{"breakpoints", arr}};
}

json ANEMWebServer::handleWatchpointAdd(const json& params)
{
	addr_t addr = fromHex(params.at("address"));
	engine.addWatchpoint(addr);
	return json{{"address", toHex(addr)}};
}

json ANEMWebServer::handleWatchpointRemove(const json& params)
{
	addr_t addr = fromHex(params.at("address"));
	bool removed = engine.removeWatchpoint(addr);
	return json{{"removed", removed}};
}

json ANEMWebServer::handleWatchpointList()
{
	auto wps = engine.listWatchpoints();
	json arr = json::array();
	for (addr_t wp : wps) arr.push_back(toHex(wp));
	return json{{"watchpoints", arr}};
}

json ANEMWebServer::handleInterrupt(const json& params)
{
	bool assert_int = params.value("assert", true);
	if (assert_int) engine.assertInterrupt();
	else engine.deassertInterrupt();
	return json{{"asserted", assert_int}};
}

json ANEMWebServer::handlePeriphList()
{
	json result;

	auto* gpio = engine.getGPIO();
	if (gpio) {
		json ports = json::array();
		for (int p = 0; p < 2; p++) {
			ports.push_back(json{
				{"port", p},
				{"data", toHex(gpio->getOutputLatch(p))},
				{"dir", toHex(gpio->getDirection(p))},
				{"ext", toHex(gpio->getExternalPins(p))}
			});
		}
		result["gpio"] = ports;
	}

	auto* timer = engine.getTimer();
	if (timer) {
		result["timer"] = json{
			{"count", toHex(timer->getCount())},
			{"ctrl", toHex(timer->getCtrl())},
			{"status", toHex(timer->getStatus())},
			{"compare", toHex(timer->getCompare())}
		};
	}

	auto* uart = engine.getUART();
	if (uart) {
		std::string txState;
		switch (uart->getTxState()) {
		case UartTxState::IDLE:  txState = "idle";  break;
		case UartTxState::START: txState = "start"; break;
		case UartTxState::DATA:  txState = "data";  break;
		case UartTxState::STOP:  txState = "stop";  break;
		}
		result["uart"] = json{
			{"ctrl", toHex(uart->getCtrl())},
			{"status", toHex(uart->getStatusReg())},
			{"baud", toHex(uart->getBaudDiv())},
			{"tx_state", txState}
		};
	}

	return result;
}

json ANEMWebServer::handleGPIORead(const json& params)
{
	auto* gpio = engine.getGPIO();
	if (!gpio) return json{{"error", "GPIO not available"}};
	int port = params.value("port", 0);
	if (port < 0 || port > 1) return json{{"error", "Port must be 0 or 1"}};
	return json{
		{"port", port},
		{"data", toHex(gpio->getOutputLatch(port))},
		{"dir", toHex(gpio->getDirection(port))},
		{"ext", toHex(gpio->getExternalPins(port))},
		{"readback", toHex(gpio->getReadback(port))}
	};
}

json ANEMWebServer::handleGPIOWrite(const json& params)
{
	auto* gpio = engine.getGPIO();
	if (!gpio) return json{{"error", "GPIO not available"}};
	int port = params.value("port", 0);
	if (port < 0 || port > 1) return json{{"error", "Port must be 0 or 1"}};
	data_t value = (data_t)fromHex(params.at("value"));
	gpio->setExternalPins(port, value);
	return json{{"port", port}, {"value", toHex(value)}};
}

json ANEMWebServer::handleUartInject(const json& params)
{
	auto* uart = engine.getUART();
	if (!uart) return json{{"error", "UART not available"}};
	std::string data = params.at("data").get<std::string>();
	for (char ch : data) uart->injectRxByte((uint8_t)ch);
	return json{{"injected", (int)data.size()}};
}

json ANEMWebServer::handleTrace(const json& params)
{
	bool enabled = params.at("enabled").get<bool>();
	engine.setTrace(enabled);
	return json{{"enabled", engine.getTrace()}};
}

json ANEMWebServer::handleSnapshotSave(const json& params)
{
	std::string path = params.at("path").get<std::string>();
	std::string program = params.value("program", std::string());
	engine.saveSnapshot(path, program);
	return json{{"path", path}};
}

json ANEMWebServer::handleSnapshotLoad(const json& params)
{
	std::string path = params.at("path").get<std::string>();
	engine.loadSnapshot(path);
	auto regs = engine.getRegisters();
	return json{{"path", path}, {"pc", toHex(regs.pc)}};
}

// ============================================================
// Simulation Worker Thread
// ============================================================

void ANEMWebServer::simLoop(std::stop_token stoken)
{
	constexpr unsigned int BATCH_SIZE = 1000;

	engine.setTraceCallback([this](unsigned long long cycle, addr_t pc,
	                                const std::string& asmText) {
		json note = jsonrpc::makeNotification("trace", {
			{"cycle", cycle}, {"pc", toHex(pc)}, {"asm", asmText}
		});
		std::lock_guard<std::mutex> lock(notifyMutex);
		notifyQueue.push(note.dump());
		if (lwsCtx) lws_cancel_service(lwsCtx);
	});

	while (!stoken.stop_requested() && !pauseRequested.load()) {
		auto result = engine.runBatch(BATCH_SIZE);

		if (result.reason != StopReason::None) {
			std::string reason;
			switch (result.reason) {
			case StopReason::Breakpoint: reason = "breakpoint"; break;
			case StopReason::Watchpoint: reason = "watchpoint"; break;
			case StopReason::Halted:     reason = "halted"; break;
			default:                     reason = "unknown"; break;
			}

			json note = jsonrpc::makeNotification("stopped", {
				{"reason", reason},
				{"pc", toHex(result.pc)},
				{"cycle", result.cycle}
			});

			if (result.watchAccess) {
				note["params"]["watch_access"] = {
					{"address", toHex(result.watchAccess->address)},
					{"value", toHex(result.watchAccess->value)},
					{"write", result.watchAccess->write}
				};
			}

			std::lock_guard<std::mutex> lock(notifyMutex);
			notifyQueue.push(note.dump());
			if (lwsCtx) lws_cancel_service(lwsCtx);
			break;
		}
	}

	if (pauseRequested.load()) {
		auto status = engine.getStatus();
		json note = jsonrpc::makeNotification("stopped", {
			{"reason", "paused"},
			{"pc", toHex(status.pc)},
			{"cycle", status.cycle}
		});
		std::lock_guard<std::mutex> lock(notifyMutex);
		notifyQueue.push(note.dump());
		if (lwsCtx) lws_cancel_service(lwsCtx);
	}

	engine.setTraceCallback(nullptr);
	simRunning.store(false);
}
