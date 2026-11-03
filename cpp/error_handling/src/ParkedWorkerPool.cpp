#include <cpp_mastery/runtime/ParkedWorkerPool.hpp>

#include <stdexcept>
#include <stop_token>
#include <utility>

namespace cpp_mastery::runtime
{

struct ParkedWorkerPool::Gate
{
    // 0 = parked, 1 = activated, 2 = cancelled. One atomic both prevents
    // missed wake-ups and supports a non-throwing activation operation.
    std::atomic<int> status{0};
    std::atomic<int> parked{0};
};

ParkedWorkerPool::ParkedWorkerPool(const int count, std::shared_ptr<WorkerStats> stats,
                                   BeforeLaunch before_launch)
    : gate_{std::make_shared<Gate>()}, stats_{std::move(stats)}, size_{count}
{
    if (count < 1 || count > 64 || !stats_)
    {
        throw std::invalid_argument{"worker pool needs [1, 64] workers and stats"};
    }

    threads_.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index)
    {
        if (before_launch)
        {
            before_launch(index + 1);
        }

        threads_.emplace_back(
            [gate = gate_, stats = stats_](std::stop_token stop)
            {
                // An interrupted constructor destroys existing jthreads. The first
                // requested stop cancels the entire unpublished pool, waking all.
                std::stop_callback cancel_on_stop{
                    stop, [gate]() noexcept
                    {
                        int waiting = 0;
                        if (gate->status.compare_exchange_strong(waiting, 2,
                                                                 std::memory_order_acq_rel))
                        {
                            gate->status.notify_all();
                        }
                    }};

                stats->parked.fetch_add(1, std::memory_order_release);
                gate->parked.fetch_add(1, std::memory_order_release);
                gate->parked.notify_all();

                // Unlike a naked condition-variable notification, atomic wait
                // remembers the new state even if activation happened earlier.
                gate->status.wait(0, std::memory_order_acquire);
                if (gate->status.load(std::memory_order_acquire) == 1 && !stop.stop_requested())
                {
                    // Simulated task: observable work starts only after commit.
                    stats->tasks_executed.fetch_add(1, std::memory_order_release);
                }
                stats->exited.fetch_add(1, std::memory_order_release);
            });

        // The next launch (including an injected failure) occurs only after
        // the previous thread has confirmed it is parked.
        while (gate_->parked.load(std::memory_order_acquire) < index + 1)
        {
            const int observed = gate_->parked.load(std::memory_order_acquire);
            gate_->parked.wait(observed, std::memory_order_acquire);
        }
    }
}

void ParkedWorkerPool::activate() noexcept
{
    // Publication happened before this call. Atomic store+notify cannot throw.
    gate_->status.store(1, std::memory_order_release);
    gate_->status.notify_all();
}

} // namespace cpp_mastery::runtime
