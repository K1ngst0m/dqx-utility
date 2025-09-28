#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <cstdint>
#include <atomic>

namespace ipc
{
    struct Incoming
    {
        std::string type;
        std::uint64_t seq = 0;
        std::string text;
        std::string lang;
    };

    class TextSourceClient
    {
    public:
        TextSourceClient();
        ~TextSourceClient();

        bool connectFromPortfile(const char* portfile_path);
        bool connectHostPort(const char* host, int port);
        void disconnect();
        bool isConnected() const;

        void sendAck(std::uint64_t seq);

        bool poll(std::vector<Incoming>& out);
        const char* lastError() const { return last_error_.c_str(); }

    private:
        void recvLoop();
        void closeSocket();
        bool sendLine(const std::string& line);
        bool readPortFromFile(const char* path, int& port);
        bool parseJsonLine(const std::string& line, Incoming& out);

        std::string last_error_;
        std::thread recv_thread_;
        std::atomic<bool> running_{false};

        std::mutex in_mutex_;
        std::vector<Incoming> inbox_;

        int sock_ = -1;
    };
}