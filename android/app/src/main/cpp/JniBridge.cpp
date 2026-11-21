// Ponte JNI entre Kotlin (com.eleonardop.pad.engine.NativeBridge) e o motor C++.
// Nenhuma função aqui é chamada pelo callback de áudio; todas rodam em threads Java.
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>
#include <memory>
#include <string>
#include "OboePlayer.h"
#include "PadEngine.h"
#include "core/Sources.h"
#include "tools/Benchmark.h"

namespace {

// Lê os samples empacotados no APK (pasta assets/ do repositório). Chamado só pelo thread
// carregador do motor, nunca pelo callback de áudio.
class ApkAssetProvider : public pad::AssetProvider {
public:
    ApkAssetProvider(JNIEnv* env, jobject assetManager)
        : ref_(env->NewGlobalRef(assetManager)), mgr_(AAssetManager_fromJava(env, assetManager)) {
        env->GetJavaVM(&vm_);
    }
    ~ApkAssetProvider() override {
        JNIEnv* env = nullptr;
        if (vm_ && vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK && env) env->DeleteGlobalRef(ref_);
    }
    bool readAll(const std::string& path, std::vector<uint8_t>& out) override {
        if (!mgr_) return false;
        AAsset* a = AAssetManager_open(mgr_, path.c_str(), AASSET_MODE_STREAMING);
        if (!a) return false;
        const off64_t len = AAsset_getLength64(a);
        out.resize(static_cast<size_t>(len));
        int64_t done = 0;
        while (done < len) {
            const int r = AAsset_read(a, out.data() + done, static_cast<size_t>(std::min<int64_t>(len - done, 1 << 20)));
            if (r <= 0) break;
            done += r;
        }
        AAsset_close(a);
        return done == len;
    }
private:
    JavaVM* vm_ = nullptr;
    jobject ref_;
    AAssetManager* mgr_;
};

struct NativeHandle {
    pad::PadEngine engine;
    padandroid::OboePlayer player{engine};
};

inline NativeHandle* H(jlong h) { return reinterpret_cast<NativeHandle*>(h); }

bool validParam(jint id) { return id >= 0 && id < pad::kParamCount; }

} // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativeCreate(JNIEnv* env, jclass, jobject assetManager) {
    auto* h = new NativeHandle();
    if (assetManager) h->engine.setAssetProvider(std::make_shared<ApkAssetProvider>(env, assetManager));
    return reinterpret_cast<jlong>(h);
}

// Teste de desempenho no aparelho: para a saída de áudio, renderiza fora do tempo real e
// religa a saída se ela estava ligada. Chamar fora do thread de UI (leva ~2–3 minutos).
JNIEXPORT jstring JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativeBenchmark(JNIEnv* env, jclass, jlong h, jboolean quick) {
    if (!h) return env->NewStringUTF("");
    NativeHandle* n = H(h);
    const bool wasRunning = n->player.isRunning();
    n->player.stop();
    if (!n->engine.isPrepared() && !n->engine.prepare(48000.0f)) return env->NewStringUTF("Falha ao preparar o motor.");
    pad::BenchmarkOptions o;
    o.blockFrames = n->player.burstFrames() > 0 ? n->player.burstFrames() : 192;
    if (quick) { o.quick = true; o.secondsPerCase = 4.0; }
    const std::string r = pad::runBenchmark(n->engine, o);
    if (wasRunning) n->player.start();
    return env->NewStringUTF(r.c_str());
}

JNIEXPORT jboolean JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativeWaitForSources(JNIEnv*, jclass, jlong h, jdouble seconds) {
    return h && H(h)->engine.waitForSources(seconds) ? JNI_TRUE : JNI_FALSE;
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

// [vozes ativas, carga média do callback (0..1+), pico de saída, menor ganho do limitador,
//  tocando (0/1), taxa do dispositivo, frames de buffer, roubos de voz, pior bloco (0..1+),
//  memória das fontes (MB), máscara de fontes prontas, máscara de fontes com falha,
//  falhas de buffer (underruns) informadas pelo Android, frames por burst]
JNIEXPORT jfloatArray JNICALL Java_com_eleonardop_pad_engine_NativeBridge_nativeStats(JNIEnv* env, jclass, jlong h) {
    constexpr int kN = 14;
    jfloatArray arr = env->NewFloatArray(kN);
    if (!h) return arr;
    NativeHandle* n = H(h);
    const pad::EngineStats s = n->engine.readStats();
    const jfloat v[kN] = {static_cast<jfloat>(s.activeVoices), s.load, s.peakOut, s.limiterMinGain, s.playing ? 1.0f : 0.0f,
                          static_cast<jfloat>(n->player.deviceSampleRate()), static_cast<jfloat>(n->player.bufferFrames()),
                          static_cast<jfloat>(s.steals), s.loadPeak, static_cast<jfloat>(s.sourcesMemoryBytes / 1e6),
                          static_cast<jfloat>(s.sourcesReady), static_cast<jfloat>(s.sourcesFailed),
                          static_cast<jfloat>(n->player.xRuns()), static_cast<jfloat>(n->player.burstFrames())};
    env->SetFloatArrayRegion(arr, 0, kN, v);
    return arr;
}

} // extern "C"
