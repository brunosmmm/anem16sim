/*
 * @file debug_server.cpp
 * @brief ANEM16 remote debug server — JSON-RPC 2.0 over ZeroMQ
 */

#include "debug_server.h"
#include "periph/gpio.h"
#include "periph/timer.h"
#include "periph/uart.h"
#include <iostream>
#include <iomanip>
#include <sstream>

using json = jsonrpc::json;

// ---- Helpers ----

std::string ANEMDebugServer::toHex(uint32_t val, int width)
{
	std::ostringstream ss;
	ss << "0x" << std::hex << std::setfill('0') << std::setw(width) << val;
	return ss.str();
}

uint32_t ANEMDebugServer::fromHex(const json& val)
{
	if (val.is_string())
	{
		std::string s = val.get<std::string>();
		return (uint32_t)std::stoul(s, nullptr, 0);
	}
	return val.get<uint32_t>();
}

json ANEMDebugServer::serializeStepResult(const StepResult& r)
{
	json result;
	result["pc"] = toHex(r.pc);
	result["cycle"] = r.cycle;
	result["halted"] = r.halted;

	switch (r.reason)
	{
	case StopReason::None:       result["stopped_reason"] = "none"; break;
	case StopReason::Breakpoint: result["stopped_reason"] = "breakpoint"; break;
	case StopReason::Watchpoint: result["stopped_reason"] = "watchpoint"; break;
	case StopReason::Halted:     result["stopped_reason"] = "halted"; break;
	}

	if (r.watchAccess)
	{
		result["watch_access"] = {
			{"address", toHex(r.watchAccess->address)},
			{"value", toHex(r.watchAccess->value)},
			{"write", r.watchAccess->write}
		};
	}

	return result;
}

// ---- Constructor ----

ANEMDebugServer::ANEMDebugServer(ANEMCPU& cpu, int port)
	: engine(cpu)
	, zmqCtx(1)
	, repSocket(zmqCtx, zmq::socket_type::rep)
	, pubSocket(zmqCtx, zmq::socket_type::pub)
	, port(port)
{
}

// ---- Notification drain ----

void ANEMDebugServer::drainNotifications()
{
	std::lock_guard<std::mutex> lock(notifyMutex);
	while (!notifyQueue.empty())
	{
		json note = std::move(notifyQueue.front());
		notifyQueue.pop();
		std::string msg = note.dump();
		pubSocket.send(zmq::buffer(msg), zmq::send_flags::none);
	}
}

// ---- Dispatch ----

json ANEMDebugServer::dispatch(const std::string& method, const json& params,
                                const json& id)
{
	// While sim is running, only allow pause, status, and breakpoint/watchpoint management
	if (simRunning.load())
	{
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

	if (method == "step")
		return jsonrpc::makeResponse(id, handleStep(params));
	if (method == "continue")
		return handleContinue(id);
	if (method == "pause")
		return jsonrpc::makeError(id, -3, "Simulator is not running");
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
	if (method == "registers")
		return jsonrpc::makeResponse(id, handleRegisters(params));
	if (method == "memory.read")
		return jsonrpc::makeResponse(id, handleMemoryRead(params));
	if (method == "memory.write")
		return jsonrpc::makeResponse(id, handleMemoryWrite(params));
	if (method == "disassemble")
		return jsonrpc::makeResponse(id, handleDisassemble(params));
	if (method == "pipeline")
		return jsonrpc::makeResponse(id, handlePipeline());
	if (method == "trace")
		return jsonrpc::makeResponse(id, handleTrace(params));
	if (method == "stats")
		return jsonrpc::makeResponse(id, handleStats());
	if (method == "reset")
		return jsonrpc::makeResponse(id, handleReset());
	if (method == "status")
		return jsonrpc::makeResponse(id, handleStatus());
	if (method == "snapshot.save")
		return jsonrpc::makeResponse(id, handleSnapshotSave(params));
	if (method == "snapshot.load")
		return jsonrpc::makeResponse(id, handleSnapshotLoad(params));
	if (method == "interrupt")
		return jsonrpc::makeResponse(id, handleInterrupt(params));
	if (method == "gpio.read")
		return jsonrpc::makeResponse(id, handleGPIORead(params));
	if (method == "gpio.write")
		return jsonrpc::makeResponse(id, handleGPIOWrite(params));
	if (method == "uart.inject")
		return jsonrpc::makeResponse(id, handleUartInject(params));
	if (method == "periph.list")
		return jsonrpc::makeResponse(id, handlePeriphList());

	return jsonrpc::makeError(id, -32601, "Method not found: " + method);
}

// ---- RPC Handlers ----

json ANEMDebugServer::handleStep(const json& params)
{
	unsigned int count = params.value("count", 1);
	auto result = engine.step(count);
	return serializeStepResult(result);
}

json ANEMDebugServer::handleContinue(const json& id)
{
	simRunning.store(true);
	pauseRequested.store(false);

	// Join any previous thread
	if (simThread.joinable())
		simThread.join();

	simThread = std::jthread([this](std::stop_token stoken) {
		simLoop(stoken);
	});

	return jsonrpc::makeResponse(id, json{{"status", "running"}});
}

json ANEMDebugServer::handlePause()
{
	if (!simRunning.load())
		return json{{"error", "not running"}};

	pauseRequested.store(true);

	// Wait for sim thread to stop
	if (simThread.joinable())
		simThread.join();

	return json{{"pc", toHex(engine.getRegisters().pc)},
	            {"cycle", engine.getStatus().cycle}};
}

json ANEMDebugServer::handleBreakpointAdd(const json& params)
{
	addr_t addr = fromHex(params.at("address"));
	engine.addBreakpoint(addr);
	return json{{"address", toHex(addr)}};
}

json ANEMDebugServer::handleBreakpointRemove(const json& params)
{
	addr_t addr = fromHex(params.at("address"));
	bool removed = engine.removeBreakpoint(addr);
	return json{{"removed", removed}};
}

json ANEMDebugServer::handleBreakpointList()
{
	auto bps = engine.listBreakpoints();
	json arr = json::array();
	for (addr_t bp : bps)
		arr.push_back(toHex(bp));
	return json{{"breakpoints", arr}};
}

json ANEMDebugServer::handleWatchpointAdd(const json& params)
{
	addr_t addr = fromHex(params.at("address"));
	engine.addWatchpoint(addr);
	return json{{"address", toHex(addr)}};
}

json ANEMDebugServer::handleWatchpointRemove(const json& params)
{
	addr_t addr = fromHex(params.at("address"));
	bool removed = engine.removeWatchpoint(addr);
	return json{{"removed", removed}};
}

json ANEMDebugServer::handleWatchpointList()
{
	auto wps = engine.listWatchpoints();
	json arr = json::array();
	for (addr_t wp : wps)
		arr.push_back(toHex(wp));
	return json{{"watchpoints", arr}};
}

json ANEMDebugServer::handleRegisters(const json& params)
{
	if (params.contains("register"))
	{
		uint8_t reg = params["register"].get<uint8_t>();
		if (reg >= 16)
			return json{{"error", "Register index must be 0-15"}};
		return json{{"register", reg}, {"value", toHex(engine.getRegister(reg))}};
	}

	auto regs = engine.getRegisters();
	json gpr = json::array();
	for (int i = 0; i < 16; i++)
		gpr.push_back(toHex(regs.gpr[i]));

	return json{
		{"pc", toHex(regs.pc)},
		{"gpr", gpr},
		{"hi", toHex(regs.hi)},
		{"lo", toHex(regs.lo)},
		{"sp", toHex(regs.sp)},
		{"epc", toHex(regs.epc)},
		{"eca", toHex(regs.eca)},
		{"ien", regs.ien}
	};
}

json ANEMDebugServer::handleMemoryRead(const json& params)
{
	addr_t addr = fromHex(params.at("address"));
	addr_t count = params.value("count", 1);

	auto entries = engine.getMemory(addr, count);
	json data = json::array();
	for (auto& e : entries)
		data.push_back(json{{"addr", toHex(e.address)}, {"value", toHex(e.value)}});

	return json{{"data", data}};
}

json ANEMDebugServer::handleMemoryWrite(const json& params)
{
	addr_t addr = fromHex(params.at("address"));
	data_t value = (data_t)fromHex(params.at("value"));
	engine.writeMemory(addr, value);
	return json{{"address", toHex(addr)}, {"value", toHex(value)}};
}

json ANEMDebugServer::handleDisassemble(const json& params)
{
	addr_t addr = fromHex(params.at("address"));
	addr_t count = params.value("count", 16);

	auto entries = engine.disassemble(addr, count);
	json instrs = json::array();
	for (auto& e : entries)
		instrs.push_back(json{
			{"addr", toHex(e.address)},
			{"asm", e.asmText},
			{"word", toHex(e.word)},
			{"current", e.current}
		});

	return json{{"instructions", instrs}};
}

json ANEMDebugServer::handlePipeline()
{
	auto p = engine.getPipeline();

	auto serializeStage = [](const PipelineStage& s) -> json {
		return json{
			{"pc", toHex(s.pc)},
			{"asm", s.asmText},
			{"bubble", s.bubble}
		};
	};

	json idStage = serializeStage(p.idStage);
	idStage["fwd_alu_alu"] = p.idStage.fwdAluAlu;
	idStage["fwd_mem_alu"] = p.idStage.fwdMemAlu;

	return json{
		{"cycle", p.cycle},
		{"stages", {
			{"IF", serializeStage(p.ifStage)},
			{"ID", idStage},
			{"EX", serializeStage(p.exStage)},
			{"MEM", serializeStage(p.memStage)}
		}},
		{"stalled", p.stalled}
	};
}

json ANEMDebugServer::handleTrace(const json& params)
{
	bool enabled = params.at("enabled").get<bool>();
	engine.setTrace(enabled);
	return json{{"enabled", engine.getTrace()}};
}

json ANEMDebugServer::handleStats()
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

json ANEMDebugServer::handleReset()
{
	engine.reset();
	return json{{"pc", toHex(engine.getRegisters().pc)}};
}

json ANEMDebugServer::handleStatus()
{
	auto s = engine.getStatus();
	return json{
		{"running", simRunning.load()},
		{"halted", s.halted},
		{"pc", toHex(s.pc)},
		{"cycle", s.cycle}
	};
}

json ANEMDebugServer::handleSnapshotSave(const json& params)
{
	std::string path = params.at("path").get<std::string>();
	std::string program = params.value("program", std::string());
	engine.saveSnapshot(path, program);
	return json{{"path", path}};
}

json ANEMDebugServer::handleInterrupt(const json& params)
{
	bool assert_int = params.value("assert", true);
	if (assert_int)
		engine.assertInterrupt();
	else
		engine.deassertInterrupt();
	return json{{"asserted", assert_int}};
}

json ANEMDebugServer::handleSnapshotLoad(const json& params)
{
	std::string path = params.at("path").get<std::string>();
	engine.loadSnapshot(path);
	auto regs = engine.getRegisters();
	return json{{"path", path}, {"pc", toHex(regs.pc)}};
}

json ANEMDebugServer::handleGPIORead(const json& params)
{
	auto* gpio = engine.getGPIO();
	if (!gpio)
		return json{{"error", "GPIO not available"}};

	int port = params.value("port", 0);
	if (port < 0 || port > 1)
		return json{{"error", "Port must be 0 or 1"}};

	return json{
		{"port", port},
		{"data", toHex(gpio->getOutputLatch(port))},
		{"dir", toHex(gpio->getDirection(port))},
		{"ext", toHex(gpio->getExternalPins(port))},
		{"readback", toHex(gpio->getReadback(port))}
	};
}

json ANEMDebugServer::handleGPIOWrite(const json& params)
{
	auto* gpio = engine.getGPIO();
	if (!gpio)
		return json{{"error", "GPIO not available"}};

	int port = params.value("port", 0);
	if (port < 0 || port > 1)
		return json{{"error", "Port must be 0 or 1"}};

	data_t value = (data_t)fromHex(params.at("value"));
	gpio->setExternalPins(port, value);
	return json{{"port", port}, {"value", toHex(value)}};
}

json ANEMDebugServer::handleUartInject(const json& params)
{
	auto* uart = engine.getUART();
	if (!uart)
		return json{{"error", "UART not available"}};

	std::string data = params.at("data").get<std::string>();
	for (char ch : data)
		uart->injectRxByte((uint8_t)ch);

	return json{{"injected", (int)data.size()}};
}

json ANEMDebugServer::handlePeriphList()
{
	json result;

	auto* gpio = engine.getGPIO();
	if (gpio)
	{
		json ports = json::array();
		for (int p = 0; p < 2; p++)
		{
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
	if (timer)
	{
		result["timer"] = json{
			{"count", toHex(timer->getCount())},
			{"ctrl", toHex(timer->getCtrl())},
			{"status", toHex(timer->getStatus())},
			{"compare", toHex(timer->getCompare())}
		};
	}

	auto* uart = engine.getUART();
	if (uart)
	{
		std::string txState;
		switch (uart->getTxState())
		{
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

// ---- Simulation worker thread ----

void ANEMDebugServer::simLoop(std::stop_token stoken)
{
	constexpr unsigned int BATCH_SIZE = 1000;

	// Set up trace callback to publish trace notifications
	engine.setTraceCallback([this](unsigned long long cycle, addr_t pc,
	                                const std::string& asmText) {
		json note = jsonrpc::makeNotification("trace", {
			{"cycle", cycle},
			{"pc", toHex(pc)},
			{"asm", asmText}
		});
		std::lock_guard<std::mutex> lock(notifyMutex);
		notifyQueue.push(std::move(note));
	});

	while (!stoken.stop_requested() && !pauseRequested.load())
	{
		auto result = engine.runBatch(BATCH_SIZE);

		if (result.reason != StopReason::None)
		{
			std::string reason;
			switch (result.reason)
			{
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

			if (result.watchAccess)
			{
				note["params"]["watch_access"] = {
					{"address", toHex(result.watchAccess->address)},
					{"value", toHex(result.watchAccess->value)},
					{"write", result.watchAccess->write}
				};
			}

			{
				std::lock_guard<std::mutex> lock(notifyMutex);
				notifyQueue.push(std::move(note));
			}
			break;
		}
	}

	// If paused (not breakpoint/watchpoint/halt), emit paused notification
	if (pauseRequested.load())
	{
		auto status = engine.getStatus();
		json note = jsonrpc::makeNotification("stopped", {
			{"reason", "paused"},
			{"pc", toHex(status.pc)},
			{"cycle", status.cycle}
		});
		std::lock_guard<std::mutex> lock(notifyMutex);
		notifyQueue.push(std::move(note));
	}

	// Clear trace callback (avoid dangling captures when not running)
	engine.setTraceCallback(nullptr);
	simRunning.store(false);
}

// ---- Main event loop ----

void ANEMDebugServer::run()
{
	std::string repAddr = "tcp://*:" + std::to_string(port);
	std::string pubAddr = "tcp://*:" + std::to_string(port + 1);

	repSocket.bind(repAddr);
	pubSocket.bind(pubAddr);

	std::cout << "ANEM16 Debug Server listening:" << std::endl;
	std::cout << "  REP (requests):      tcp://localhost:" << port << std::endl;
	std::cout << "  PUB (notifications): tcp://localhost:" << (port + 1) << std::endl;

	while (true)
	{
		// Drain any pending notifications from the sim thread
		drainNotifications();

		// Poll REP socket with timeout so we can drain notifications periodically
		zmq::pollitem_t items[] = {{repSocket.handle(), 0, ZMQ_POLLIN, 0}};
		zmq::poll(items, 1, std::chrono::milliseconds(50));

		if (!(items[0].revents & ZMQ_POLLIN))
			continue;

		zmq::message_t request;
		auto recvResult = repSocket.recv(request, zmq::recv_flags::none);
		if (!recvResult)
			continue;

		std::string raw(static_cast<char*>(request.data()), request.size());
		auto req = jsonrpc::parseRequest(raw);

		json response;
		if (!req.valid)
		{
			if (!req.parseError.empty() && req.parseError.find("JSON") != std::string::npos)
				response = jsonrpc::makeError(nullptr, -32700, "Parse error: " + req.parseError);
			else
				response = jsonrpc::makeError(nullptr, -32600, "Invalid request: " + req.parseError);
		}
		else
		{
			try
			{
				response = dispatch(req.method, req.params, req.id);
			}
			catch (const json::exception& e)
			{
				response = jsonrpc::makeError(req.id, -32602, std::string("Invalid params: ") + e.what());
			}
			catch (const std::exception& e)
			{
				response = jsonrpc::makeError(req.id, -1, std::string("Simulator error: ") + e.what());
			}
		}

		std::string reply = response.dump();
		repSocket.send(zmq::buffer(reply), zmq::send_flags::none);
	}
}
