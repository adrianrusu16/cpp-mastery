#include <cpp_mastery/runtime/FencedActivationReconciler.hpp>

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
        io_error("cannot open epoch ledger directory");
    }
    const int synced = ::fsync(dir);
    const int closed = ::close(dir);
    if (synced == -1 || closed == -1)
    {
        io_error("cannot fsync epoch ledger directory");
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

[[nodiscard]] std::uint64_t parse_number(const std::string_view field)
{
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(field.data(), field.data() + field.size(), value);
    if (field.empty() || error != std::errc{} || end != field.data() + field.size())
    {
        throw std::runtime_error{"corrupt epoch ledger numeric field"};
    }
    return value;
}

[[nodiscard]] std::string record(const AuthorityEpoch epoch)
{
    const auto body = "FE1|" + std::to_string(epoch.value);
    return body + '|' + std::to_string(checksum(body)) + '\n';
}

[[nodiscard]] AuthorityEpoch parse_line(const std::string_view line)
{
    const auto first = line.find('|');
    const auto second = first == std::string_view::npos ? first : line.find('|', first + 1);
    if (first == std::string_view::npos || second == std::string_view::npos ||
        line.substr(0, first) != "FE1")
    {
        throw std::runtime_error{"invalid epoch ledger record"};
    }
    const auto body = line.substr(0, second);
    const auto supplied = parse_number(line.substr(second + 1));
    if (supplied > UINT32_MAX || supplied != checksum(body))
    {
        throw std::runtime_error{"epoch ledger checksum mismatch"};
    }
    const auto value = parse_number(line.substr(first + 1, second - first - 1));
    if (value == 0)
    {
        throw std::runtime_error{"epoch ledger contains zero epoch"};
    }
    return {value};
}

[[nodiscard]] std::string read_all(const int fd)
{
    if (::lseek(fd, 0, SEEK_SET) == -1)
    {
        io_error("cannot seek epoch ledger");
    }
    std::string data;
    std::array<char, 4096> bytes{};
    for (;;)
    {
        const auto count = ::read(fd, bytes.data(), bytes.size());
        if (count == 0)
        {
            return data;
        }
        if (count == -1 && errno == EINTR)
        {
            continue;
        }
        if (count == -1)
        {
            io_error("cannot read epoch ledger");
        }
        if (data.size() > 1'000'000U - static_cast<std::size_t>(count))
        {
            throw std::runtime_error{"epoch ledger exceeds teaching size limit"};
        }
        data.append(bytes.data(), static_cast<std::size_t>(count));
    }
}

} // namespace

AuthorityEpochLedger::AuthorityEpochLedger(const std::filesystem::path& path, EpochWriteHook* hook)
    : hook_{hook}
{
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd_ == -1 && errno == EEXIST)
    {
        fd_ = ::open(path.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    }
    if (fd_ == -1)
    {
        io_error("cannot open epoch ledger");
    }
    try
    {
        struct stat state{};
        if (::fstat(fd_, &state) == -1 || !S_ISREG(state.st_mode))
        {
            io_error("epoch ledger must be a regular file");
        }
        if (::flock(fd_, LOCK_EX | LOCK_NB) == -1)
        {
            io_error("epoch ledger already has a writer");
        }
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

AuthorityEpochLedger::~AuthorityEpochLedger()
{
    if (fd_ != -1)
    {
        static_cast<void>(::close(fd_));
    }
}

AuthorityEpoch AuthorityEpochLedger::current() const noexcept
{
    return current_;
}

void AuthorityEpochLedger::load()
{
    const auto data = read_all(fd_);
    std::size_t pos = 0;
    while (pos < data.size())
    {
        const auto end = data.find('\n', pos);
        if (end == std::string::npos)
        {
            throw std::runtime_error{"torn epoch ledger record"};
        }
        const auto parsed = parse_line(std::string_view{data}.substr(pos, end - pos));
        if (parsed.value <= current_.value)
        {
            throw std::runtime_error{"epoch ledger is not monotonically increasing"};
        }
        current_ = parsed;
        pos = end + 1;
    }
}

void AuthorityEpochLedger::append(const AuthorityEpoch next)
{
    if (hook_)
    {
        hook_->before_append(next);
    }
    const auto bytes = record(next);
    if (::lseek(fd_, 0, SEEK_END) == -1)
    {
        poisoned_ = true;
        io_error("cannot seek to epoch ledger end");
    }
    std::size_t cursor = 0;
    while (cursor < bytes.size())
    {
        const auto count = ::write(fd_, bytes.data() + cursor, bytes.size() - cursor);
        if (count == -1 && errno == EINTR)
        {
            continue;
        }
        if (count <= 0)
        {
            poisoned_ = true;
            io_error("cannot write epoch ledger");
        }
        cursor += static_cast<std::size_t>(count);
    }
    if (::fsync(fd_) == -1)
    {
        poisoned_ = true;
        io_error("cannot fsync epoch ledger");
    }
}

void AuthorityEpochLedger::advance(const AuthorityEpoch next)
{
    if (poisoned_)
    {
        throw std::logic_error{"epoch ledger I/O state uncertain; reopen and inspect"};
    }
    if (next.value == 0 || next.value <= current_.value)
    {
        throw std::invalid_argument{"authority epoch must advance monotonically"};
    }
    append(next);
    current_ = next;
}

FencedActivationReconciler::FencedActivationReconciler(DurableActivationJournal& journal,
                                                       AuthorityEpochLedger& epochs) noexcept
    : journal_{journal}, epochs_{epochs}
{
}

FencedReconciliationResult FencedActivationReconciler::reconcile(const ActivationAttemptId id,
                                                                 FencedStatusAuthority& authority)
{
    const auto recorded = journal_.lookup(id);
    if (recorded.state != JournalActivationState::pending)
    {
        return {recorded, false, false};
    }

    const auto queried_epoch = epochs_.current();
    if (queried_epoch.value == 0)
    {
        throw std::logic_error{"cannot reconcile without a durable authority epoch"};
    }

    FencedStatusReply reply{};
    try
    {
        reply = authority.query(id, recorded.requested_generation, queried_epoch);
    }
    catch (...)
    {
        // Query failures are not evidence that the backend did not start.
        return {recorded, true, false};
    }

    if (reply.id != id || reply.generation != recorded.requested_generation)
    {
        throw std::invalid_argument{"status reply identity does not match durable intent"};
    }
    if (reply.epoch.value > queried_epoch.value)
    {
        // A reply cannot grant itself authority to advance the fence.
        throw std::invalid_argument{"status reply claims an unauthorized future epoch"};
    }
    if (reply.epoch != queried_epoch || epochs_.current() != queried_epoch)
    {
        // GREEN: both old-owner replies and ownership changes during the
        // callback leave the journal intact, including negative evidence.
        return {recorded, true, true};
    }

    switch (reply.status)
    {
    case AuthoritativeActivationStatus::unavailable:
        return {recorded, true, false};
    case AuthoritativeActivationStatus::started:
        return {journal_.confirm(id, ActivationResolution::confirmed_started), true, false};
    case AuthoritativeActivationStatus::not_started:
        return {journal_.confirm(id, ActivationResolution::confirmed_not_started), true, false};
    }
    throw std::invalid_argument{"invalid fenced authority status"};
}

} // namespace cpp_mastery::runtime
