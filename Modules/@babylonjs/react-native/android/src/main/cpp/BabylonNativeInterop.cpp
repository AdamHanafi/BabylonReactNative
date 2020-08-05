#include <jni.h>

#include <Babylon/JsRuntime.h>
#include <Babylon/Plugins/NativeWindow.h>
#include <Babylon/Plugins/NativeEngine.h>
#include <Babylon/Plugins/NativeInput.h>
#include <Babylon/Plugins/NativeXr.h>
#include <Babylon/Polyfills/Window.h>

#include <AndroidExtensions/Globals.h>

#include <arcana/threading/task_schedulers.h>

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <optional>
#include <sstream>
#include <unistd.h>

#include <jsi/jsi.h>

namespace Babylon
{
    namespace
    {
        void log(const char *str)
        {
            __android_log_print(ANDROID_LOG_VERBOSE, "BabylonNative", "%s", str);
        }
    }

    class Native final
    {
    public:
        // This class must be constructed from the JavaScript thread
        Native(facebook::jsi::Runtime* jsiRuntime, ANativeWindow* windowPtr)
            : m_env{ Napi::Attach<facebook::jsi::Runtime&>(*jsiRuntime) }
        {
            auto looper_scheduler = std::make_shared<looper_scheduler_t>(looper_scheduler_t::get_for_current_thread());

            JsRuntime::DispatchFunctionT dispatchFunction{[env = m_env, looper_scheduler = std::move(looper_scheduler)](std::function<void(Napi::Env)> func) {
                (*looper_scheduler)([env, func = std::move(func)]()
                {
                    func(env);
                });
            }};

            m_runtime = &JsRuntime::CreateForJavaScript(m_env, dispatchFunction);

            auto width = static_cast<size_t>(ANativeWindow_getWidth(windowPtr));
            auto height = static_cast<size_t>(ANativeWindow_getHeight(windowPtr));

            Plugins::NativeEngine::InitializeGraphics(windowPtr, width, height);
            Plugins::NativeEngine::Initialize(m_env);
            Plugins::NativeWindow::Initialize(m_env, windowPtr, width, height);
            Plugins::NativeXr::Initialize(m_env);

            Polyfills::Window::Initialize(m_env);

            m_nativeInput = &Babylon::Plugins::NativeInput::CreateForJavaScript(m_env);

            // TODO: This shouldn't be necessary, but for some reason results in a significant increase in frame rate. Need to figure this out.
            Plugins::NativeEngine::Reinitialize(m_env, windowPtr, width, height);
        }

        ~Native()
        {
            Plugins::NativeEngine::DeinitializeGraphics();
            // TODO: Figure out why this causes the app to crash
            //Napi::Detach(m_env);
        }

        void Refresh(ANativeWindow* windowPtr)
        {
            auto width = static_cast<size_t>(ANativeWindow_getWidth(windowPtr));
            auto height = static_cast<size_t>(ANativeWindow_getHeight(windowPtr));
            Plugins::NativeEngine::Reinitialize(m_env, windowPtr, width, height);
        }

        void SetPointerButtonState(uint32_t pointerId, uint32_t buttonId, bool isDown, uint32_t x, uint32_t y)
        {
            if (isDown)
            {
                m_nativeInput->PointerDown(pointerId, buttonId, x, y);
            }
            else
            {
                m_nativeInput->PointerUp(pointerId, buttonId, x, y);
            }
        }

        void SetPointerPosition(uint32_t pointerId, uint32_t x, uint32_t y)
        {
            m_nativeInput->PointerMove(pointerId, x, y);
        }

    private:
        using looper_scheduler_t = arcana::looper_scheduler<sizeof(std::weak_ptr<Napi::Env>) + sizeof(std::function<void(Napi::Env)>)>;

        Napi::Env m_env;
        JsRuntime* m_runtime;
        Plugins::NativeInput* m_nativeInput;
    };
}

extern "C" JNIEXPORT void JNICALL Java_com_reactlibrary_BabylonNativeInterop_initialize(JNIEnv* env, jclass obj, jobject context)
{
    JavaVM* javaVM{};
    if (env->GetJavaVM(&javaVM) != JNI_OK)
    {
        throw std::runtime_error("Failed to get Java VM");
    }

    android::global::Initialize(javaVM, context);
}

extern "C" JNIEXPORT void JNICALL Java_com_reactlibrary_BabylonNativeInterop_setCurrentActivity(JNIEnv* env, jclass obj, jobject activity)
{
    android::global::SetCurrentActivity(activity);
}

extern "C" JNIEXPORT void JNICALL Java_com_reactlibrary_BabylonNativeInterop_pause(JNIEnv* env, jclass obj)
{
    android::global::Pause();
}

extern "C" JNIEXPORT void JNICALL Java_com_reactlibrary_BabylonNativeInterop_resume(JNIEnv* env, jclass obj)
{
    android::global::Resume();
}

extern "C" JNIEXPORT jlong JNICALL Java_com_reactlibrary_BabylonNativeInterop_create(JNIEnv* env, jclass obj, jlong jsiRuntimeRef, jobject surface)
{
    auto jsiRuntime = reinterpret_cast<facebook::jsi::Runtime*>(jsiRuntimeRef);
    ANativeWindow* windowPtr = ANativeWindow_fromSurface(env, surface);
    auto native = new Babylon::Native(jsiRuntime, windowPtr);
    return reinterpret_cast<intptr_t>(native);
}

extern "C" JNIEXPORT void JNICALL Java_com_reactlibrary_BabylonNativeInterop_refresh(JNIEnv* env, jclass obj, jlong instanceRef, jobject surface)
{
    auto native = reinterpret_cast<Babylon::Native*>(instanceRef);
    ANativeWindow* windowPtr = ANativeWindow_fromSurface(env, surface);
    native->Refresh(windowPtr);
}

extern "C" JNIEXPORT void JNICALL Java_com_reactlibrary_BabylonNativeInterop_setPointerButtonState(JNIEnv* env, jclass obj, jlong instanceRef, jint pointerId, jint buttonId, jboolean isDown, jint x, jint y)
{
    auto native = reinterpret_cast<Babylon::Native*>(instanceRef);
    native->SetPointerButtonState(static_cast<uint32_t>(pointerId), static_cast<uint32_t>(buttonId), isDown, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
}

extern "C" JNIEXPORT void JNICALL Java_com_reactlibrary_BabylonNativeInterop_setPointerPosition(JNIEnv* env, jclass obj, jlong instanceRef, jint pointerId, jint x, jint y)
{
    auto native = reinterpret_cast<Babylon::Native*>(instanceRef);
    native->SetPointerPosition(static_cast<uint32_t>(pointerId), static_cast<uint32_t>(x), static_cast<uint32_t>(y));
}

extern "C" JNIEXPORT void JNICALL Java_com_reactlibrary_BabylonNativeInterop_destroy(JNIEnv* env, jclass obj, jlong instanceRef)
{
    auto native = reinterpret_cast<Babylon::Native*>(instanceRef);
    delete native;
}
