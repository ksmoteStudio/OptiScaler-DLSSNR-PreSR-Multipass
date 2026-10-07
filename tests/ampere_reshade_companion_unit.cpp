// TDD: MergeReshadeCompanionContent is the function that regenerates
// [DLSSG-SM86-75-COMPANION] inside ReShade.ini on every launch.
//
// This test includes the REAL header rather than a mock. That is possible
// because AmpereMfgLoader.h only pulls in <cstdint> <filesystem> <fstream>
// <sstream> <string> -- no pch.h, no D3D12, no Config.h. Most tests in this repo
// re-implement the logic in a mock struct, which means a green suite proves
// nothing about the shipped code. This one exercises the shipped code.
//
// Why this matters: until 2026-10-07 both call sites in AmpereMfgLoader.cpp
// passed only four arguments, so uiRecomposition silently took its default of 1
// and UIRecomposition could never be set to 0 from configuration at all. These
// tests pin the contract so the wiring cannot quietly break again.

#include <cassert>
#include <iostream>
#include <string>

#include <framegen/dlssg/AmpereMfgLoader.h>

using AmpereMfgLoader::MergeReshadeCompanionContent;

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

    bool Has(const std::string& haystack, const std::string& needle)
    {
        return haystack.find(needle) != std::string::npos;
    }
}

int main()
{
    // Test 1: omitting uiRecomposition keeps today's behaviour (Auto = 1).
    {
        std::string out = MergeReshadeCompanionContent("", false, 0.0f, 0);
        Check(Has(out, "UIRecomposition=1"), "1 omitted uiRecomposition defaults to 1 (unchanged behaviour)");
    }

    // Test 2: 0 can now actually be written -- this is what was impossible before.
    {
        std::string out = MergeReshadeCompanionContent("", false, 0.0f, 0, 0);
        Check(Has(out, "UIRecomposition=0"), "2 uiRecomposition=0 is honoured");
    }

    // Test 3: 2 (force on) also round-trips.
    {
        std::string out = MergeReshadeCompanionContent("", false, 0.0f, 0, 2);
        Check(Has(out, "UIRecomposition=2"), "3 uiRecomposition=2 is honoured");
    }

    // Test 4: out-of-range values fall back to Auto rather than writing junk.
    {
        std::string out = MergeReshadeCompanionContent("", false, 0.0f, 0, 7);
        Check(Has(out, "UIRecomposition=1"), "4 out-of-range uiRecomposition falls back to 1");
    }

    // Test 5: the other three keys keep their existing derivation, unchanged by
    // the new parameter. Guards against the new argument shifting positions.
    {
        std::string out = MergeReshadeCompanionContent("", true, 75.0f, 3, 0);
        Check(Has(out, "Dynamic=1"), "5 dynamicMfg=true still writes Dynamic=1");
        Check(Has(out, "TargetFPS=75"), "5 dynamicTargetFps still writes TargetFPS=75");
        Check(Has(out, "Multiplier=0"), "5 dynamic mode forces Multiplier back to 0");
    }

    // Test 6: a fixed multiplier in the non-dynamic path is preserved.
    {
        std::string out = MergeReshadeCompanionContent("", false, 0.0f, 3, 1);
        Check(Has(out, "Multiplier=3"), "6 fixed multiplier survives when dynamic is off");
    }

    // Test 7: existing content outside the companion section must survive a
    // rewrite. Losing the user's own ReShade.ini settings would be a regression.
    {
        std::string existing = "[General]\nMySetting=42\n\n[DLSSG-SM86-75-COMPANION]\nStale=1\n\n[Other]\nKeep=me\n";
        std::string out = MergeReshadeCompanionContent(existing, false, 0.0f, 0, 0);
        Check(Has(out, "MySetting=42"), "7 content before the companion section is preserved");
        Check(Has(out, "Keep=me"), "7 content after the companion section is preserved");
        Check(!Has(out, "Stale=1"), "7 stale key inside the companion section is dropped");
        Check(Has(out, "UIRecomposition=0"), "7 companion section still rewritten with the new value");
    }

    // Test 8: exactly one companion section survives a rewrite, otherwise a
    // second startup would append a duplicate block.
    {
        std::string once = MergeReshadeCompanionContent("", false, 0.0f, 0, 0);
        std::string twice = MergeReshadeCompanionContent(once, false, 0.0f, 0, 0);
        int n = 0;
        for (size_t p = twice.find("[DLSSG-SM86-75-COMPANION]"); p != std::string::npos;
             p = twice.find("[DLSSG-SM86-75-COMPANION]", p + 1))
        {
            ++n;
        }
        Check(n == 1, "8 rewriting an already-rewritten file does not duplicate the section");
    }

    std::cout << std::endl;
    if (g_failed == 0)
    {
        std::cout << "ALL 8 AMPERE COMPANION TESTS PASSED." << std::endl;
        return 0;
    }

    std::cout << g_failed << " of 8 ampere companion tests FAILED." << std::endl;
    return 1;
}