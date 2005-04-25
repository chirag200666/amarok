
#ifndef ENGINE_FWD_H
#define ENGINE_FWD_H

/// If you need just the enums

namespace Engine
{
    class SimpleMetaBundle;
    class Effects;
    class Base;

    /**
     * You should return:
     * Playing when playing,
     * Paused when paused
     * Idle when you still have a URL loaded (ie you have not been told to stop())
     * Empty when you have been told to stop(), or an error occured and you stopped yourself
     *
     * It is vital to be Idle just after the track has ended!
     */
    enum State { Empty, Idle, Playing, Paused };
    enum StreamingMode { Socket, Signal, NoStreaming };
}

typedef Engine::Base EngineBase;

#endif
