// Ponte JNI entre Kotlin (com.eleonardop.pad.engine.NativeBridge) e o motor C++.
// Nenhuma função aqui é chamada pelo callback de áudio; todas rodam em threads Java.
#include <jni.h>
#include <memory>
#include "OboePlayer.h"
#include "PadEngine.h"

namespace {

struct NativeHandle {
    pad::PadEngine engine;
    padandroid::OboePlayer player{engine};
};

inline NativeHandle* H(jlong h) { return reinterpret_cast<NativeHandle*>(h); }

bool validParam(jint id) { return id >= 0 && id < pad::kParamCount; }

} // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativeCreate(JNIEnv*, jclass) {
    return reinterpret_cast<jlong>(new NativeHandle());
}

JNIEXPORT void JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativeDestroy(JNIEnv*, jclass, jlong h) {
    if (!h) return;
    H(h)->player.stop();
    delete H(h);
}

JNIEXPORT jboolean JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativeStart(JNIEnv*, jclass, jlong h) {
    return h && H(h)->player.start() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativeShutdownAudio(JNIEnv*, jclass, jlong h) {
    if (h) H(h)->player.stop();
}

JNIEXPORT jboolean JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativePlayChord(JNIEnv*, jclass, jlong h, jint root, jint type) {
    if (!h || type < 0 || type >= static_cast<jint>(pad::ChordType::Count)) return JNI_FALSE;
    return H(h)->engine.playChord(root, static_cast<pad::ChordType>(type)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativeSetKey(JNIEnv*, jclass, jlong h, jint root) {
    return h && H(h)->engine.setKey(root) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativeStopNotes(JNIEnv*, jclass, jlong h) {
    return h && H(h)->engine.stop() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativeSetParam(JNIEnv*, jclass, jlong h, jint id, jfloat v) {
    if (h && validParam(id)) H(h)->engine.setParam(static_cast<pad::ParamId>(id), v);
}

JNIEXPORT jfloat JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativeGetParam(JNIEnv*, jclass, jlong h, jint id) {
    return (h && validParam(id)) ? H(h)->engine.getParam(static_cast<pad::ParamId>(id)) : 0.0f;
}

JNIEXPORT void JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativeApplyPreset(JNIEnv*, jclass, jlong h, jint index) {
    if (h) H(h)->engine.applyPreset(index);
}

JNIEXPORT jint JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativePresetCount(JNIEnv*, jclass) {
    return pad::presetCount();
}

JNIEXPORT jstring JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativePresetName(JNIEnv* env, jclass, jint index) {
    return env->NewStringUTF(pad::preset(index).name);
}

JNIEXPORT jstring JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativePresetDescription(JNIEnv* env, jclass, jint index) {
    return env->NewStringUTF(pad::preset(index).description);
}

JNIEXPORT jint JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativeParamCount(JNIEnv*, jclass) {
    return pad::kParamCount;
}

JNIEXPORT jstring JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativeParamName(JNIEnv* env, jclass, jint id) {
    return env->NewStringUTF(validParam(id) ? pad::paramInfo(static_cast<pad::ParamId>(id)).name : "");
}

// [min, max, padrão]
JNIEXPORT jfloatArray JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativeParamRange(JNIEnv* env, jclass, jint id) {
    jfloatArray arr = env->NewFloatArray(3);
    if (!validParam(id)) return arr;
    const auto& p = pad::paramInfo(static_cast<pad::ParamId>(id));
    const jfloat v[3] = {p.minValue, p.maxValue, p.defaultValue};
    env->SetFloatArrayRegion(arr, 0, 3, v);
    return arr;
}

// [vozes ativas, carga do callback (0..1+), pico de saída, menor ganho do limitador,
//  tocando (0/1), taxa do dispositivo, frames de buffer, roubos de voz]
JNIEXPORT jfloatArray JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativeStats(JNIEnv* env, jclass, jlong h) {
    jfloatArray arr = env->NewFloatArray(8);
    if (!h) return arr;
    NativeHandle* n = H(h);
    n->engine.collectGarbage();
    const pad::EngineStats s = n->engine.readStats();
    const jfloat v[8] = {static_cast<jfloat>(s.activeVoices), s.load, s.peakOut, s.limiterMinGain, s.playing ? 1.0f : 0.0f,
                         static_cast<jfloat>(n->player.deviceSampleRate()), static_cast<jfloat>(n->player.bufferFrames()),
                         static_cast<jfloat>(s.steals)};
    env->SetFloatArrayRegion(arr, 0, 8, v);
    return arr;
}

} // extern "C"
