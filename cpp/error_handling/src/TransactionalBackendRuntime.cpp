#include <cpp_mastery/runtime/TransactionalBackendRuntime.hpp>

#include <limits>
#include <stdexcept>
#include <utility>

namespace cpp_mastery::runtime
{
namespace
{

// Declaration order matters: the underlying buffer dies before its backend.
class LeasedBuffer final : public RenderBuffer
{
  public:
    LeasedBuffer(std::shared_ptr<ReconstructionBackend> backend,
                 std::unique_ptr<RenderBuffer> buffer) noexcept
        : backend_{std::move(backend)}, buffer_{std::move(buffer)}
    {
    }

    [[nodiscard]] std::size_t size_bytes() const noexcept override
    {
        return buffer_->size_bytes();
    }

  private:
    std::shared_ptr<ReconstructionBackend> backend_;
    std::unique_ptr<RenderBuffer> buffer_;
};

class LeasedFactory final : public ResourceFactory
{
  public:
    explicit LeasedFactory(std::shared_ptr<ReconstructionBackend> backend) noexcept
        : backend_{std::move(backend)}
    {
    }

    [[nodiscard]] std::unique_ptr<RenderBuffer> allocate(const BufferKind kind,
                                                         const int distance) override
    {
        auto resource = backend_->allocate(kind, distance);
        if (!resource)
        {
            return nullptr; // SnapshotRuntime checks for null and fails staging.
        }
        return std::make_unique<LeasedBuffer>(backend_, std::move(resource));
    }

  private:
    std::shared_ptr<ReconstructionBackend> backend_;
};

class VerifiedTeardown final : public SafeTeardownAuthority
{
  public:
    explicit VerifiedTeardown(BackendGeneration generation) noexcept : generation_{generation} {}
    [[nodiscard]] bool confirmed_safe_to_destroy(BackendGeneration old) override
    {
        return old == generation_;
    }

  private:
    BackendGeneration generation_;
};

[[nodiscard]] std::shared_ptr<ReconstructionBackend> checked_build(ReconstructionBuilder& builder,
                                                                   BackendGeneration target)
{
    auto backend = builder.build(target);
    if (!backend)
    {
        throw std::runtime_error{"backend builder returned null"};
    }
    if (backend->generation() != target)
    {
        throw std::logic_error{"backend builder returned wrong generation"};
    }
    return backend;
}

} // namespace

TransactionalBackendRuntime::TransactionalBackendRuntime(const config::EngineConfig& initial,
                                                         ReconstructionBuilder& builder)
{
    auto initial_backend = checked_build(builder, BackendGeneration{1});
    LeasedFactory factory{initial_backend};
    runtime_ = std::make_unique<DeviceRecoveryRuntime>(initial, factory);
    active_backend_ = std::move(initial_backend);
}

BackendGeneration TransactionalBackendRuntime::generation() const noexcept
{
    return runtime_->generation();
}

bool TransactionalBackendRuntime::device_lost() const noexcept
{
    return runtime_->device_lost();
}

std::shared_ptr<const RuntimeSnapshot> TransactionalBackendRuntime::acquire() const noexcept
{
    return runtime_->acquire();
}

void TransactionalBackendRuntime::publish(const BackendGeneration expected,
                                          const config::EngineConfig& next)
{
    LeasedFactory factory{active_backend_};
    runtime_->publish(expected, next, factory);
}

SubmissionResult TransactionalBackendRuntime::try_submit(const BackendGeneration expected,
                                                         const GpuQueueKind queue,
                                                         SubmissionBackend& backend)
{
    return runtime_->try_submit(expected, queue, backend);
}

void TransactionalBackendRuntime::report_device_lost(const BackendGeneration expected)
{
    runtime_->report_device_lost(expected);
}

void TransactionalBackendRuntime::complete_through(const BackendGeneration expected,
                                                   const GpuQueueKind queue,
                                                   const std::uint64_t submission)
{
    runtime_->complete_through(expected, queue, submission);
}

std::uint64_t TransactionalBackendRuntime::submitted(const GpuQueueKind queue) const
{
    return runtime_->submitted(queue);
}

std::size_t TransactionalBackendRuntime::unresolved_count() const noexcept
{
    return runtime_->unresolved_count();
}

std::size_t TransactionalBackendRuntime::collect()
{
    return runtime_->collect();
}

BackendGeneration TransactionalBackendRuntime::recover(const BackendGeneration old_generation,
                                                       const config::EngineConfig& next,
                                                       ReconstructionBuilder& builder,
                                                       SafeTeardownAuthority& authority)
{
    if (old_generation != generation())
    {
        throw std::invalid_argument{"wrong recovery generation"};
    }
    if (!device_lost())
    {
        throw std::logic_error{"cannot rebuild a running generation"};
    }
    if (old_generation.value == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error{"backend generation exhausted"};
    }
    if (!authority.confirmed_safe_to_destroy(old_generation))
    {
        throw std::logic_error{"safe teardown not confirmed"};
    }

    const BackendGeneration next_generation{old_generation.value + 1};
    // GREEN: prepare a full backend + both buffers in the coordinator BEFORE
    // the one-way commit of the new generation.
    auto staged_backend = checked_build(builder, next_generation);
    LeasedFactory staged_factory{staged_backend};
    VerifiedTeardown verified{old_generation};
    const auto committed = runtime_->recover(old_generation, next, staged_factory, verified);
    // shared_ptr::swap is noexcept. Buffered snapshot leases still keep their
    // corresponding old backend alive as long as any CPU reader holds them.
    active_backend_.swap(staged_backend);
    return committed;
}

} // namespace cpp_mastery::runtime
