#pragma once

#include <iostream>
#include <fstream>
#include <sstream>
#include <mutex>
#include <chrono>
#include <iomanip>

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    FATAL = 4
};

class Logger {
private:
    static LogLevel minLevel;
    static std::ofstream logFile;
    static std::mutex logMutex;
    static bool consoleOutput;
    
    static const char* levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO ";
            case LogLevel::WARN:  return "WARN ";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::FATAL: return "FATAL";
            default: return "UNKNOWN";
        }
    }
    
    template<typename T>
    static void writeToStream(std::ostream& stream, T&& arg) {
        stream << std::forward<T>(arg);
    }
    
    template<typename T, typename... Args>
    static void writeToStream(std::ostream& stream, T&& arg, Args&&... args) {
        stream << std::forward<T>(arg);
        writeToStream(stream, std::forward<Args>(args)...);
    }
    
public:
    static void init(const std::string& filename = "", LogLevel level = LogLevel::INFO, bool console = true) {
        minLevel = level;
        consoleOutput = console;
        
        if (!filename.empty()) {
            logFile.open(filename, std::ios::app);
        }
    }
    
    static void shutdown() {
        if (logFile.is_open()) {
            logFile.close();
        }
    }
    
    template<typename... Args>
    static void log(LogLevel level, Args&&... args) {
        if (level < minLevel) return;
        
        std::lock_guard<std::mutex> lock(logMutex);
        
        // Build message
        std::ostringstream message;
        
        // Timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ) % 1000;
        
        message << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        message << "." << std::setfill('0') << std::setw(3) << ms.count();
        
        // Level
        message << " [" << levelToString(level) << "] ";
        
        // Message content
        writeToStream(message, std::forward<Args>(args)...);
        
        // Output to console
        if (consoleOutput) {
            if (level >= LogLevel::ERROR) {
                std::cerr << message.str() << std::endl;
            } else {
                std::cout << message.str() << std::endl;
            }
        }
        
        // Output to file
        if (logFile.is_open()) {
            logFile << message.str() << std::endl;
            logFile.flush();
        }
    }
    
    // Convenience methods
    template<typename... Args>
    static void debug(Args&&... args) {
        log(LogLevel::DEBUG, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void info(Args&&... args) {
        log(LogLevel::INFO, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void warn(Args&&... args) {
        log(LogLevel::WARN, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void error(Args&&... args) {
        log(LogLevel::ERROR, std::forward<Args>(args)...);
    }
    
    template<typename... Args>
    static void fatal(Args&&... args) {
        log(LogLevel::FATAL, std::forward<Args>(args)...);
    }
};

// Static member initialization (in cpp file)
inline LogLevel Logger::minLevel = LogLevel::INFO;
inline std::ofstream Logger::logFile;
inline std::mutex Logger::logMutex;
inline bool Logger::consoleOutput = true;
