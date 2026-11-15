#pragma once

// Engine-independent queue so the slow-storage and saturation behavior can be
// tested without starting the game. Only the worker calls the supplied sink.
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class AsyncDiagnosticLog
{
public:
    struct Entry { unsigned channel; std::string text; bool verbose; };
    using Batch = std::vector<Entry>;
    using Sink = std::function<void(const Batch&)>;
    static constexpr size_t MAX_BYTES = 1024 * 1024;
    static constexpr size_t MAX_ENTRIES = 2048;
    static constexpr size_t RESERVED_BYTES = 256 * 1024;
    static constexpr size_t RESERVED_ENTRIES = 256;
    static constexpr size_t MAX_MESSAGE_BYTES = 512 * 1024;

    explicit AsyncDiagnosticLog(Sink sink) : m_sink(std::move(sink)), m_worker([this] { run(); }) {}
    ~AsyncDiagnosticLog()
    {
        { std::lock_guard<std::mutex> lock(m_mutex); m_stop = true; }
        m_wake.notify_one();
        m_worker.join();
    }
    AsyncDiagnosticLog(const AsyncDiagnosticLog&) = delete;
    AsyncDiagnosticLog& operator=(const AsyncDiagnosticLog&) = delete;

    // No disk I/O, and the worker never holds this mutex while accessing disk.
    // rateKey 1..32 identifies a recurring error (e.g. duplicate resend/slot).
    void submit(unsigned channel, std::string text, bool verbose = false, unsigned rateKey = 0)
    {
        if (channel >= 2) return;
        if (text.size() > MAX_MESSAGE_BYTES) {
            text = text.substr(0, MAX_MESSAGE_BYTES - 32) + " [log message truncated]\n";
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stop) return;
        if (rateKey > 0 && rateKey <= m_rates.size()) {
            Rate& rate = m_rates[rateKey - 1];
            const auto now = Clock::now();
            if (rate.started && now - rate.since < std::chrono::seconds(5)) {
                ++rate.suppressed;
                rate.last = text.substr(0, 1024);
                return;
            }
            summarize(rate);
            rate.started = true;
            rate.since = now;
            rate.channel = channel;
            rate.last = text.substr(0, 1024);
        }
        enqueue({channel, std::move(text), verbose});
        if (m_bytes >= 64 * 1024) m_wake.notify_one();
    }

    // Explicit, bounded barrier for uploads and orderly exit. Normal log calls
    // never wait for storage. A timeout means the file may have an incomplete tail.
    bool flush(unsigned timeoutMs)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        for (auto& rate : m_rates) summarize(rate);
        const uint64_t ticket = ++m_requested;
        m_wake.notify_one();
        return m_done.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return m_completed >= ticket; });
    }

private:
    using Clock = std::chrono::steady_clock;
    struct Rate {
        bool started = false;
        Clock::time_point since;
        unsigned channel = 0;
        uint64_t suppressed = 0;
        std::string last;
    };
    void enqueue(Entry entry)
    {
        const size_t byteLimit = entry.verbose ? MAX_BYTES - RESERVED_BYTES : MAX_BYTES;
        const size_t entryLimit = entry.verbose ? MAX_ENTRIES - RESERVED_ENTRIES : MAX_ENTRIES;
        // Important entries may reclaim verbose space once their reserve fills.
        while (!entry.verbose && (m_bytes + entry.text.size() > byteLimit || m_queue.size() >= entryLimit)) {
            auto victim = m_queue.begin();
            while (victim != m_queue.end() && !victim->verbose) ++victim;
            if (victim == m_queue.end()) break;
            m_bytes -= victim->text.size();
            ++m_dropped[victim->channel][1];
            m_queue.erase(victim);
        }
        if (m_bytes + entry.text.size() > byteLimit || m_queue.size() >= entryLimit) {
            ++m_dropped[entry.channel][entry.verbose ? 1 : 0];
            return;
        }
        m_bytes += entry.text.size();
        m_queue.push_back(std::move(entry));
    }
    void summarize(Rate& rate)
    {
        if (!rate.suppressed) return;
        enqueue({rate.channel, "LOG repeated error: suppressed=" + std::to_string(rate.suppressed)
            + " last=" + rate.last + "\n", false});
        rate.suppressed = 0;
    }
    void run()
    {
        for (;;) {
            Batch batch;
            uint64_t ticket;
            bool stop;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                // Wake once a second, at 64 KiB, or at an explicit barrier.
                m_wake.wait_for(lock, std::chrono::seconds(1), [&] {
                    return m_stop || m_requested != m_completed || m_bytes >= 64 * 1024;
                });
                for (auto& rate : m_rates)
                    if (m_stop || (rate.started && Clock::now() - rate.since >= std::chrono::seconds(5))) summarize(rate);
                batch.reserve(m_queue.size() + 2);
                while (!m_queue.empty()) {
                    batch.push_back(std::move(m_queue.front()));
                    m_queue.pop_front();
                }
                m_bytes = 0;
                for (unsigned channel = 0; channel < 2; ++channel) {
                    auto& dropped = m_dropped[channel];
                    if (dropped[0] || dropped[1]) {
                        batch.push_back({channel, "LOG queue overflow: dropped important=" + std::to_string(dropped[0])
                            + " verbose=" + std::to_string(dropped[1]) + "\n", false});
                        dropped = {};
                    }
                }
                ticket = m_requested;
                stop = m_stop;
            }
            // A failed sink must not terminate the process. The next batch may
            // still be useful; I/O errors are handled by the sink itself.
            try { m_sink(batch); } catch (...) {}
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_completed = ticket;
            }
            m_done.notify_all();
            if (stop) return;
        }
    }
    Sink m_sink;
    std::mutex m_mutex;
    std::condition_variable m_wake, m_done;
    std::deque<Entry> m_queue;
    size_t m_bytes = 0;
    std::array<std::array<uint64_t, 2>, 2> m_dropped{};
    std::array<Rate, 32> m_rates;
    uint64_t m_requested = 0, m_completed = 0;
    bool m_stop = false;
    std::thread m_worker;
};
