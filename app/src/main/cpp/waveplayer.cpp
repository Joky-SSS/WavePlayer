#include <jni.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

extern "C" {
#include "avilib/wavlib.h"
}

static const char *JAVA_IO_IOEXCEPTION = "java/io/IOException";
static const char *JAVA_LANG_OUTOFMEMORYERROR = "java/lang/OutOfMemoryError";

#define ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))

struct PlayerContext{
    SLObjectItf engineObject;
    SLEngineItf engineEngine;
    SLObjectItf outputMixObject;
    SLObjectItf audioPlayerObject;
    SLAndroidSimpleBufferQueueItf  audioPlayerBufferQueue;
    SLPlayItf audioPlayerPlay;
    WAV wav;
    unsigned char *buffer;
    size_t bufferSize;
};

static void ThrowException(JNIEnv *env, const char *className, const char *message) {
    jclass clazz = env->FindClass(className);
    if (clazz != nullptr) {
        env->ThrowNew(clazz, message);
        env->DeleteLocalRef(clazz);
    }
}

static WAV OpenWavFile(JNIEnv *env, jstring fileName) {
    WAVError error = WAV_SUCCESS;
    WAV wav = nullptr;
    const char *cFileName = env->GetStringUTFChars(fileName, 0);
    if (cFileName == nullptr) {
        goto exit;
    }
    wav = wav_open(cFileName, WAV_READ, &error);
    env->ReleaseStringUTFChars(fileName, cFileName);
    if (wav == nullptr) {
        ThrowException(env, JAVA_IO_IOEXCEPTION, wav_strerror(error));
    }
    exit:
    return wav;
}

static void CloseWaveFile(WAV wav) {
    if (wav != nullptr) {
        wav_close(wav);
    }
}

static bool CheckError(JNIEnv *env, SLresult result) {
    bool isError = false;
    if (SL_RESULT_SUCCESS != result) {
        isError = true;
        ThrowException(env, JAVA_IO_IOEXCEPTION, "Error Occurred");
    }
    return isError;
}

static void CreateEngine(JNIEnv *env, SLObjectItf &engineObject) {
    SLEngineOption engineOptions[] = {
            (SLuint32) SL_ENGINEOPTION_THREADSAFE,
            (SLuint32) SL_BOOLEAN_TRUE
    };
    SLresult result = slCreateEngine(&engineObject, ARRAY_LEN(engineOptions), engineOptions, 0, 0,
                                     0);
    CheckError(env, result);
}

static void RealizedObject(JNIEnv *env, SLObjectItf object) {
    SLresult result = (*object)->Realize(object, SL_BOOLEAN_FALSE);
    CheckError(env, result);
}

static void DestroyObject(SLObjectItf object) {
    if (object != nullptr) {
        (*object)->Destroy(object);
    }
    object = nullptr;
}

static void GetEngineInterface(JNIEnv *env, SLObjectItf &engineObject, SLEngineItf &engineEngine) {
    SLresult result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine);
    CheckError(env, result);
}

static void CreateOutputMix(JNIEnv *env, SLEngineItf engineEngine, SLObjectItf &outputMixObject) {
    SLresult result = (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 0, 0, 0);
    CheckError(env, result);
}

static void FreePlayerBuffer(unsigned char *&buffers) {
    if (buffers != nullptr) {
        delete buffers;
        buffers = nullptr;
    }
}

static void InitPlayerBuffer(JNIEnv *env, WAV wav, unsigned char *&buffer, size_t &bufferSize) {
    bufferSize = wav_get_channels(wav) * wav_get_rate(wav) * wav_get_bits(wav);
    buffer = new unsigned char[bufferSize];
    if (buffer == nullptr) {
        ThrowException(env, JAVA_LANG_OUTOFMEMORYERROR, "buffer");
    }
}

static void
CreateBufferQueueAudioPlayer(WAV wav, SLEngineItf engineEngine, SLObjectItf outputMixObject,
                             SLObjectItf &audioPlayerObject) {
    SLDataLocator_AndroidSimpleBufferQueue dataSourceLocator = {
            SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 1
    };
    SLDataFormat_PCM dataSourceFormat = {
            SL_DATAFORMAT_PCM,
            wav_get_channels(wav),
            static_cast<SLuint32>(wav_get_rate(wav) * 1000),
            wav_get_bits(wav),
            wav_get_bits(wav),
            SL_SPEAKER_FRONT_CENTER,
            SL_BYTEORDER_LITTLEENDIAN
    };
    SLDataSource dataSource = {
            &dataSourceLocator,
            &dataSourceFormat
    };
    SLDataLocator_OutputMix dataSinkLocator = {
            SL_DATALOCATOR_OUTPUTMIX,
            outputMixObject
    };
    SLDataSink dataSink = {
            &dataSinkLocator,
            0
    };
    SLInterfaceID interfaceIds[] = {SL_IID_BUFFERQUEUE};
    SLboolean requiredInterfaces[] = {
            SL_BOOLEAN_TRUE
    };
    SLresult result = (*engineEngine)->CreateAudioPlayer(engineEngine, &audioPlayerObject,
                                                         &dataSource, &dataSink,
                                                         ARRAY_LEN(interfaceIds), interfaceIds,
                                                         requiredInterfaces);
}

static void GetAudioPlayerBufferQueueInterface(JNIEnv *env, SLObjectItf audioPlayerObject,
                                               SLAndroidSimpleBufferQueueItf &audioPlayerBufferQueue) {
    SLresult result = (*audioPlayerObject)->GetInterface(audioPlayerObject, SL_IID_BUFFERQUEUE,
                                                         &audioPlayerBufferQueue);
    CheckError(env,result);
}

static void DestroyContext(PlayerContext *&ctx){
    DestroyObject(ctx->audioPlayerObject);
    FreePlayerBuffer(ctx->buffer);
    DestroyObject(ctx->outputMixObject);
    DestroyObject(ctx->engineObject);
    CloseWaveFile(ctx->wav);
    delete ctx;
    ctx = nullptr;
}

static void PlayerCallback(SLAndroidSimpleBufferQueueItf audioPlayerBufferQueue,
                           void *context){
    PlayerContext *ctx = (PlayerContext*)context;
    ssize_t readSize = wav_read_data(ctx->wav,ctx->buffer,ctx->bufferSize);
    if (readSize > 0){
        (*audioPlayerBufferQueue)->Enqueue(audioPlayerBufferQueue,ctx->buffer,readSize);
    }else{
        DestroyContext(ctx);
    }
}

static void RegisterPlayerCallback(JNIEnv *env,SLAndroidSimpleBufferQueueItf audioPlayerBufferQueue,
                                   PlayerContext *ctx){
    SLresult result = (*audioPlayerBufferQueue)->RegisterCallback(audioPlayerBufferQueue,PlayerCallback,ctx);
    CheckError(env,result);
}

static void GetAudioPlayerPlayInterface(JNIEnv *env,SLObjectItf audioPlayerObject,SLPlayItf &audioPlayerPlay){
    SLresult result = (*audioPlayerObject)->GetInterface(audioPlayerObject,SL_IID_PLAY,&audioPlayerPlay);
    CheckError(env,result);
}

static void SetAudioPlayerStatePlaying(JNIEnv *env,SLPlayItf audioPlayerPlay){
    SLresult result = (*audioPlayerPlay)->SetPlayState(audioPlayerPlay,SL_PLAYSTATE_PLAYING);
    CheckError(env,result);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_jokyxray_waveplayer_MainActivity_play(JNIEnv *env, jclass clazz, jstring file_name) {
    PlayerContext *ctx = new PlayerContext();
    ctx->wav = OpenWavFile(env,file_name);
    if (env->ExceptionOccurred() != nullptr)
        goto exit;
    CreateEngine(env,ctx->engineObject);
    if (env->ExceptionOccurred() != nullptr)
        goto exit;
    RealizedObject(env,ctx->engineObject);
    if (env->ExceptionOccurred() != nullptr)
        goto exit;
    GetEngineInterface(env,ctx->engineObject,ctx->engineEngine);
    if (env->ExceptionOccurred() != nullptr)
        goto exit;
    CreateOutputMix(env,ctx->engineEngine,ctx->outputMixObject);
    if (env->ExceptionOccurred() != nullptr)
        goto exit;
    RealizedObject(env,ctx->outputMixObject);
    if (env->ExceptionOccurred() != nullptr)
        goto exit;
    InitPlayerBuffer(env,ctx->wav,ctx->buffer,ctx->bufferSize);
    if (env->ExceptionOccurred() != nullptr)
        goto exit;
    CreateBufferQueueAudioPlayer(ctx->wav,ctx->engineEngine,ctx->outputMixObject,ctx->audioPlayerObject);
    if (env->ExceptionOccurred() != nullptr)
        goto exit;
    RealizedObject(env,ctx->audioPlayerObject);
    if (env->ExceptionOccurred() != nullptr)
        goto exit;
    GetAudioPlayerBufferQueueInterface(env,ctx->audioPlayerObject,ctx->audioPlayerBufferQueue);
    if (env->ExceptionOccurred() != nullptr)
        goto exit;
    RegisterPlayerCallback(env,ctx->audioPlayerBufferQueue,ctx);
    if (env->ExceptionOccurred() != nullptr)
        goto exit;
    GetAudioPlayerPlayInterface(env,ctx->audioPlayerObject,ctx->audioPlayerPlay);
    if (env->ExceptionOccurred() != nullptr)
        goto exit;
    SetAudioPlayerStatePlaying(env,ctx->audioPlayerPlay);
    if (env->ExceptionOccurred() != nullptr)
        goto exit;
    PlayerCallback(ctx->audioPlayerBufferQueue,ctx);
    exit:
    if (env->ExceptionOccurred() != nullptr)
        DestroyContext(ctx);
}



