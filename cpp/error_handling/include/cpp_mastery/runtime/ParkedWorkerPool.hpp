#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace cpp_mastery::runtime
{

// A deliberately small task simulation: each activated worker records one task.
// Atomic counters let tests observe cleanup without unsafe cross-thread reads.
struct WorkerStats
{
    std::atomic<int> parked{0};
    std::atomic<int> exited{0};
    std::atomic<int> tasks_executed{0};
};

// New workers remain parked until activate(). On construction failure, jthread
// destruction requests stop; stop callbacks wake and join staged workers.
class ParkedWorkerPool final
{
  public:
    // Called just before each thread creation. Test hooks may throw to simulate
    // std::jthread construction failures, including the fifth launch.
    using BeforeLaunch = std::function<void(int)>;

    ParkedWorkerPool(int count, std::shared_ptr<WorkerStats> stats,
                     BeforeLaunch before_launch = {});

    ParkedWorkerPool(const ParkedWorkerPool&) = delete;
    ParkedWorkerPool& operator=(const ParkedWorkerPool&) = delete;

    // Called only after publication; only atomic operations, no allocations or locks.
    void activate() noexcept;
    [[nodiscard]] int size() const noexcept
    {
        return size_;
    }

  private:
    struct Gate;

    // Order is intentional: threads_ is destroyed BEFORE gate_ and stats_.
    std::shared_ptr<Gate> gate_;
    std::shared_ptr<WorkerStats> stats_;
    int size_;
    std::vector<std::jthread> threads_;
};

} // namespace cpp_mastery::runtime
