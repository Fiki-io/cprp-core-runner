#include <jni.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <setjmp.h>
#include <pthread.h>
#include <stdio.h>
#include <android/log.h>

#define LOG_TAG "CPRP_CORE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern int box86_main(int argc, const char **argv, char **env);

static __thread jmp_buf s_exit_jmp;
static __thread int s_in_runner = 0;

#define CONSOLE_BUFFER_SIZE 65536
static char s_console_buffer[CONSOLE_BUFFER_SIZE];
static size_t s_console_len = 0;
static pthread_mutex_t s_buffer_mutex = PTHREAD_MUTEX_INITIALIZER;

static int s_pipe_read_fd = -1;
static pthread_t s_logger_thread;
static int s_logger_initialized = 0;

static void append_to_buffer(const char* data, size_t len) {
    if (!data || len == 0) return;
    pthread_mutex_lock(&s_buffer_mutex);
    if (s_console_len + len >= CONSOLE_BUFFER_SIZE - 1) {
        size_t overflow = (s_console_len + len) - (CONSOLE_BUFFER_SIZE - 2);
        if (overflow < s_console_len) {
            memmove(s_console_buffer, s_console_buffer + overflow, s_console_len - overflow);
            s_console_len -= overflow;
        } else {
            s_console_len = 0;
        }
    }
    memcpy(s_console_buffer + s_console_len, data, len);
    s_console_len += len;
    s_console_buffer[s_console_len] = '\0';
    pthread_mutex_unlock(&s_buffer_mutex);
}

static void* logger_thread_func(void* arg) {
    int fd = (int)(intptr_t)arg;
    char buf[1024];
    while (1) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = '\0';
        __android_log_write(ANDROID_LOG_INFO, LOG_TAG, buf);
        append_to_buffer(buf, (size_t)n);
    }
    return NULL;
}

static void init_stdout_stderr_pipe(void) {
    if (s_logger_initialized) return;
    int pfd[2];
    if (pipe(pfd) == 0) {
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
        dup2(pfd[1], STDOUT_FILENO);
        dup2(pfd[1], STDERR_FILENO);
        close(pfd[1]);
        s_pipe_read_fd = pfd[0];
        pthread_create(&s_logger_thread, NULL, logger_thread_func, (void*)(intptr_t)s_pipe_read_fd);
        pthread_detach(s_logger_thread);
        s_logger_initialized = 1;
    }
}

__attribute__((visibility("default"), noreturn)) void exit(int status) {
    LOGI("exit(%d) intercepted in CPRP native bridge", status);
    if (s_in_runner) {
        longjmp(s_exit_jmp, status == 0 ? 10000 : status);
    }
    _Exit(status);
}

__attribute__((visibility("default"), noreturn)) void _exit(int status) {
    exit(status);
}

__attribute__((visibility("default"), noreturn)) void _Exit(int status) {
    LOGI("_Exit(%d) intercepted in CPRP native bridge", status);
    if (s_in_runner) {
        longjmp(s_exit_jmp, status == 0 ? 10000 : status);
    }
    abort();
}

static int run_core_internal(JNIEnv* env, jobjectArray argsArray) {
    init_stdout_stderr_pipe();
    if (!argsArray) return -1;
    int argc = (*env)->GetArrayLength(env, argsArray);
    const char** argv = (const char**)malloc((argc + 1) * sizeof(char*));
    for (int i = 0; i < argc; i++) {
        jstring str = (jstring)(*env)->GetObjectArrayElement(env, argsArray, i);
        argv[i] = (*env)->GetStringUTFChars(env, str, 0);
    }
    argv[argc] = NULL;

    LOGI("CPRP Core Runner (Box86 ARM32) invoked with %d arguments.", argc);
    for (int i = 0; i < argc; i++) {
        LOGI("arg[%d] = %s", i, argv[i]);
    }

    s_in_runner = 1;
    int jmp_res = setjmp(s_exit_jmp);
    int result = 0;
    if (jmp_res == 0) {
        result = box86_main(argc, argv, NULL);
    } else {
        result = (jmp_res == 10000) ? 0 : jmp_res;
        LOGI("box86_main unwound via intercepted exit with code: %d", result);
    }
    s_in_runner = 0;

    fflush(stdout);
    fflush(stderr);
    usleep(25000);

    for (int i = 0; i < argc; i++) {
        jstring str = (jstring)(*env)->GetObjectArrayElement(env, argsArray, i);
        (*env)->ReleaseStringUTFChars(env, str, argv[i]);
    }
    free(argv);

    LOGI("CPRP Core Runner finished with code: %d", result);
    return result;
}

JNIEXPORT jint JNICALL Java_com_cprp_launcher_NativeBridge_startCore(
    JNIEnv* env,
    jobject thiz,
    jobjectArray argsArray) {
    return run_core_internal(env, argsArray);
}

JNIEXPORT jstring JNICALL Java_com_cprp_launcher_NativeBridge_pollLogOutput(
    JNIEnv* env,
    jobject thiz) {
    pthread_mutex_lock(&s_buffer_mutex);
    jstring res = (*env)->NewStringUTF(env, s_console_buffer);
    s_console_buffer[0] = '\0';
    s_console_len = 0;
    pthread_mutex_unlock(&s_buffer_mutex);
    return res;
}
