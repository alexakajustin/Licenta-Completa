#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iomanip>

/**
 * @class PerformanceProfiler
 * @brief Simple, powerful, modular, and uncoupled CPU performance profiling utility.
 */
class PerformanceProfiler
{
public:
    struct Record
    {
        std::string testName;
        int elementCount;
        double elapsedMilliseconds;
    };

    static PerformanceProfiler& Get()
    {
        static PerformanceProfiler instance;
        return instance;
    }

    /**
     * @brief Profiles the execution of a given callback function.
     * @tparam Func Callback callable.
     * @param testName Descriptive name of the test/operation.
     * @param elementCount Size or count of elements processed (for charting trends).
     * @param func Callable block of code to measure.
     * @return Execution time in milliseconds.
     */
    template<typename Func>
    double Profile(const std::string& testName, int elementCount, Func&& func)
    {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Execute the function
        func();

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> duration = end - start;
        double elapsedMS = duration.count();

        records.push_back({ testName, elementCount, elapsedMS });

        std::cout << "[PROFILER] " << std::left << std::setw(40) << testName 
                  << " | Count: " << std::right << std::setw(8) << elementCount 
                  << " | Time: " << std::fixed << std::setprecision(3) << elapsedMS << " ms\n";

        return elapsedMS;
    }

    /**
     * @brief Writes all accumulated profiling metrics to a CSV file.
     * @param filepath Target output file path.
     * @return True if successful.
     */
    bool WriteCSV(const std::string& filepath)
    {
        std::ofstream file(filepath);
        if (!file.is_open())
        {
            std::cerr << "[PROFILER] Error: Failed to open file for writing CSV: " << filepath << "\n";
            return false;
        }

        file << "TestName,ElementCount,ElapsedMS\n";
        for (const auto& r : records)
        {
            file << r.testName << "," << r.elementCount << "," << std::fixed << std::setprecision(6) << r.elapsedMilliseconds << "\n";
        }
        file.close();
        std::cout << "[PROFILER] Successfully wrote performance data to: " << filepath << "\n";
        return true;
    }

    /**
     * @brief Clears all accumulated profiling records.
     */
    void Clear()
    {
        records.clear();
    }

    const std::vector<Record>& GetRecords() const
    {
        return records;
    }

private:
    PerformanceProfiler() = default;
    std::vector<Record> records;
};
