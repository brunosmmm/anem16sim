/*
 * @file snapshot.h
 * @brief CPU state snapshot save/load (JSON)
 */

#ifndef SNAPSHOT_H_
#define SNAPSHOT_H_

#include <nlohmann/json.hpp>
#include <string>

class ANEMCPU;

namespace snapshot {

nlohmann::json save(const ANEMCPU& cpu, const std::string& programFile = "");
void restore(ANEMCPU& cpu, const nlohmann::json& j);
void saveToFile(const ANEMCPU& cpu, const std::string& path, const std::string& programFile = "");
void loadFromFile(ANEMCPU& cpu, const std::string& path);

} // namespace snapshot

#endif /* SNAPSHOT_H_ */
