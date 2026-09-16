// NIST-developed software is provided by NIST as a public service. You may use, copy and distribute copies of the
// software in any medium, provided that you keep intact this entire notice. You may improve, modify and create
// derivative works of the software or any portion of the software, and you may copy and distribute such modifications
// or works. Modified works should carry a notice stating that you changed the software and should note the date and
// nature of any such change. Please explicitly acknowledge the National Institute of Standards and Technology as the
// source of the software. NIST-developed software is expressly provided "AS IS." NIST MAKES NO WARRANTY OF ANY KIND,
// EXPRESS, IMPLIED, IN FACT OR ARISING BY OPERATION OF LAW, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTY OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, NON-INFRINGEMENT AND DATA ACCURACY. NIST NEITHER REPRESENTS NOR
// WARRANTS THAT THE OPERATION OF THE SOFTWARE WILL BE UNINTERRUPTED OR ERROR-FREE, OR THAT ANY DEFECTS WILL BE
// CORRECTED. NIST DOES NOT WARRANT OR MAKE ANY REPRESENTATIONS REGARDING THE USE OF THE SOFTWARE OR THE RESULTS
// THEREOF, INCLUDING BUT NOT LIMITED TO THE CORRECTNESS, ACCURACY, RELIABILITY, OR USEFULNESS OF THE SOFTWARE. You
// are solely responsible for determining the appropriateness of using and distributing the software and you assume
// all risks associated with its use, including but not limited to the risks and costs of program errors, compliance
// with applicable laws, damage to or loss of data, programs or equipment, and the unavailability or interruption of
// operation. This software is not intended to be used in any situation where a failure could cause risk of injury or
// damage to property. The software developed by NIST employees is not subject to copyright protection within the
// United States.

#ifndef HEDGEHOG_TOOL_LOG_H
#define HEDGEHOG_TOOL_LOG_H

#include <mutex>
#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <source_location>

#define HH_LOC std::source_location loc = std::source_location::current()

namespace hh::log {

std::mutex LOG_MUTEX;
size_t ERROR_COUNT = 0;

// log level ///////////////////////////////////////////////////////////////////

enum class LogLevel {
    Info    = 0,
    Warning = 1,
    Error   = 2,
};
LogLevel LOG_LEVEL = LogLevel::Warning;

void set_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(LOG_MUTEX);
    LOG_LEVEL = level;
}

// messages  ///////////////////////////////////////////////////////////////////

//
// Trick to capture message and the source location.
//
struct LocatedMessage {
    std::source_location loc;
    std::string_view message;

    LocatedMessage(const char *message, std::source_location loc = std::source_location::current())
        : message(message) {}

    LocatedMessage(std::string const &message, std::source_location loc = std::source_location::current())
        : message(message) {}
};

// info ////////////////////////////////////////////////////////////////////////

void info(std::source_location loc, auto &&...args) {
    std::lock_guard<std::mutex> lock(LOG_MUTEX);
    if (LOG_LEVEL > LogLevel::Info) return;
    std::cout << "HH  INFO  " << loc.file_name() << "(" << loc.line() << ":" << loc.column() << "): ";
    (std::cout << ... << args);
    std::cout << std::endl;
}

void info(LocatedMessage lm, auto &&...args) {
    info(lm.loc, lm.message, std::forward<decltype(args)>(args)...);
}

// warning /////////////////////////////////////////////////////////////////////

void warning(std::source_location loc, auto &&...args) {
    std::lock_guard<std::mutex> lock(LOG_MUTEX);
    if (LOG_LEVEL > LogLevel::Warning) return;
    std::cerr << "HH  WARN  " << loc.file_name() << "(" << loc.line() << ":" << loc.column() << "): ";
    (std::cerr << ... << args);
    std::cerr << std::endl;
}

void warning(LocatedMessage lm, auto &&...args) {
    warning(lm.loc, lm.message, std::forward<decltype(args)>(args)...);
}

// error ///////////////////////////////////////////////////////////////////////

void error(std::source_location loc, auto &&...args) {
    std::lock_guard<std::mutex> lock(LOG_MUTEX);
    if (LOG_LEVEL > LogLevel::Error) return;
    std::cerr << "HH  ERROR  " << loc.file_name() << "(" << loc.line() << ":" << loc.column() << "): ";
    (std::cerr << ... << args);
    std::cerr << std::endl;
    ERROR_COUNT++;
}

void error(LocatedMessage lm, auto &&...args) {
    error(lm.loc, lm.message, std::forward<decltype(args)>(args)...);
}

size_t error_count() {
    std::lock_guard<std::mutex> lock(LOG_MUTEX);
    return ERROR_COUNT;
}

// fatal ///////////////////////////////////////////////////////////////////////

[[noreturn]] void fatal(std::source_location loc, auto &&...args) {
    {
        std::lock_guard<std::mutex> lock(LOG_MUTEX);
        std::cerr << "HH  FATAL  " << loc.file_name() << "(" << loc.line() << ":" << loc.column() << "): ";
        (std::cerr << ... << args);
        std::cerr << std::endl;
    }
    std::abort();
}

[[noreturn]] void fatal(LocatedMessage lm, auto &&...args) {
    fatal(lm.loc, lm.message, std::forward<decltype(args)>(args)...);
}

} // end namespace hh::log

#endif
