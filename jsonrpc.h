/*
 * @file jsonrpc.h
 * @brief JSON-RPC 2.0 request/response utilities
 */

#ifndef JSONRPC_H_
#define JSONRPC_H_

#include <nlohmann/json.hpp>
#include <string>

namespace jsonrpc {

using json = nlohmann::json;

struct Request {
	json id;        // null for notifications
	std::string method;
	json params;
	bool valid;
	std::string parseError;
};

inline json makeResponse(const json& id, const json& result)
{
	return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

inline json makeError(const json& id, int code, const std::string& message)
{
	return {{"jsonrpc", "2.0"}, {"id", id},
	        {"error", {{"code", code}, {"message", message}}}};
}

inline json makeNotification(const std::string& method, const json& params)
{
	return {{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
}

inline Request parseRequest(const std::string& raw)
{
	Request req;
	req.valid = false;

	json j;
	try {
		j = json::parse(raw);
	} catch (const json::parse_error& e) {
		req.parseError = e.what();
		return req;
	}

	if (!j.is_object())
	{
		req.parseError = "Request must be a JSON object";
		return req;
	}

	if (!j.contains("method") || !j["method"].is_string())
	{
		req.parseError = "Missing or invalid 'method' field";
		return req;
	}

	req.method = j["method"].get<std::string>();
	req.id = j.value("id", json(nullptr));
	req.params = j.value("params", json::object());
	req.valid = true;
	return req;
}

} // namespace jsonrpc

#endif /* JSONRPC_H_ */
