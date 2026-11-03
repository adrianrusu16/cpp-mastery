#include <cpp_mastery/runtime/RuntimeState.hpp>

#include <stdexcept>
#include <utility>

namespace cpp_mastery::runtime
{

namespace
{

class CpuBuffer final : public RenderBuffer
{
  public:
    explicit CpuBuffer(const std::size_t count) : storage_(count) {}

    [[nodiscard]] std::size_t size_bytes() const noexcept override
    {
        return storage_.size();
    }

  private:
    std::vector<std::byte> storage_;
};

} // namespace

std::unique_ptr<RenderBuffer> CpuResourceFactory::allocate(const BufferKind kind,
                                                           const int render_distance)
{
    if (render_distance < 1 || render_distance > 32)
    {
        throw std::invalid_argument{"render_distance must be in [1, 32]"};
    }

    const std::size_t multiplier = kind == BufferKind::geometry ? 1024U : 512U;
    return std::make_unique<CpuBuffer>(static_cast<std::size_t>(render_distance) * multiplier);
}

void RuntimeState::ResourceSet::swap(ResourceSet& other) noexcept
{
    geometry.swap(other.geometry);
    lighting.swap(other.lighting);
}

RuntimeState::ResourceSet RuntimeState::prepare(const config::EngineConfig& next,
                                                ResourceFactory& factory)
{
    ResourceSet staged;
    staged.geometry = factory.allocate(BufferKind::geometry, next.render_distance);
    if (!staged.geometry)
    {
        throw std::runtime_error{"resource factory returned null geometry buffer"};
    }

    staged.lighting = factory.allocate(BufferKind::lighting, next.render_distance);
    if (!staged.lighting)
    {
        throw std::runtime_error{"resource factory returned null lighting buffer"};
    }

    return staged;
}

RuntimeState::RuntimeState(const config::EngineConfig& initial, ResourceFactory& factory)
    : settings_{initial.worker_threads, initial.render_distance},
      resources_{prepare(initial, factory)}
{
}

void RuntimeState::apply(const config::EngineConfig& next, ResourceFactory& factory)
{
    // Stage all fallible work without touching the active resources or settings.
    ResourceSet staged = prepare(next, factory);

    // This commit cannot throw: unique_ptr swap and two trivial integer assignments.
    // After the swap, staged owns the old resources and releases them on return.
    resources_.swap(staged);
    settings_ = RuntimeSettings{next.worker_threads, next.render_distance};
}

} // namespace cpp_mastery::runtime
