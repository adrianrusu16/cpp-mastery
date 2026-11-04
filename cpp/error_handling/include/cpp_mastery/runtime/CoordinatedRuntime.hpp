#pragma once

#include <cpp_mastery/runtime/CrossQueueHandoff.hpp>
#include <cpp_mastery/runtime/MultiQueueGpuRetirementQueue.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace cpp_mastery::runtime
{

// CPU-only integration lab. Readers may acquire immutable snapshots without a
// ticket, but GPU submission requires an admission that retirement honors.
// One writer publishes updates; the mock GPU completion signal is caller-owned.
class CoordinatedRuntime final
{
    struct Generation;
    struct State;

  public:
    class Admission final
    {
      public:
        Admission(Admission&& other) noexcept = default;
        Admission& operator=(Admission&& other) noexcept;
        Admission(const Admission&) = delete;
        Admission& operator=(const Admission&) = delete;
        ~Admission();

        [[nodiscard]] std::shared_ptr<const RuntimeSnapshot> snapshot() const noexcept;

        // A successful submission consumes this ticket. On an exception from
        // registration, the ticket is still pending and can be retried/cancelled.
        [[nodiscard]] std::uint64_t submit(GpuQueueKind queue);

        // Compose the Lesson 10 handoff precondition with graphics admission.
        // The external handoff model must not be modified concurrently.
        [[nodiscard]] std::uint64_t submit_graphics_after(const CrossQueueHandoff& handoff);

        // Safe to call more than once. The destructor cancels pending admission.
        void cancel() noexcept;

      private:
        friend class CoordinatedRuntime;
        Admission(std::shared_ptr<State> state, std::shared_ptr<Generation> generation) noexcept;

        std::shared_ptr<State> state_;
        std::shared_ptr<Generation> generation_;
    };

    CoordinatedRuntime(const config::EngineConfig& initial, ResourceFactory& factory);

    // CPU-only readers retain their snapshot; this is NOT GPU submission admission.
    [[nodiscard]] std::shared_ptr<const RuntimeSnapshot> acquire() const noexcept;

    // Creates a retirement-aware reservation for the currently published version.
    [[nodiscard]] Admission admit();

    // Build the next immutable snapshot outside the admission lock, then publish.
    // Strong exception guarantee for failed preparation/retired-list allocation.
    void publish(const config::EngineConfig& next, ResourceFactory& factory);

    // GPU-completion model: callers signal each ordered queue independently.
    void complete_through(GpuQueueKind queue, std::uint64_t submission);
    [[nodiscard]] std::size_t collect();
    [[nodiscard]] std::uint64_t submitted(GpuQueueKind queue) const;
    [[nodiscard]] std::size_t pending_admissions() const;
    [[nodiscard]] std::size_t retired_generations() const;

  private:
    std::shared_ptr<State> state_;
};

} // namespace cpp_mastery::runtime
