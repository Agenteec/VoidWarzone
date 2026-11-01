#include "Platform.h"

#include <SDL3/SDL.h>

#if defined(SDL_PLATFORM_ANDROID)
#include <jni.h>
#endif

namespace vw::client {

#if defined(SDL_PLATFORM_ANDROID)

namespace {

struct Activity {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    jobject self = static_cast<jobject>(SDL_GetAndroidActivity());
    jclass cls = env && self ? env->GetObjectClass(self) : nullptr;

    ~Activity() {
        if (cls)
            env->DeleteLocalRef(cls);
        if (self)
            env->DeleteLocalRef(self);
    }

    jmethodID method(const char* name, const char* sig) const {
        if (!cls)
            return nullptr;
        jmethodID m = env->GetMethodID(cls, name, sig);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return nullptr;
        }
        return m;
    }
};

} // namespace

bool systemShare() { return true; }

void shareText(const std::string& title, const std::string& text) {
    Activity a;
    jmethodID m = a.method("shareText", "(Ljava/lang/String;Ljava/lang/String;)V");
    if (!m)
        return;
    jstring jt = a.env->NewStringUTF(title.c_str());
    jstring jx = a.env->NewStringUTF(text.c_str());
    a.env->CallVoidMethod(a.self, m, jt, jx);
    if (a.env->ExceptionCheck())
        a.env->ExceptionClear();
    a.env->DeleteLocalRef(jt);
    a.env->DeleteLocalRef(jx);
}

std::string takeJoinLink() {
    Activity a;
    jmethodID m = a.method("takePendingJoin", "()Ljava/lang/String;");
    if (!m)
        return {};
    auto js = static_cast<jstring>(a.env->CallObjectMethod(a.self, m));
    if (a.env->ExceptionCheck())
        a.env->ExceptionClear();
    if (!js)
        return {};
    const char* chars = a.env->GetStringUTFChars(js, nullptr);
    std::string code = chars ? chars : "";
    a.env->ReleaseStringUTFChars(js, chars);
    a.env->DeleteLocalRef(js);
    return code;
}

#else

bool systemShare() { return false; }

void shareText(const std::string&, const std::string& text) { SDL_SetClipboardText(text.c_str()); }

std::string takeJoinLink() { return {}; }

#endif

std::string inviteLink(const std::string& code) { return "voidwarzone://join/" + code; }

} // namespace vw::client
