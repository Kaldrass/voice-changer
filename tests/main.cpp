#include <iostream>

bool TestRingBufferBasic();
bool TestRingBufferOverflowAndDrop();
bool TestBiquadIdentity();
bool TestBiquadStability();
bool TestEQEffectFiniteAndChangesSignal();
bool TestPitchShiftEffectFinite();
bool TestVoiceConverterEffectFinite();

int main()
{
    int failed = 0;

    auto run = [&](const char* name, bool (*fn)()) {
        const bool ok = fn();
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << "\n";
        if (!ok) ++failed;
    };

    run("RingBufferBasic", &TestRingBufferBasic);
    run("RingBufferOverflowAndDrop", &TestRingBufferOverflowAndDrop);
    run("BiquadIdentity", &TestBiquadIdentity);
    run("BiquadStability", &TestBiquadStability);
    run("EQEffectFiniteAndChangesSignal", &TestEQEffectFiniteAndChangesSignal);
    run("PitchShiftEffectFinite", &TestPitchShiftEffectFinite);
    run("VoiceConverterEffectFinite", &TestVoiceConverterEffectFinite);

    if (failed != 0)
    {
        std::cout << "Tests failed: " << failed << "\n";
        return 1;
    }

    std::cout << "All tests passed.\n";
    return 0;
}
