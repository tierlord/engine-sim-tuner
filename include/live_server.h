#ifndef ATG_ENGINE_SIM_LIVE_SERVER_H
#define ATG_ENGINE_SIM_LIVE_SERVER_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Tiny HTTP server on 127.0.0.1 used by tools/engine-builder.html in live mode.
//
//   GET  /status     -> {"app":"engine-sim","live":true}
//   POST /engine     -> body = engine script; answers with the JSON produced by the handler
//   POST /dyno       -> starts a dyno run (body = "key=value" lines)
//   POST /dyno/stop  -> aborts it
//   GET  /dyno       -> progress and measured points
//   GET  /<file>     -> static files of the user interface (see setWebRoot), "/" is index.html
//   GET  /events     -> Server-Sent Events: every publish() reaches all open streams. An open stream
//                       tells the program that a user interface is alive (see streamCount()).
//   GET  /telemetry  -> current engine values (rpm, load, ignition advance, knock, ...)
//   POST /control    -> driver input as "key=value" lines (throttle, clutch, starter, ignition, shift, dyno ...)
//
// Requests are handed to the main thread (poll()), so the simulation is only touched from there.
class LiveServer {
    public:
        using Handler = std::function<std::string(
            const std::string &method, const std::string &path, const std::string &body)>;

        LiveServer();
        ~LiveServer();

        bool start(int port);
        void setWebRoot(const std::string &directory) { m_webRoot = directory; }   // GET of other paths serves files from here
        void stop();
        bool isRunning() const { return m_running; }
        int getPort() const { return m_port; }

        // Call from the main thread every frame
        void poll(const Handler &handler);

        // Sends one event to every open /events stream (main thread). Streams that cannot take it are closed.
        void publish(const std::string &json);
        int streamCount() const { return m_streamCount; }

    private:
        void serve();
        bool serveFile(uintptr_t client, const std::string &path, const std::string &origin);
        bool handleConnection(uintptr_t client);          // true: the socket was kept as an event stream
        void closeStreams();

        std::thread m_thread;
        std::atomic<bool> m_running;
        uintptr_t m_listenSocket;
        int m_port;

        std::string m_webRoot;
        std::mutex m_streamMutex;
        std::vector<uintptr_t> m_streams;
        std::atomic<int> m_streamCount;

        std::mutex m_mutex;
        std::condition_variable m_cv;
        bool m_hasRequest;
        bool m_hasResponse;
        std::string m_requestMethod;
        std::string m_requestPath;
        std::string m_requestBody;
        std::string m_responseBody;
};

#endif /* ATG_ENGINE_SIM_LIVE_SERVER_H */
