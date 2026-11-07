#pragma once

#include <cpp_mastery/runtime/DurableActivationJournal.hpp>
#include <cpp_mastery/runtime/FencedBackendHandoff.hpp>

namespace cpp_mastery::runtime
{

// Composes the durable attempt ID with the fenced backend admission boundary.
// A request is journaled before the trusted backend may perform an effect.
// Replaying ANY existing ID is read-only; an uncertain outcome needs
// independent authoritative reconciliation, never blind re-dispatch.
//
// Single-controller CPU-only model. The journal and gate must be used only
// by this controller while a dispatch is in progress. Does not provide
// cross-process atomicity, GPU exactly-once guarantees or payload deduplication
// for a sink that bypasses the gate.
class FencedActivationDispatcher final
{
  public:
    FencedActivationDispatcher(DurableActivationJournal& journal, FencedBackendGate& gate) noexcept;

    [[nodiscard]] JournalActivationResult dispatch(ActivationAttemptId id,
                                                   BackendGeneration generation,
                                                   const config::EngineConfig& settings,
                                                   AuthorityEpoch owner, BackendCommandSink& sink);

  private:
    DurableActivationJournal& journal_;
    FencedBackendGate& gate_;
};

} // namespace cpp_mastery::runtime
