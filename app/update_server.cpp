#include <winsock2.h>
#include <ws2tcpip.h>
#include "app_paths.hpp"
#include <atomic>
#include <fstream>
#include <thread>
#include <iostream>
#include <array>
#include <algorithm>
namespace {
std::atomic<int> clients{};
struct Socket {
    SOCKET value{INVALID_SOCKET};
    ~Socket() {
        if (value != INVALID_SOCKET)
            closesocket(value);
    }
};
bool Send(SOCKET socket, const char *data, size_t size) {
    while (size) {
        int n = send(socket, data, static_cast<int>(std::min(size, size_t(65536))), 0);
        if (n <= 0)
            return false;
        data += n;
        size -= n;
    }
    return true;
}
void Error(SOCKET socket, int code, const char *message) {
    auto body = std::string(message) + "\n";
    auto response = "HTTP/1.1 " + std::to_string(code) + " " + message +
                    "\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(body.size()) +
                    "\r\nConnection: close\r\n\r\n" + body;
    Send(socket, response.data(), response.size());
}
void Serve(SOCKET raw, std::filesystem::path root) {
    Socket socket{raw};
    struct Count {
        ~Count() { --clients; }
    } count;
    try {
        DWORD timeout = 15000;
        setsockopt(raw, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&timeout), sizeof(timeout));
        setsockopt(raw, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char *>(&timeout), sizeof(timeout));
        std::string request;
        std::array<char, 4096> buffer{};
        while (request.find("\r\n\r\n") == std::string::npos) {
            int n = recv(raw, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (n <= 0)
                return;
            request.append(buffer.data(), n);
            if (request.size() > 16384) {
                Error(raw, 431, "Request too large");
                return;
            }
        }
        auto space = request.find(' '), end = request.find(' ', space + 1);
        if (space == std::string::npos || end == std::string::npos) {
            Error(raw, 400, "Bad request");
            return;
        }
        auto method = request.substr(0, space), url = request.substr(space + 1, end - space - 1);
        if (method != "GET" && method != "HEAD") {
            Error(raw, 405, "Method not allowed");
            return;
        }
        if (auto query = url.find('?'); query != std::string::npos)
            url.resize(query);
        if (url == "/")
            url = "/index.html";
        if (url.size() < 2 || url[0] != '/' || url.find("..") != std::string::npos ||
            url.find_first_not_of("/abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") !=
                std::string::npos ||
            url.find('/', 1) != std::string::npos) {
            Error(raw, 404, "Not found");
            return;
        }
        const auto path = root / vortex::Wide(url.substr(1));
        const auto extension = path.extension().wstring();
        if (extension != L".json" && extension != L".nupkg" && extension != L".exe" && extension != L".html" &&
            extension != L".md" && path.filename() != L"RELEASES") {
            Error(raw, 404, "Not found");
            return;
        }
        std::error_code error;
        const auto canonical = std::filesystem::canonical(path, error);
        if (error || canonical.parent_path() != root || !std::filesystem::is_regular_file(canonical)) {
            Error(raw, 404, "Not found");
            return;
        }
        vortex::Handle file(CreateFileW(canonical.c_str(), GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
        LARGE_INTEGER size{};
        if (!file || !GetFileSizeEx(file.value, &size)) {
            Error(raw, 404, "Not found");
            return;
        }
        std::string type = extension == L".html"   ? "text/html; charset=utf-8"
                           : extension == L".json" ? "application/json"
                                                   : "application/octet-stream";
        auto header = "HTTP/1.1 200 OK\r\nContent-Type: " + type +
                      "\r\nContent-Length: " + std::to_string(size.QuadPart) +
                      "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: "
                      "default-src 'none'; style-src 'unsafe-inline'\r\nConnection: close\r\n\r\n";
        if (!Send(raw, header.data(), header.size()) || method == "HEAD")
            return;
        std::array<char, 65536> bytes{};
        DWORD read{};
        while (ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read)
            if (!Send(raw, bytes.data(), read))
                break;
    } catch (...) {
        Error(raw, 500, "Server error");
    }
}
} // namespace
int wmain(int argc, wchar_t **argv) {
    if (argc != 3) {
        std::cerr << "Usage: VortexUpdateServer.exe <release-directory> <port>\n";
        return 2;
    }
    try {
        const auto root = std::filesystem::canonical(argv[1]);
        if (!std::filesystem::is_directory(root))
            throw std::runtime_error("Release directory is missing.");
        size_t consumed{};
        const int port = std::stoi(argv[2], &consumed);
        if (consumed != wcslen(argv[2]) || port < 1024 || port > 65535)
            throw std::runtime_error("Choose a port between 1024 and 65535.");
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data))
            throw std::runtime_error("Cannot initialize networking.");
        Socket listener{socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
        if (listener.value == INVALID_SOCKET)
            throw std::runtime_error("Cannot create listener.");
        BOOL exclusive = TRUE;
        setsockopt(listener.value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<char *>(&exclusive),
                   sizeof(exclusive));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<u_short>(port));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener.value, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == SOCKET_ERROR ||
            listen(listener.value, 16) == SOCKET_ERROR)
            throw std::runtime_error("The update server port is already in use.");
        std::cout << "Vortex updates: http://127.0.0.1:" << port << "/\n" << std::flush;
        for (;;) {
            SOCKET client = accept(listener.value, nullptr, nullptr);
            if (client == INVALID_SOCKET)
                break;
            if (clients.fetch_add(1) >= 16) {
                --clients;
                Error(client, 503, "Busy");
                closesocket(client);
                continue;
            }
            try {
                std::thread(Serve, client, root).detach();
            } catch (...) {
                --clients;
                closesocket(client);
            }
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
