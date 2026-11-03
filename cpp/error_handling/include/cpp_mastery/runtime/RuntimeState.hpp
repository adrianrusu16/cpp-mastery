#pragma once

#include <cpp_mastery/config/ReadEngineConfig.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace cpp_mastery::runtime
{

enum class BufferKind
{
    geometry,
    lighting,
};

// Owning resource handles are returned by the factory. The virtual interface lets
// tests inject deterministic allocation failures without a real GPU.
class RenderBuffer
{
  public:
    virtual ~RenderBuffer() = default;
    [[nodiscard]] virtual std::size_t size_bytes() const noexcept = 0;
};

class ResourceFactory
{
  public:
    virtual ~ResourceFactory() = default;
    [[nodiscard]] virtual std::unique_ptr<RenderBuffer> allocate(BufferKind kind,
                                                                 int render_distance) = 0;
};

// Concrete, CPU-backed reference factory for the lab. No GPU API is implied.
class CpuResourceFactory final : public ResourceFactory
{
  public:
    [[nodiscard]] std::unique_ptr<RenderBuffer> allocate(BufferKind kind,
                                                         int render_distance) override;
};

struct RuntimeSettings
{
    int worker_threads;
    int render_distance;
};

class RuntimeState final
{
  public:
    // Both initial resources must be prepared successfully or construction fails.
    RuntimeState(const config::EngineConfig& initial, ResourceFactory& factory);

    // Strong exception guarantee: on failure, the active settings and resources
    // are unchanged. Only staging is allowed to throw; commit is non-throwing.
    void apply(const config::EngineConfig& next, ResourceFactory& factory);

    [[nodiscard]] const RuntimeSettings& settings() const noexcept
    {
        return settings_;
    }
    [[nodiscard]] const RenderBuffer* geometry_buffer() const noexcept
    {
        return resources_.geometry.get();
    }
    [[nodiscard]] const RenderBuffer* lighting_buffer() const noexcept
    {
        return resources_.lighting.get();
    }

  private:
    struct ResourceSet
    {
        std::unique_ptr<RenderBuffer> geometry;
        std::unique_ptr<RenderBuffer> lighting;

        void swap(ResourceSet& other) noexcept;
    };

    [[nodiscard]] static ResourceSet prepare(const config::EngineConfig& next,
                                             ResourceFactory& factory);

    RuntimeSettings settings_;
    ResourceSet resources_;
};

} // namespace cpp_mastery::runtime
