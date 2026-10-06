#pragma once

#include <string>
#include <string_view>

namespace AutoWalk::Log {

bool Initialize();
void Write(std::string_view message);
void WriteLineToConsole(std::string_view message);
std::string GetLogPath();

} // namespace AutoWalk::Log
