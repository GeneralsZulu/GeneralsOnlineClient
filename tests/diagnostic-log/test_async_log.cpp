#include "Common/AsyncDiagnosticLog.h"
#include <cassert>
#include <future>
#include <iostream>

int main()
{
    using Log = AsyncDiagnosticLog;
    // Block the sink, simulating an indefinitely slow disk. Producers must
    // still finish and flush must time out rather than waiting for that disk.
    std::promise<void> entered, unblock;
    auto gate = unblock.get_future().share();
    std::string output[2];
    size_t largestBatch = 0;
    bool first = true;
    {
        Log log([&](const Log::Batch& batch) {
            if (first && !batch.empty()) {
                first = false;
                entered.set_value();
                gate.wait();
            }
            size_t bytes = 0;
            for (const auto& e : batch) { output[e.channel] += e.text; bytes += e.text.size(); }
            if (bytes > largestBatch) largestBatch = bytes;
        });
        log.submit(0, std::string(64 * 1024, 'x'));
        assert(entered.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready);
        auto producers = std::async(std::launch::async, [&] {
            for (int i = 0; i < 2000; ++i) log.submit(1, std::string(1024, 'v'), true);
            log.submit(0, "important-after-verbose-flood\n");
            for (int i = 0; i < 2000; ++i) log.submit(0, std::string(1024, 'i'));
        });
        assert(producers.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
        producers.get();
        assert(!log.flush(20));
        unblock.set_value();
        assert(log.flush(5000));
        assert(output[0].find("important-after-verbose-flood") != std::string::npos);
        assert(output[0].find("queue overflow: dropped important=") != std::string::npos);
        assert(output[1].find("verbose=") != std::string::npos);
        // Queue byte cap plus bounded, worker-generated overflow summaries.
        assert(largestBatch <= Log::MAX_BYTES + 512);
    }
    // Multiple producers/channels, changing details in a repeated error, final
    // summaries even when repetition stops, and an explicit upload barrier.
    std::string errors;
    unsigned lines[2] = {};
    {
        Log log([&](const Log::Batch& batch) {
            for (const auto& e : batch) { ++lines[e.channel]; errors += e.text; }
        });
        std::thread a([&] { for (int i = 0; i < 100; ++i) log.submit(0, "release\n"); });
        std::thread b([&] { for (int i = 0; i < 100; ++i) log.submit(1, "network\n"); });
        a.join(); b.join();
        for (int i = 0; i < 50; ++i) log.submit(0, "duplicate slot=2 frame=" + std::to_string(i), false, 3);
        assert(log.flush(5000));
        assert(lines[0] == 102 && lines[1] == 100);
        assert(errors.find("suppressed=49 last=duplicate slot=2 frame=49") != std::string::npos);
        log.submit(0, "drained-on-destruction\n");
    }
    assert(errors.find("drained-on-destruction") != std::string::npos);
    // A final repeated error must get a summary even without another error or
    // an upload/exit barrier to trigger it.
    {
        std::promise<void> summary;
        Log log([&](const Log::Batch& batch) {
            for (const auto& e : batch)
                if (e.text.find("suppressed=1 last=last frame=2") != std::string::npos) summary.set_value();
        });
        log.submit(1, "first frame=1", false, 17);
        log.submit(1, "last frame=2", false, 17);
        assert(summary.get_future().wait_for(std::chrono::seconds(8)) == std::future_status::ready);
    }
    // Flush by time (without a size trigger) and message-size truncation.
    {
        std::promise<std::string> written;
        Log log([&](const Log::Batch& batch) {
            if (!batch.empty()) written.set_value(batch.front().text);
        });
        log.submit(0, "timer-flush");
        assert(written.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready);
        assert(log.flush(5000));
    }
    {
        size_t size = 0;
        Log log([&](const Log::Batch& batch) {
            for (const auto& e : batch) { size = e.text.size(); assert(e.text.find("truncated") != std::string::npos); }
        });
        log.submit(0, std::string(Log::MAX_MESSAGE_BYTES * 2, 'x'));
        assert(log.flush(5000));
        assert(size <= Log::MAX_MESSAGE_BYTES);
    }
    std::cout << "async diagnostic log tests passed\n";
}
