#pragma once

// How long the game's message loop may sleep after an idle pass. Kept free of
// game dependencies so the arithmetic can be tested against a model of the
// single player frame gate (tests/frame_wait.cpp).
//
// The gate (SrvProcessAllGames, 0x52fc20) runs a frame once `now - last`
// reaches `period`, where `last` is the tick it recorded for the frame before.
namespace FrameWait {
    // What the game itself slept after every pass (MessageGameLoop, 0x451c42).
    const unsigned IdleMs = 10;

    // singlePlayer: the server steps in this loop, at the rate the setting asks for.
    // period:  milliseconds per frame. last: tick of the last frame, 0 before the first.
    // now:     the gate's clock. stepped: a frame ran since the previous call.
    inline unsigned Milliseconds(bool singlePlayer, int period, int last, int now, bool stepped) {
        // Multiplayer keeps its own pace; idle as the game did.
        if (!singlePlayer || period <= 0 || !last) return IdleMs;
        const int left = period - (now - last);
        // Until the gate opens, and never longer than a frame if the clocks disagree.
        if (left > 0) return unsigned(left < period ? left : period);
        // The frame is due. Either the loop is behind and must not wait, or
        // nothing is stepping (the pause menu, loading) and it idles as before.
        if (stepped) return 0;
        return unsigned(period) < IdleMs ? unsigned(period) : IdleMs;
    }

    // The clock for the client's duplicate-request window. The sender
    // (NET_D2GS_CLIENT_Send, 0x478350) drops a request identical to the one
    // before it within 200 ms of GetTickCount, 50 ms for skill and attack
    // requests: five frames and one at the stock 40 ms. Left on real time, a
    // single player game asked to run at 5 ms a frame would hold a repeated
    // request for 35 frames, 1.4 seconds of game time, and a character whose
    // step was interrupted stands still for it. So in single player this
    // clock runs as fast as the game is asked to: 40 of its milliseconds a
    // frame. Elsewhere it is real time.
    struct SenderClock {
        double value = 0;
        unsigned last = 0;
        bool started = false;
        unsigned Read(unsigned real, bool singlePlayer, int period) {
            if (!started) { started = true; last = real; value = real; }
            const unsigned elapsed = real - last; // unsigned: survives the tick count wrapping
            last = real;
            value += singlePlayer && period > 0 ? elapsed * 40.0 / period : double(elapsed);
            return unsigned(static_cast<unsigned long long>(value));
        }
    };
}
