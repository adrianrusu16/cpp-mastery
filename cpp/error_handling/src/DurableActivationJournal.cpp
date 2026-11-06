#include <cpp_mastery/runtime/DurableActivationJournal.hpp>

#include <array>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace cpp_mastery::runtime
{
namespace
{

[[noreturn]] void io_error(const char* message)
{
    throw std::runtime_error{message};
}

void sync_directory(const std::filesystem::path& path)
{
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path{"."};
    const int dir = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir == -1)
    {
        io_error("cannot open journal directory for fsync");
    }
    const int sync_result = ::fsync(dir);
    const int close_result = ::close(dir);
    if (sync_result == -1 || close_result == -1)
    {
        io_error("cannot fsync journal directory");
    }
}

[[nodiscard]] std::uint32_t checksum(const std::string_view bytes) noexcept
{
    std::uint32_t hash = 2166136261U;
    for (const char character : bytes)
    {
        hash ^= static_cast<unsigned char>(character);
        hash *= 16777619U;
    }
    return hash;
}

template <typename T> [[nodiscard]] T number(const std::string_view field)
{
    T value{};
    const auto [end, error] = std::from_chars(field.data(), field.data() + field.size(), value);
    if (field.empty() || error != std::errc{} || end != field.data() + field.size())
    {
        throw std::runtime_error{"corrupt activation journal numeric field"};
    }
    return value;
}

[[nodiscard]] std::string record(const char kind, const ActivationAttemptId id,
                                 const BackendGeneration generation,
                                 const config::EngineConfig& config)
{
    std::string body = "AJ1|";
    body += kind;
    body += '|';
    body += std::to_string(id.value);
    body += '|';
    body += std::to_string(generation.value);
    body += '|';
    body += std::to_string(config.worker_threads);
    body += '|';
    body += std::to_string(config.render_distance);
    return body + '|' + std::to_string(checksum(body)) + '\n';
}

struct ParsedRecord
{
    char kind;
    ActivationAttemptId id;
    BackendGeneration generation;
    config::EngineConfig config;
};

[[nodiscard]] ParsedRecord parse_line(const std::string_view line)
{
    std::array<std::string_view, 7> fields{};
    std::size_t begin = 0;
    for (std::size_t index = 0; index < fields.size() - 1; ++index)
    {
        const auto next = line.find('|', begin);
        if (next == std::string_view::npos)
        {
            throw std::runtime_error{"corrupt activation journal record"};
        }
        fields[index] = line.substr(begin, next - begin);
        begin = next + 1;
    }
    fields.back() = line.substr(begin);
    if (fields[0] != "AJ1" || fields[1].size() != 1 ||
        (fields[1] != "I" && fields[1] != "S" && fields[1] != "N"))
    {
        throw std::runtime_error{"invalid activation journal record version or kind"};
    }
    const auto actual = number<std::uint32_t>(fields[6]);
    const auto body = line.substr(0, begin - 1);
    if (checksum(body) != actual)
    {
        throw std::runtime_error{"activation journal checksum mismatch"};
    }
    ParsedRecord parsed{fields[1][0],
                        {number<std::uint64_t>(fields[2])},
                        {number<std::uint64_t>(fields[3])},
                        {number<int>(fields[4]), number<int>(fields[5])}};
    if (parsed.id.value == 0 || parsed.generation.value == 0 || parsed.config.worker_threads <= 0 ||
        parsed.config.render_distance <= 0)
    {
        throw std::runtime_error{"invalid activation journal identity or configuration"};
    }
    return parsed;
}

[[nodiscard]] std::string read_all(const int fd)
{
    if (::lseek(fd, 0, SEEK_SET) == -1)
    {
        io_error("cannot seek activation journal");
    }
    std::string data;
    std::array<char, 4096> buffer{};
    for (;;)
    {
        const auto count = ::read(fd, buffer.data(), buffer.size());
        if (count == 0)
        {
            break;
        }
        if (count == -1 && errno == EINTR)
        {
            continue;
        }
        if (count == -1)
        {
            io_error("cannot read activation journal");
        }
        if (data.size() > 4'000'000U - static_cast<std::size_t>(count))
        {
            throw std::runtime_error{"activation journal exceeds teaching size limit"};
        }
        data.append(buffer.data(), static_cast<std::size_t>(count));
    }
    return data;
}

} // namespace

DurableActivationJournal::DurableActivationJournal(const std::filesystem::path& path,
                                                   JournalWriteHook* hook)
    : hook_{hook}
{
    const auto name = path.string();
    fd_ = ::open(name.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    const bool created = fd_ != -1;
    if (!created && errno == EEXIST)
    {
        fd_ = ::open(name.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    }
    if (fd_ == -1)
    {
        io_error("cannot open activation journal");
    }
    try
    {
        struct stat state{};
        if (::fstat(fd_, &state) == -1 || !S_ISREG(state.st_mode))
        {
            io_error("activation journal must be a regular file");
        }
        if (::flock(fd_, LOCK_EX | LOCK_NB) == -1)
        {
            io_error("activation journal already has a writer");
        }
        // Always re-sync the directory name before dispatching work, including
        // when an earlier creation failed before its directory fsync completed.
        sync_directory(path);
        load();
    }
    catch (...)
    {
        static_cast<void>(::close(fd_));
        fd_ = -1;
        throw;
    }
}

DurableActivationJournal::~DurableActivationJournal()
{
    if (fd_ != -1)
    {
        static_cast<void>(::close(fd_));
    }
}

void DurableActivationJournal::validate(const ActivationAttemptId id,
                                        const BackendGeneration generation)
{
    if (id.value == 0 || generation.value == 0)
    {
        throw std::invalid_argument{"activation identity and generation must be nonzero"};
    }
}

JournalActivationResult DurableActivationJournal::result(const ActivationAttemptId id,
                                                         const Entry& entry) noexcept
{
    return {id, entry.generation, entry.state};
}

bool DurableActivationJournal::has_pending() const noexcept
{
    for (const auto& [id, entry] : entries_)
    {
        static_cast<void>(id);
        if (entry.state == JournalActivationState::pending)
        {
            return true;
        }
    }
    return false;
}

void DurableActivationJournal::load()
{
    const std::string bytes = read_all(fd_);
    if (!bytes.empty() && bytes.back() != '\n')
    {
        throw std::runtime_error{"torn final activation journal record"};
    }
    std::size_t from = 0;
    while (from < bytes.size())
    {
        const auto until = bytes.find('\n', from);
        const auto parsed = parse_line(std::string_view{bytes}.substr(from, until - from));
        const auto found = entries_.find(parsed.id.value);
        if (parsed.kind == 'I')
        {
            if (found != entries_.end())
            {
                throw std::runtime_error{"duplicate activation journal intent"};
            }
            entries_.emplace(parsed.id.value, Entry{parsed.generation, parsed.config,
                                                    JournalActivationState::pending});
        }
        else
        {
            if (found == entries_.end() || found->second.state != JournalActivationState::pending ||
                found->second.generation != parsed.generation ||
                found->second.config.worker_threads != parsed.config.worker_threads ||
                found->second.config.render_distance != parsed.config.render_distance)
            {
                throw std::runtime_error{"invalid activation journal terminal transition"};
            }
            found->second.state = parsed.kind == 'S' ? JournalActivationState::started
                                                     : JournalActivationState::not_started;
        }
        from = until + 1;
    }
}

void DurableActivationJournal::append(const char kind, const ActivationAttemptId id,
                                      const Entry& entry)
{
    if (poisoned_)
    {
        throw std::logic_error{"journal I/O failed; reopen and reconcile before continuing"};
    }
    if (hook_)
    {
        hook_->before_append(kind);
    }
    const auto bytes = record(kind, id, entry.generation, entry.config);
    std::size_t written = 0;
    while (written < bytes.size())
    {
        const auto n = ::write(fd_, bytes.data() + written, bytes.size() - written);
        if (n == -1 && errno == EINTR)
        {
            continue;
        }
        if (n <= 0)
        {
            poisoned_ = true;
            io_error("activation journal write failed");
        }
        written += static_cast<std::size_t>(n);
    }
    if (::fsync(fd_) == -1)
    {
        poisoned_ = true;
        io_error("activation journal fsync failed");
    }
}

void DurableActivationJournal::finish(const ActivationAttemptId id, Entry& entry,
                                      const JournalActivationState state)
{
    const char kind = state == JournalActivationState::started ? 'S' : 'N';
    append(kind, id, entry);
    entry.state = state;
}

JournalActivationResult DurableActivationJournal::begin_or_replay(
    const ActivationAttemptId id, const BackendGeneration generation,
    const config::EngineConfig& next, JournalActivationBackend& backend)
{
    validate(id, generation);
    if (next.worker_threads <= 0 || next.render_distance <= 0)
    {
        throw std::invalid_argument{"activation configuration must be positive"};
    }
    if (poisoned_)
    {
        throw std::logic_error{"activation journal needs recovery"};
    }
    const auto existing = entries_.find(id.value);
    if (existing != entries_.end())
    {
        const auto& entry = existing->second;
        if (entry.generation != generation || entry.config.worker_threads != next.worker_threads ||
            entry.config.render_distance != next.render_distance)
        {
            throw std::invalid_argument{"activation ID reused with conflicting inputs"};
        }
        // GREEN: a replay never contacts the external backend, even if pending.
        return result(id, entry);
    }
    if (has_pending())
    {
        throw std::logic_error{"reconcile the pending activation before starting another"};
    }

    auto [it, inserted] = entries_.emplace(id.value, Entry{generation, next});
    static_cast<void>(inserted);
    try
    {
        // GREEN: the durable intent MUST precede the external side effect.
        append('I', id, it->second);
    }
    catch (...)
    {
        entries_.erase(it);
        throw;
    }

    ActivationOutcome outcome = ActivationOutcome::unknown;
    try
    {
        outcome = backend.activate(id, generation, next);
    }
    catch (...)
    {
        // No terminal result can be inferred from a throwing backend.
    }
    if (outcome == ActivationOutcome::started)
    {
        finish(id, it->second, JournalActivationState::started);
    }
    else if (outcome == ActivationOutcome::not_started)
    {
        finish(id, it->second, JournalActivationState::not_started);
    }
    return result(id, it->second);
}

JournalActivationResult DurableActivationJournal::confirm(const ActivationAttemptId id,
                                                          const ActivationResolution resolution)
{
    if (poisoned_)
    {
        throw std::logic_error{"activation journal needs recovery"};
    }
    if (resolution != ActivationResolution::confirmed_started &&
        resolution != ActivationResolution::confirmed_not_started)
    {
        throw std::invalid_argument{"invalid activation resolution"};
    }
    const auto found = entries_.find(id.value);
    if (found == entries_.end())
    {
        throw std::invalid_argument{"unknown activation ID"};
    }
    const auto expected = resolution == ActivationResolution::confirmed_started
                              ? JournalActivationState::started
                              : JournalActivationState::not_started;
    if (found->second.state != JournalActivationState::pending)
    {
        if (found->second.state != expected)
        {
            throw std::logic_error{"contradictory activation confirmation"};
        }
        return result(id, found->second);
    }
    finish(id, found->second, expected);
    return result(id, found->second);
}

JournalActivationResult DurableActivationJournal::lookup(const ActivationAttemptId id) const
{
    const auto found = entries_.find(id.value);
    if (found == entries_.end())
    {
        throw std::invalid_argument{"unknown activation ID"};
    }
    return result(id, found->second);
}

std::size_t DurableActivationJournal::size() const noexcept
{
    return entries_.size();
}

} // namespace cpp_mastery::runtime
