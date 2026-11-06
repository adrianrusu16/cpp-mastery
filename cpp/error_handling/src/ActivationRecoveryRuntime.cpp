#include <cpp_mastery/runtime/ActivationRecoveryRuntime.hpp>

#include <limits>
#include <stdexcept>
#include <utility>

namespace cpp_mastery::runtime
{
namespace
{

class VerifiedTeardown final : public SafeTeardownAuthority
{
  public:
    explicit VerifiedTeardown(BackendGeneration generation) noexcept : generation_{generation} {}
    [[nodiscard]] bool confirmed_safe_to_destroy(BackendGeneration generation) override
    {
        return generation == generation_;
    }

  private:
    BackendGeneration generation_;
};

} // namespace

// Caches the two PREPARED resources for the coordinator's normal construction
// path. Subsequent publications delegate to the underlying active backend.
// Snapshot buffer leases retain this wrapper and thus the underlying backend.
class ActivationRecoveryRuntime::PreparedBackend final : public ReconstructionBackend
{
  public:
    PreparedBackend(std::shared_ptr<ActivatableBackend> backend,
                    std::unique_ptr<RenderBuffer> geometry,
                    std::unique_ptr<RenderBuffer> lighting) noexcept
        : backend_{std::move(backend)}, geometry_{std::move(geometry)},
          lighting_{std::move(lighting)}
    {
    }

    [[nodiscard]] BackendGeneration generation() const noexcept override
    {
        return backend_->generation();
    }

    [[nodiscard]] std::unique_ptr<RenderBuffer> allocate(BufferKind kind, int distance) override
    {
        if (kind == BufferKind::geometry && geometry_)
        {
            return std::move(geometry_);
        }
        if (kind == BufferKind::lighting && lighting_)
        {
            return std::move(lighting_);
        }
        return backend_->allocate(kind, distance);
    }

  private:
    std::shared_ptr<ActivatableBackend> backend_;
    std::unique_ptr<RenderBuffer> geometry_;
    std::unique_ptr<RenderBuffer> lighting_;
};

struct ActivationRecoveryRuntime::Pending
{
    BackendGeneration old_generation;
    config::EngineConfig config;
    std::shared_ptr<ActivatableBackend> backend;
    std::shared_ptr<PreparedBackend> prepared;
    bool confirmed_started = false;
};

namespace
{
class PreparedBuilder final : public ReconstructionBuilder
{
  public:
    explicit PreparedBuilder(std::shared_ptr<ReconstructionBackend> prepared) noexcept
        : prepared_{std::move(prepared)}
    {
    }

    [[nodiscard]] std::shared_ptr<ReconstructionBackend> build(BackendGeneration) override
    {
        return prepared_;
    }

  private:
    std::shared_ptr<ReconstructionBackend> prepared_;
};
} // namespace

ActivationRecoveryRuntime::ActivationRecoveryRuntime(const config::EngineConfig& initial,
                                                     ActivationBuilder& builder)
    : runtime_{std::make_unique<TransactionalBackendRuntime>(initial, builder)}
{
}

ActivationRecoveryRuntime::~ActivationRecoveryRuntime() = default;

BackendGeneration ActivationRecoveryRuntime::generation() const noexcept
{
    return runtime_->generation();
}

bool ActivationRecoveryRuntime::device_lost() const noexcept
{
    return runtime_->device_lost();
}

bool ActivationRecoveryRuntime::activation_pending() const noexcept
{
    return static_cast<bool>(pending_);
}

std::shared_ptr<const RuntimeSnapshot> ActivationRecoveryRuntime::acquire() const noexcept
{
    return runtime_->acquire();
}

void ActivationRecoveryRuntime::publish(const BackendGeneration gen,
                                        const config::EngineConfig& next)
{
    runtime_->publish(gen, next);
}

void ActivationRecoveryRuntime::report_device_lost(const BackendGeneration gen)
{
    runtime_->report_device_lost(gen);
}

SubmissionResult ActivationRecoveryRuntime::try_submit(const BackendGeneration gen,
                                                       const GpuQueueKind queue,
                                                       SubmissionBackend& backend)
{
    return runtime_->try_submit(gen, queue, backend);
}

void ActivationRecoveryRuntime::complete_through(const BackendGeneration gen,
                                                 const GpuQueueKind queue,
                                                 const std::uint64_t serial)
{
    runtime_->complete_through(gen, queue, serial);
}

std::uint64_t ActivationRecoveryRuntime::submitted(const GpuQueueKind queue) const
{
    return runtime_->submitted(queue);
}

std::size_t ActivationRecoveryRuntime::collect()
{
    return runtime_->collect();
}

std::size_t ActivationRecoveryRuntime::unresolved_count() const noexcept
{
    return runtime_->unresolved_count();
}

BackendGeneration ActivationRecoveryRuntime::commit_pending()
{
    if (!pending_ || !pending_->confirmed_started)
    {
        throw std::logic_error{"activation is not confirmed"};
    }

    PreparedBuilder builder{pending_->prepared};
    VerifiedTeardown verified{pending_->old_generation};
    // The underlying transaction publishes ONLY after both buffers have been
    // acquired. If that stage throws, pending_ retains the activated backend.
    const auto generation =
        runtime_->recover(pending_->old_generation, pending_->config, builder, verified);
    pending_.reset();
    return generation;
}

std::optional<BackendGeneration>
ActivationRecoveryRuntime::recover(const BackendGeneration old_generation,
                                   const config::EngineConfig& next, ActivationBuilder& builder,
                                   SafeTeardownAuthority& authority)
{
    if (pending_)
    {
        throw std::logic_error{"resolve the prior activation before retrying"};
    }
    if (old_generation != generation())
    {
        throw std::invalid_argument{"stale recovery generation"};
    }
    if (!device_lost())
    {
        throw std::logic_error{"recovery requires device loss"};
    }
    if (old_generation.value == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error{"generation exhausted"};
    }
    if (!authority.confirmed_safe_to_destroy(old_generation))
    {
        throw std::logic_error{"old backend teardown not proven safe"};
    }

    const BackendGeneration next_generation{old_generation.value + 1};
    auto backend = builder.build_activatable(next_generation);
    if (!backend || backend->generation() != next_generation)
    {
        throw std::logic_error{"invalid reconstructed backend"};
    }

    // Every fallible preparation operation happens BEFORE activation.
    auto geometry = backend->allocate(BufferKind::geometry, next.render_distance);
    if (!geometry)
    {
        throw std::runtime_error{"new backend returned null geometry"};
    }
    auto lighting = backend->allocate(BufferKind::lighting, next.render_distance);
    if (!lighting)
    {
        throw std::runtime_error{"new backend returned null lighting"};
    }
    auto prepared =
        std::make_shared<PreparedBackend>(backend, std::move(geometry), std::move(lighting));
    pending_ = std::make_unique<Pending>(old_generation, next, std::move(backend),
                                         std::move(prepared), false);

    ActivationOutcome outcome = ActivationOutcome::unknown;
    try
    {
        outcome = pending_->backend->activate();
    }
    catch (...)
    {
        // A throwing activation has UNKNOWN outcome unless independently proven.
        outcome = ActivationOutcome::unknown;
    }

    // Lesson 15 GREEN: a confirmed pre-start rejection MUST NOT publish.
    if (outcome == ActivationOutcome::not_started)
    {
        pending_.reset();
        throw std::runtime_error{"activation rejected before external work started"};
    }
    if (outcome != ActivationOutcome::started)
    {
        // Unknown or invalid provider response: fail closed.
        return std::nullopt;
    }

    pending_->confirmed_started = true;
    return commit_pending();
}

std::optional<BackendGeneration>
ActivationRecoveryRuntime::resolve_pending(const ActivationResolution resolution)
{
    if (!pending_)
    {
        throw std::logic_error{"no pending activation"};
    }
    if (pending_->confirmed_started && resolution == ActivationResolution::confirmed_not_started)
    {
        throw std::logic_error{"cannot contradict confirmed activation"};
    }
    if (resolution == ActivationResolution::confirmed_not_started)
    {
        pending_.reset();
        return std::nullopt;
    }
    if (resolution != ActivationResolution::confirmed_started)
    {
        throw std::invalid_argument{"unknown activation resolution"};
    }
    pending_->confirmed_started = true;
    return commit_pending();
}

} // namespace cpp_mastery::runtime
