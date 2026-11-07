#include <cpp_mastery/runtime/FencedActivationDispatch.hpp>

#include <stdexcept>

namespace cpp_mastery::runtime
{

namespace
{
class DispatchAdapter final : public JournalActivationBackend
{
  public:
    DispatchAdapter(FencedBackendGate& gate, const AuthorityEpoch owner,
                    BackendCommandSink& sink) noexcept
        : gate_{gate}, owner_{owner}, sink_{sink}
    {
    }

    [[nodiscard]] ActivationOutcome activate(const ActivationAttemptId id, BackendGeneration,
                                             const config::EngineConfig&) override
    {
        // This adapter is invoked only after begin_or_replay has fsync'd intent.
        switch (gate_.submit({owner_, id.value}, sink_))
        {
        case BackendAdmission::executed:
            return ActivationOutcome::started;
        case BackendAdmission::rejected_stale:
            // The trusted backend gate proves this attempt produced no effect.
            return ActivationOutcome::not_started;
        case BackendAdmission::rejected_quarantined:
            // Handoff durability is uncertain; preserve the intent pending.
            return ActivationOutcome::unknown;
        }
        throw std::logic_error{"unknown backend admission state"};
    }

  private:
    FencedBackendGate& gate_;
    AuthorityEpoch owner_;
    BackendCommandSink& sink_;
};
} // namespace

FencedActivationDispatcher::FencedActivationDispatcher(DurableActivationJournal& journal,
                                                       FencedBackendGate& gate) noexcept
    : journal_{journal}, gate_{gate}
{
}

JournalActivationResult FencedActivationDispatcher::dispatch(const ActivationAttemptId id,
                                                             const BackendGeneration generation,
                                                             const config::EngineConfig& settings,
                                                             const AuthorityEpoch owner,
                                                             BackendCommandSink& sink)
{
    DispatchAdapter adapter{gate_, owner, sink};
    // GREEN: begin_or_replay owns identity validation, intent fsync, and
    // idempotent replay. The backend adapter is called only on a NEW ID.
    // An ambiguous execution stays pending until independent reconciliation.
    return journal_.begin_or_replay(id, generation, settings, adapter);
}

} // namespace cpp_mastery::runtime
