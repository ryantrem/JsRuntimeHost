#include "AppRuntime.h"
#include "WorkQueue.h"
#include <cassert>

namespace Babylon
{
    AppRuntime::AppRuntime() :
        AppRuntime{{}}
    {
    }

    AppRuntime::AppRuntime(Options options)
        : m_workQueue{std::make_unique<WorkQueue>([this] { RunPlatformTier(); })}
        , m_options{std::move(options)}
    {
        Dispatch([this](Napi::Env env) {
            JsRuntime::CreateForJavaScript(env, [this](auto func) { Dispatch(std::move(func)); });
        });
    }

    AppRuntime::~AppRuntime()
    {
        // Setting this flag before the implicit ~unique_ptr<WorkQueue> runs
        // prevents Dispatch() from dereferencing m_workQueue during shutdown.
        // On libc++ (Apple), unique_ptr nulls its pointer before calling the
        // deleter, creating a window where other threads (e.g., TimeoutDispatcher's
        // background thread) can reach Dispatch() via the join chain and
        // dereference null, causing EXC_BAD_ACCESS.
        m_destructing = true;
    }

    void AppRuntime::Run(Napi::Env env)
    {
        m_workQueue->Run(env);
    }

    void AppRuntime::Suspend()
    {
        m_workQueue->Suspend();
    }

    void AppRuntime::Resume()
    {
        m_workQueue->Resume();
    }

    void AppRuntime::Dispatch(Dispatchable<void(Napi::Env)> func)
    {
        // Prevent re-entrancy during destruction since m_workQueue can already be nulled
        // on some platforms (e.g. Apple's libc++) before the AppRuntime destructor body runs.
        if (m_destructing)
        {
            return;
        }

        m_workQueue->Append([this, func{std::move(func)}](Napi::Env env) mutable {
            Execute([this, env, func{std::move(func)}]() mutable {
                try
                {
                    func(env);
                }
                catch (const Napi::Error& error)
                {
                    m_options.UnhandledExceptionHandler(error);
                }
                catch (...)
                {
                    assert(false);
                    std::abort();
                }
            });
        });
    }
}
