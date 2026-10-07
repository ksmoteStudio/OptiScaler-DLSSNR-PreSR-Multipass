// TDD RED — DLSS-NR runtime status, as the overlay menu should show it.
//
// This test deliberately includes the SHIPPED header rather than re-implementing
// the logic in a mock. Most tests under tests/ do the latter (nr_status_reporting
// .cpp re-declares MockConfig/MockModel/MockNrState and re-implements Prepare()),
// which means a green suite there proves nothing about the code that ships.
// This file exists so at least this behaviour is verified against the real thing.
//
// The defect this pins down, from 2026-10-07: the overlay menu rendered
// Config::DlssNrEnabled, so it showed "On" while NR dispatched nothing at all.
// 30,042 consecutive "pre-SR input dispatch failed in pipeline" warnings, and a
// menu that insisted neural rendering was running. A user reading the menu could
// not tell the difference. Test 2 below is that exact case.

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

#include <dlssnr/DlssNr_RuntimeStatus.h>

using DlssNr::RuntimeStatus;

namespace
{
    int g_failed = 0;

    void Check(bool condition, const char* what)
    {
        if (condition)
        {
            std::cout << "[PASS] " << what << std::endl;
        }
        else
        {
            std::cout << "[FAIL] " << what << std::endl;
            ++g_failed;
        }
    }

    bool Is(const char* a, const char* b) { return std::strcmp(a, b) == 0; }
}

int main()
{
    // Test 1: nothing configured yet is Off.
    {
        RuntimeStatus status;
        Check(status.Current() == RuntimeStatus::State::Off, "1 fresh status is Off");
        Check(Is(status.DisplayName(), "Off"), "1 fresh status displays Off");
    }

    // Test 2: THE BUG. Being configured on is not the same as running.
    // A menu that reported the config flag alone would claim "On" here.
    {
        RuntimeStatus status;
        status.Configure(true);
        Check(status.Current() == RuntimeStatus::State::Off,
              "2 configured on but never dispatched stays Off");
        Check(Is(status.DisplayName(), "Off"),
              "2 configured on but never dispatched does not display On");
    }

    // Test 3: configured on and actually dispatching is Active.
    {
        RuntimeStatus status;
        status.Configure(true);
        status.MarkDispatched();
        Check(status.Current() == RuntimeStatus::State::Active, "3 dispatch makes it Active");
        Check(Is(status.DisplayName(), "Active"), "3 Active displays Active");
    }

    // Test 4: configured on but the pipeline refused is Stalled, with the reason.
    {
        RuntimeStatus status;
        status.Configure(true);
        status.MarkSkipped("pre-SR input dispatch failed in pipeline");
        Check(status.Current() == RuntimeStatus::State::Stalled, "4 skip makes it Stalled");
        Check(Is(status.DisplayName(), "Stalled"), "4 Stalled displays Stalled");
        Check(Is(status.Reason(), "pre-SR input dispatch failed in pipeline"),
              "4 Stalled exposes the pipeline's reason");
    }

    // Test 5: a later success clears the stale stall. Reasons must not stick.
    {
        RuntimeStatus status;
        status.Configure(true);
        status.MarkSkipped("the depth or motion vectors could not be made readable this frame");
        status.MarkDispatched();
        Check(status.Current() == RuntimeStatus::State::Active, "5 recovery returns to Active");
        Check(Is(status.Reason(), ""), "5 recovery clears the stale reason");
    }

    // Test 6: turning it off is clean -- Off, and no leftover reason.
    {
        RuntimeStatus status;
        status.Configure(true);
        status.MarkDispatched();
        status.MarkSkipped("something went wrong");
        status.Configure(false);
        Check(status.Current() == RuntimeStatus::State::Off, "6 disabling returns to Off");
        Check(Is(status.DisplayName(), "Off"), "6 disabled displays Off");
        Check(Is(status.Reason(), ""), "6 disabling clears the reason");
    }

    // Test 7: toggling off and on again must not resurrect a stale reason, and
    // must not report Active until a dispatch is actually seen.
    {
        RuntimeStatus status;
        status.Configure(true);
        status.MarkSkipped("old failure from a previous session");
        status.Configure(false);
        status.Configure(true);
        Check(status.Current() == RuntimeStatus::State::Off, "7 re-enabling starts Off again");
        Check(Is(status.Reason(), ""), "7 re-enabling does not resurrect the old reason");
    }

    std::cout << std::endl;
    if (g_failed == 0)
    {
        std::cout << "ALL 7 RUNTIME STATUS TESTS PASSED." << std::endl;
        return 0;
    }

    std::cout << g_failed << " of 7 runtime status tests FAILED." << std::endl;
    return 1;
}