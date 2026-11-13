// Runs FrameWait against a model of the single player frame gate
// (SrvProcessAllGames, 0x52fc20) on a simulated clock and checks the frame rate
// reached and the share of the time spent asleep.
#include "headers/frame_wait.h"
#include <cassert>
#include <cstdio>

namespace {
struct Result { int frames; double asleep; int idlePasses; };

// period: the gate's milliseconds per frame. work: what a frame costs, in
// microseconds. late: how far a sleep overshoots, in microseconds.
Result Run(int period, int work, int late, bool stepping = true, bool singlePlayer = true, int seconds = 20) {
    const long long end = 1000000LL * seconds;
    long long clock = 5000000; // microseconds; the game's clock is this in milliseconds
    long long slept = 0;
    int last = 0, seen = 0, frames = 0, idle = 0;
    while (clock - 5000000 < end) {
        const int now = int(clock / 1000);
        bool ran = false;
        if (stepping) {
            if (!last) last = now;
            if (now - last >= period) {
                // The gate carries what it overshot into the next frame, a frame at most.
                int over = now - last - period;
                if (over >= period) over = period;
                last = now - over;
                clock += work; ++frames; ran = true;
            }
        }
        const unsigned wait = FrameWait::Milliseconds(singlePlayer, period, last, int(clock / 1000), last != seen);
        seen = last;
        if (wait) { clock += 1000LL * wait + late; slept += 1000LL * wait + late; }
        else clock += 20; // a pass of the loop that neither ran a frame nor slept
        if (!ran) ++idle;
    }
    return {frames, double(slept) / double(end), idle};
}

void Near(double value, double want, double tolerance, const char* what) {
    if (value < want - tolerance || value > want + tolerance) {
        std::printf("%s: got %.3f, want %.3f +- %.3f\n", what, value, want, tolerance);
        assert(false);
    }
}
}

int main() {
    // The stock rate: 25 frames a second, asleep for all but the work, and one sleep a frame.
    Result r = Run(40, 5000, 300);
    Near(r.frames / 20.0, 25.0, 0.3, "stock rate");
    Near(r.asleep, 0.875, 0.02, "stock rate asleep");
    assert(r.idlePasses <= r.frames / 50 + 1);

    // A sleep that ends a millisecond late does not slow the game: the gate carries the overshoot.
    r = Run(40, 5000, 1500);
    Near(r.frames / 20.0, 25.0, 0.3, "stock rate, late sleeps");

    // 60 a second (a 16 ms frame), which the game's own sleep could not hold.
    r = Run(16, 4000, 300);
    Near(r.frames / 20.0, 62.5, 1.0, "16 ms frame");
    Near(r.asleep, 0.75, 0.03, "16 ms frame asleep");

    // A target the machine cannot reach: no waiting at all, every pass a frame.
    r = Run(5, 8000, 300);
    Near(r.frames / 20.0, 125.0, 1.5, "frame slower than the target");
    assert(r.asleep < 0.001 && r.idlePasses <= 1); // the one wait is for the first frame

    // A fast target the machine can reach: the rate holds and the rest is sleep.
    r = Run(5, 2000, 100);
    Near(r.frames / 20.0, 200.0, 4.0, "5 ms frame");
    Near(r.asleep, 0.6, 0.05, "5 ms frame asleep");

    // Before the first frame (loading): idle as the game did, never spin.
    r = Run(40, 5000, 300, false);
    assert(r.frames == 0); Near(r.asleep, 1.0, 0.01, "before the first frame");
    // The frame is due and none ran (the pause menu): the same, a frame at most.
    assert(FrameWait::Milliseconds(true, 40, 1000, 5000, false) == 10);
    assert(FrameWait::Milliseconds(true, 5, 1000, 5000, false) == 5);
    assert(FrameWait::Milliseconds(true, 40, 0, 5000, false) == 10);

    // Multiplayer is not ours to pace.
    assert(FrameWait::Milliseconds(false, 5, 1000, 1001, true) == 10);
    // A clock that ran backwards waits a frame at most.
    assert(FrameWait::Milliseconds(true, 40, 5000, 1000, true) == 40);
    // The frame is due and the loop is keeping up: go straight on.
    assert(FrameWait::Milliseconds(true, 40, 1000, 1040, true) == 0);
    assert(FrameWait::Milliseconds(true, 40, 1000, 1039, true) == 1);

    // The sender's clock: real time at the stock rate and in multiplayer,
    // 40 ms a frame at any single player rate, and never backwards.
    FrameWait::SenderClock stock, fast, remote;
    assert(stock.Read(1000, true, 40) == 1000 && stock.Read(1200, true, 40) == 1200);
    assert(remote.Read(1000, false, 5) == 1000 && remote.Read(1200, false, 5) == 1200);
    assert(fast.Read(1000, true, 5) == 1000);
    assert(fast.Read(1005, true, 5) == 1040);  // one 5 ms frame is 40 ms of it
    assert(fast.Read(1025, true, 5) == 1200);  // the 200 ms window is five frames
    assert(fast.Read(1025, true, 5) == 1200);
    assert(fast.Read(1125, true, 40) == 1300); // the rate changed: no jump
    FrameWait::SenderClock wrap;
    const unsigned before = wrap.Read(0xfffffff0u, true, 40);
    assert(wrap.Read(0x00000010u, true, 40) - before == 0x20); // the tick count wrapped

    std::puts("frame wait checks passed");
    return 0;
}
