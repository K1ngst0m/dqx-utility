#include "TextSourceClient.hpp"

#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>
#include <atomic>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#endif

using namespace ipc;

static bool parse_int_field(const std::string& s, const char* key, std::uint64_t& val)
{
    std::string k = std::string("\"") + key + "\"";
    auto p = s.find(k);
    if (p == std::string::npos) return false;
    p = s.find(':', p);
    if (p == std::string::npos) return false;
    ++p;
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
    std::uint64_t v = 0;
    bool any = false;
    while (p < s.size() && s[p] >= '0' && s[p] <= '9') { v = v * 10 + (s[p]-'0'); ++p; any = true; }
    if (!any) return false;
    val = v;
    return true;
}

static bool parse_string_field(const std::string& s, const char* key, std::string& out)
{
    std::string k = std::string("\"") + key + "\"";
    auto p = s.find(k);
    if (p == std::string::npos) return false;
    p = s.find(':', p);
    if (p == std::string::npos) return false;
    ++p;
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
    if (p >= s.size() || s[p] != '"') return false;
    ++p;
    std::string v;
    while (p < s.size())
    {
        char c = s[p++];
        if (c == '\\')
        {
            if (p >= s.size()) break;
            char e = s[p++];
            if (e == 'n') v.push_back('\n');
            else if (e == 't') v.push_back('\t');
            else v.push_back(e);
        }
        else if (c == '"')
        {
            break;
        }
        else
        {
            v.push_back(c);
        }
    }
    out.swap(v);
    return true;
}

TextSourceClient::TextSourceClient() {}
TextSourceClient::~TextSourceClient() { disconnect(); }

bool TextSourceClient::readPortFromFile(const char* path, int& port)
{
    port = 0;
    FILE* f = std::fopen(path, "rb");
    if (!f) { last_error_ = "open portfile failed"; return false; }
    std::string content;
    char buf[512];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) content.append(buf, buf + n);
    std::fclose(f);
    std::uint64_t p = 0;
    if (!parse_int_field(content, "port", p)) { last_error_ = "port not found"; return false; }
    if (p == 0 || p > 65535) { last_error_ = "invalid port"; return false; }
    port = static_cast<int>(p);
    return true;
}

bool TextSourceClient::connectFromPortfile(const char* path)
{
    int port = 0;
    if (!readPortFromFile(path, port)) return false;
    return connectHostPort("127.0.0.1", port);
}

bool TextSourceClient::connectHostPort(const char* host, int port)
{
    disconnect();
#if defined(_WIN32)
    WSADATA wsaData; WSAStartup(MAKEWORD(2,2), &wsaData);
#endif
    struct addrinfo hints; std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char service[32]; std::snprintf(service, sizeof(service), "%d", port);
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host, service, &hints, &res) != 0) { last_error_ = "getaddrinfo failed"; return false; }

    int s = -1;
    for (auto p = res; p != nullptr; p = p->ai_next)
    {
        s = (int)socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s < 0) continue;
        if (::connect(s, p->ai_addr, (socklen_t)p->ai_addrlen) == 0) { sock_ = s; break; }
#if defined(_WIN32)
        closesocket(s);
#else
        close(s);
#endif
        s = -1;
    }
    freeaddrinfo(res);
    if (sock_ < 0) { last_error_ = "connect failed"; return false; }

    running_.store(true);
    recv_thread_ = std::thread(&TextSourceClient::recvLoop, this);

    std::string hello = std::string("{\"type\":\"hello\",\"protocol\":\"dqx_text_v1\",\"start_seq\":0}\n");
    sendLine(hello);
    return true;
}

void TextSourceClient::disconnect()
{
    running_.store(false);
    if (sock_ >= 0)
    {
#if defined(_WIN32)
        shutdown(sock_, SD_BOTH);
#else
        shutdown(sock_, SHUT_RDWR);
#endif
    }
    if (recv_thread_.joinable()) recv_thread_.join();
    closeSocket();
#if defined(_WIN32)
    WSACleanup();
#endif
}

bool TextSourceClient::isConnected() const { return sock_ >= 0 && running_.load(); }

bool TextSourceClient::sendLine(const std::string& line)
{
    if (sock_ < 0) return false;
    const char* data = line.c_str();
    size_t left = line.size();
    while (left > 0)
    {
#if defined(_WIN32)
        int n = ::send(sock_, data, (int)left, 0);
#else
        ssize_t n = ::send(sock_, data, left, 0);
#endif
        if (n <= 0) return false;
        data += n; left -= (size_t)n;
    }
    return true;
}

void TextSourceClient::sendAck(std::uint64_t seq)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "{\"type\":\"ack\",\"seq\":%llu}\n", (unsigned long long)seq);
    sendLine(std::string(buf));
}

bool TextSourceClient::poll(std::vector<Incoming>& out)
{
    std::lock_guard<std::mutex> lock(in_mutex_);
    if (inbox_.empty()) return false;
    out.swap(inbox_);
    return true;
}

bool TextSourceClient::parseJsonLine(const std::string& line, Incoming& out)
{
    std::string t;
    if (!parse_string_field(line, "type", t)) return false;
    out.type = t;
    std::uint64_t seq = 0;
    parse_int_field(line, "seq", seq);
    out.seq = seq;
    std::string txt;
    if (parse_string_field(line, "text", txt)) out.text = txt;
    std::string lang;
    if (parse_string_field(line, "lang", lang)) out.lang = lang; else out.lang.clear();
    return true;
}

void TextSourceClient::recvLoop()
{
    std::string line;
    line.reserve(1024);
    char buf[512];
    while (running_.load())
    {
        ptrdiff_t n = ::recv(sock_, buf, sizeof(buf), 0);
        if (n <= 0) break;
        for (ptrdiff_t i = 0; i < n; ++i)
        {
            char c = buf[i];
            if (c == '\n')
            {
                Incoming msg;
                if (parseJsonLine(line, msg))
                {
                    std::lock_guard<std::mutex> lock(in_mutex_);
                    inbox_.push_back(std::move(msg));
                }
                line.clear();
            }
            else if (c != '\r')
            {
                line.push_back(c);
                if (line.size() > 32768) line.clear();
            }
        }
    }
    running_.store(false);
}

void TextSourceClient::closeSocket()
{
    if (sock_ >= 0)
    {
#if defined(_WIN32)
        closesocket(sock_);
#else
        close(sock_);
#endif
        sock_ = -1;
    }
}
