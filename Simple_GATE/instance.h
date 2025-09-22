#ifndef INSTANCE_H
#define INSTANCE_H
#include <iostream>

class Instance
{
public:
    Instance();
    bool start();
    bool terminate();
    bool suspend();
    bool resume();
    bool readMemory(__UINTPTR_TYPE__ adress, void* buffer, __SIZE_TYPE__ size);
    bool writeMemory (__UINTPTR_TYPE__ adress, const void* data, __SIZE_TYPE__ size);
    void handleMessages();
    pid_t getPid();
    std::array<uint8_t, 16> getUNON();
    ProcessStatus getStatus();
    std::string getExecutablePath();
    std::chrono::seconds getUptime();
    void setPriority(ProcessPriority priority);
private:
    enum class ProcessStatus{
        NotStarted,
        Running,
        Suspended,
        Terminated,
        Error
    };
    enum class ProcessPriority {
        Low,
        Medium,
        High
    };
    pid_t m_pid;
    std::array<uint8_t, 16> m_UNON;
    ProcessStatus m_status;
    std::string m_executablePath;
    std::vector<std::string> m_args;
    int m_clientSocket;
    std::thread m_communicationThread;
    std::mutex m_memoryMutex;
    std::chrono::system_clock::time_point m_startTime;
    ProcessPriority m_priority;
};

#endif // INSTANCE_H
