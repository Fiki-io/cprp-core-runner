#include <jni.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <android/log.h>

#define LOG_TAG "CPRP_CORE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern int box86_main(int argc, const char **argv, char **env);

JNIEXPORT jint JNICALL Java_com_cprp_launcher_NativeBridge_startCore(
    JNIEnv* env,
    jobject thiz,
    jobjectArray argsArray) {
    if (!argsArray) return -1;
    int argc = (*env)->GetArrayLength(env, argsArray);
    const char** argv = (const char**)malloc((argc + 1) * sizeof(char*));
    for (int i = 0; i < argc; i++) {
        jstring str = (jstring)(*env)->GetObjectArrayElement(env, argsArray, i);
        argv[i] = (*env)->GetStringUTFChars(env, str, 0);
    }
    argv[argc] = NULL;

    LOGI("CPRP Core Runner invoked with %d arguments.", argc);
    for (int i = 0; i < argc; i++) {
        LOGI("arg[%d] = %s", i, argv[i]);
    }

    int result = box86_main(argc, argv, NULL);
    LOGI("CPRP Core Runner finished with code: %d", result);
    return result;
}
