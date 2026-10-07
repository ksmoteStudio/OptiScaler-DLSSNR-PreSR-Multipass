#pragma once

// DLSS-NR runtime status: what the NR pass is ACTUALLY doing, as opposed to
// what the config asked for.
//
// Added 2026-10-07 after a session where the overlay menu showed NR as "On"
// while the log filled with 30,042 consecutive "pre-SR input dispatch failed in
// pipeline" warnings. The menu rendered Config::DlssNrEnabled, which answers
// "was NR requested", not "did NR run". Those are different questions and only
// one of them was being asked on screen.
//
// Deliberate constraints, because tests/nr_runtime_status_unit.cpp compiles
// this header standalone with cl.exe:
//   - no pch.h, no D3D12, no spdlog, no OptiScaler headers
//   - header-only, so nothing has to be linked in
// The point is that the test exercises THIS file, not a mock that happens to
// resemble it.
//
// Threading: MarkDispatched/MarkSkipped run on the render thread; Current and
// DisplayName are read from the overlay menu thread. The tri-state is atomic;
// the reason string is guarded by its own mutex.

#include <atomic>
#include <cstring>
#include <mutex>

namespace DlssNr
{

    class RuntimeStatus
    {
      public:
        enum class State
        {
            Off = 0,    ///< Not configured, or configured off.
            Active = 1, ///< Configured on AND a dispatch has actually been observed.
            Stalled = 2 ///< Configured on, but the pipeline refused; Reason() says why.
        };

        // Mirrors Config::DlssNrEnabled. Calling it repeatedly with the same value
        // must not clear a live Active/Stalled observation, so only an actual
        // off->on or on->off transition resets the state.
        void Configure(bool enabled)
        {
            const bool wasEnabled = _configured.load(std::memory_order_acquire);
            if (wasEnabled == enabled)
            {
                return;
            }
            _configured.store(enabled, std::memory_order_release);
            _state.store(State::Off, std::memory_order_release);
            std::lock_guard<std::mutex> guard(_reasonMutex);
            _reason[0] = '\0';
        }

        // The NR pass really dispatched. This is the only thing that promotes
        // the state to Active -- being configured is deliberately not enough.
        void MarkDispatched()
        {
            if (!_configured.load(std::memory_order_acquire))
            {
                return;
            }
            std::lock_guard<std::mutex> guard(_reasonMutex);
            _reason[0] = '\0';
            _state.store(State::Active, std::memory_order_release);
        }

        // The pipeline refused. Keeps the reason so the menu can show it instead
        // of the user having to go find it in the log.
        void MarkSkipped(const char* reason)
        {
            if (!_configured.load(std::memory_order_acquire))
            {
                return;
            }
            {
                std::lock_guard<std::mutex> guard(_reasonMutex);
                CopyReason(reason);
            }
            _state.store(State::Stalled, std::memory_order_release);
        }

        State Current() const { return _state.load(std::memory_order_acquire); }

        const char* DisplayName() const
        {
            switch (Current())
            {
            case State::Active:
                return "Active";
            case State::Stalled:
                return "Stalled";
            case State::Off:
            default:
                return "Off";
            }
        }

        // Points at an internal buffer, valid until the next Mark* call from any
        // thread. Menu code renders it immediately; do not stash the pointer.
        const char* Reason() const
        {
            std::lock_guard<std::mutex> guard(_reasonMutex);
            return _reason;
        }

      private:
        void CopyReason(const char* reason)
        {
            if (reason == nullptr)
            {
                _reason[0] = '\0';
                return;
            }
            std::strncpy(_reason, reason, sizeof(_reason) - 1);
            _reason[sizeof(_reason) - 1] = '\0';
        }

        std::atomic<State> _state { State::Off };
        std::atomic<bool> _configured { false };
        mutable std::mutex _reasonMutex;
        char _reason[192] { '\0' };
    };

    // One process-wide instance: the menu has no handle on the NR owner object.
    inline RuntimeStatus& GetRuntimeStatus()
    {
        static RuntimeStatus instance {};
        return instance;
    }

} // namespace DlssNr