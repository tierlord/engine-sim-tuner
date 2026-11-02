#include "../include/live_server.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>

#pragma comment(lib, "ws2_32.lib")

namespace {
    constexpr size_t MaxRequestSize = 4 * 1024 * 1024;

    std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    // Only the local file:// page (Origin "null") and localhost pages may talk to the simulator.
    // Any other website must not be able to push scripts into the app.
    bool originAllowed(const std::string &origin) {
        if (origin.empty() || origin == "null") return true;
        return origin.compare(0, 16, "http://localhost") == 0
            || origin.compare(0, 16, "http://127.0.0.1") == 0
            || origin.compare(0, 7, "file://") == 0;
    }

    void sendAll(SOCKET s, const std::string &data) {
        size_t sent = 0;
        while (sent < data.size()) {
            const int n = send(s, data.data() + sent, (int)(data.size() - sent), 0);
            if (n <= 0) return;
            sent += n;
        }
    }

    std::string response(int code, const char *status, const std::string &origin, const std::string &body) {
        std::string r = "HTTP/1.1 " + std::to_string(code) + " " + status + "\r\n";
        r += "Content-Type: application/json\r\n";
        r += "Access-Control-Allow-Origin: " + (origin.empty() ? std::string("*") : origin) + "\r\n";
        r += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
        r += "Access-Control-Allow-Headers: Content-Type, X-Engine-Builder\r\n";
        r += "Access-Control-Allow-Private-Network: true\r\n";
        r += "Vary: Origin\r\n";
        r += "Cache-Control: no-store\r\n";
        r += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        r += "Connection: close\r\n\r\n";
        return r + body;
    }
}

LiveServer::LiveServer() {
    m_running = false;
    m_listenSocket = (uintptr_t)INVALID_SOCKET;
    m_port = 0;
    m_hasRequest = false;
    m_hasResponse = false;
}

LiveServer::~LiveServer() {
    stop();
}

bool LiveServer::start(int port) {
    if (m_running) return true;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;

    BOOL reuse = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);       // loopback only

    if (bind(s, (sockaddr *)&addr, sizeof(addr)) != 0 || listen(s, 4) != 0) {
        closesocket(s);
        return false;
    }

    m_listenSocket = (uintptr_t)s;
    m_port = port;
    m_running = true;
    m_hasRequest = false;
    m_hasResponse = false;
    m_thread = std::thread(&LiveServer::serve, this);

    return true;
}

void LiveServer::stop() {
    if (!m_running) return;

    m_running = false;
    m_cv.notify_all();                       // release a request that is waiting for the main thread
    if (m_thread.joinable()) m_thread.join();

    closesocket((SOCKET)m_listenSocket);
    m_listenSocket = (uintptr_t)INVALID_SOCKET;
}

void LiveServer::serve() {
    while (m_running) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET((SOCKET)m_listenSocket, &readSet);
        timeval timeout = { 0, 200 * 1000 };

        if (select(0, &readSet, nullptr, nullptr, &timeout) > 0) {
            SOCKET client = accept((SOCKET)m_listenSocket, nullptr, nullptr);
            if (client != INVALID_SOCKET) {
                DWORD ioTimeout = 3000;
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ioTimeout, sizeof(ioTimeout));
                handleConnection((uintptr_t)client);

                // Close gracefully: unread request data would otherwise turn into a connection reset
                shutdown(client, SD_SEND);
                char sink[4096];
                for (int i = 0; i < 256 && recv(client, sink, sizeof(sink), 0) > 0; ++i) { /* drain */ }
                closesocket(client);
            }
        }
    }
}

void LiveServer::handleConnection(uintptr_t clientHandle) {
    SOCKET client = (SOCKET)clientHandle;

    // Read until the end of the headers
    std::string data;
    char buffer[4096];
    size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos && data.size() < MaxRequestSize) {
        const int n = recv(client, buffer, sizeof(buffer), 0);
        if (n <= 0) return;
        data.append(buffer, n);
        headerEnd = data.find("\r\n\r\n");
    }
    if (headerEnd == std::string::npos) return;

    const std::string head = data.substr(0, headerEnd);
    std::string body = data.substr(headerEnd + 4);

    // Request line + headers
    const size_t lineEnd = head.find("\r\n");
    const std::string requestLine = head.substr(0, lineEnd);
    const size_t sp1 = requestLine.find(' ');
    const size_t sp2 = requestLine.find(' ', sp1 + 1);
    const std::string method = requestLine.substr(0, sp1);
    const std::string path = (sp1 == std::string::npos || sp2 == std::string::npos)
        ? "" : requestLine.substr(sp1 + 1, sp2 - sp1 - 1);

    std::string origin;
    size_t contentLength = 0;
    size_t pos = lineEnd;
    while (pos != std::string::npos && pos < head.size()) {
        const size_t next = head.find("\r\n", pos + 2);
        const std::string line = head.substr(pos + 2, next == std::string::npos ? std::string::npos : next - pos - 2);
        const size_t colon = line.find(':');
        if (colon != std::string::npos) {
            const std::string name = lower(line.substr(0, colon));
            std::string value = line.substr(colon + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            if (name == "origin") origin = value;
            else if (name == "content-length") contentLength = (size_t)std::strtoull(value.c_str(), nullptr, 10);
        }
        pos = next;
    }

    if (!originAllowed(origin)) {
        sendAll(client, response(403, "Forbidden", "", "{\"ok\":false,\"error\":\"origin not allowed\"}"));
        return;
    }

    if (method == "OPTIONS") {
        sendAll(client, response(204, "No Content", origin, ""));
        return;
    }

    if (method == "GET" && path == "/status") {
        sendAll(client, response(200, "OK", origin, "{\"app\":\"engine-sim\",\"live\":true}"));
        return;
    }

    // Everything below touches the simulation, so it is executed on the main thread
    const bool mainThreadRoute =
        (method == "POST" && path == "/engine")
        || (method == "POST" && path == "/dyno")
        || (method == "POST" && path == "/dyno/stop")
        || (method == "GET" && path == "/dyno")
        || (method == "GET" && path == "/telemetry")
        || (method == "POST" && path == "/control");

    if (mainThreadRoute) {
        const bool needsBody = (method == "POST" && path == "/engine");
        if ((needsBody && contentLength == 0) || contentLength > MaxRequestSize) {
            sendAll(client, response(400, "Bad Request", origin, "{\"ok\":false,\"error\":\"empty or too large body\"}"));
            return;
        }

        while (body.size() < contentLength) {
            const int n = recv(client, buffer, sizeof(buffer), 0);
            if (n <= 0) return;
            body.append(buffer, n);
        }
        body.resize(contentLength);

        // Hand over to the main thread and wait for its answer
        std::unique_lock<std::mutex> lock(m_mutex);
        m_requestMethod = method;
        m_requestPath = path;
        m_requestBody = std::move(body);
        m_hasResponse = false;
        m_hasRequest = true;

        const bool answered = m_cv.wait_for(lock, std::chrono::seconds(20), [this] { return m_hasResponse || !m_running; });
        m_hasRequest = false;

        if (answered && m_hasResponse) {
            sendAll(client, response(200, "OK", origin, m_responseBody));
        }
        else {
            sendAll(client, response(504, "Gateway Timeout", origin, "{\"ok\":false,\"error\":\"simulator did not answer in time\"}"));
        }
        return;
    }

    sendAll(client, response(404, "Not Found", origin, "{\"ok\":false,\"error\":\"unknown route\"}"));
}

void LiveServer::poll(const Handler &handler) {
    std::string method, path, body;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_hasRequest || m_hasResponse) return;
        method = m_requestMethod;
        path = m_requestPath;
        body = m_requestBody;
    }

    const std::string result = handler(method, path, body);       // runs on the main thread, outside the lock

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_responseBody = result;
        m_hasResponse = true;
    }
    m_cv.notify_all();
}
