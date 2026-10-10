#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <vector>

namespace clicklimit {
enum class Button { left, right, middle };
enum class Kind { move, down, up };
struct Event { Kind kind; Button button; int x = 0, y = 0; };
struct Options {
    bool infinite = false, fixed = false, responsive = false;
    std::uint64_t limit = 1000;
    unsigned button = 0, clicks = 1, batch = 64;
    double interval = .01;
    int x = 0, y = 0;
    std::uint64_t budgetNs = 0;
};
struct SendResult { unsigned inserted = 0; unsigned error = 0; };
struct Result {
    std::uint64_t clicks = 0, cycles = 0, withinBudget = 0, elapsedNs = 0;
    unsigned error = 0;
    bool failed = false, cancelled = false;
};
// Materialize the complete buffer before measuring or starting the input loop.
inline std::vector<Event> prepare(const Options& o) {
    const unsigned capacity = o.interval == 0 ? (o.responsive ? std::min(16u,o.batch) : o.batch) : 1;
    std::vector<Event> events;
    events.reserve(capacity * o.clicks * 2 + (o.fixed ? 1 : 0));
    const auto button = static_cast<Button>(o.button);
    if (o.fixed) events.push_back({Kind::move, button, o.x, o.y});
    for (unsigned i = 0; i < capacity * o.clicks; ++i) {
        events.push_back({Kind::down, button}); events.push_back({Kind::up, button});
    }
    return events;
}
// Backend contract: prepare(events), send(prefixCount), release(button),
// nowNs(), waitUntil(deadlineNs, stop). Tests use an in-memory backend.
template<class Backend, class Progress>
Result run(const Options& o, std::atomic<bool>& stop, Backend& backend, Progress progress) {
    Result result;
    const unsigned capacity = o.interval == 0 ? (o.responsive ? std::min(16u,o.batch) : o.batch) : 1;
    const unsigned perCycle = o.clicks * 2;
    const unsigned prefix = o.fixed ? 1 : 0;
    const auto events = prepare(o);
    backend.prepare(events);
    const auto start = backend.nowNs();
    auto deadline = start;
    unsigned unpublished = 0;
    while (!stop.load(std::memory_order_relaxed) && (o.infinite || result.cycles < o.limit)) {
        if (o.budgetNs && backend.nowNs() - start >= o.budgetNs) break;
        const auto cycles = static_cast<unsigned>(o.infinite ? capacity :
            std::min<std::uint64_t>(capacity, o.limit - result.cycles));
        const unsigned requested = prefix + cycles * perCycle;
        const SendResult sent = backend.send(requested);
        const unsigned mouseEvents = sent.inserted > prefix ? sent.inserted - prefix : 0;
        result.clicks += mouseEvents / 2;
        result.cycles += mouseEvents / perCycle;
        // In ordinary turbo mode no clock read, keyboard query, or UI call occurs per batch.
        if (o.budgetNs) {
            const auto after = backend.nowNs() - start;
            if (after <= o.budgetNs) result.withinBudget = result.clicks;
        }
        if (sent.inserted != requested) {
            result.failed = true; result.error = sent.error;
            if (mouseEvents % 2) backend.release(static_cast<Button>(o.button));
            break;
        }
        if (++unpublished >= 16) { progress(result.clicks); unpublished = 0; }
        if(stop.load(std::memory_order_relaxed))break;
        if(o.responsive && o.interval==0 && (o.infinite || result.cycles<o.limit))backend.pause(stop);
        else if (o.interval > 0 && (o.infinite || result.cycles < o.limit)) {
            const auto spacing = static_cast<std::uint64_t>(o.interval * 1e9);
            const auto now = backend.nowNs();
            deadline = std::max(deadline + spacing, now);
            backend.waitUntil(deadline, stop);
        }
    }
    result.cancelled = stop.load(std::memory_order_relaxed);
    result.elapsedNs = backend.nowNs() - start;
    progress(result.clicks);
    return result;
}
}
