package com.eleonardop.pad.engine

import android.os.Handler
import android.os.Looper
import java.util.concurrent.Executors

/** Parâmetros do motor. A ordem é a mesma de `ParamId` em engine/src/core/Params.h. */
enum class PadParam(val label: String) {
    LAYER_WARM("Camada Warm"),
    LAYER_AIR("Camada Air"),
    LAYER_STRINGS("Camada Strings"),
    LAYER_SHIMMER("Camada Shimmer"),
    LAYER_LOW("Camada Low"),
    WARMTH("Calor (Warmth)"),
    BRIGHTNESS("Brilho"),
    AIR("Ar (Air)"),
    LOW("Graves (Low)"),
    WIDTH("Largura estéreo"),
    MOVEMENT("Movimento"),
    DETUNE("Detune"),
    REVERB("Reverb"),
    REVERB_DECAY("Duração do reverb (s)"),
    REVERB_SIZE("Tamanho da sala"),
    REVERB_DAMPING("Amortecimento do reverb"),
    SHIMMER("Shimmer"),
    ATTACK("Attack (s)"),
    RELEASE("Release (s)"),
    CROSSFADE_TIME("Crossfade (s)"),
    MASTER_VOLUME("Volume geral");

    val id: Int get() = ordinal
    val range: FloatArray by lazy { NativeBridge.nativeParamRange(ordinal) } // [min, max, padrão]
}

/** Tipos de acorde. A ordem é a mesma de `ChordType` em engine/src/core/MusicTheory.h. */
enum class ChordType(val label: String) {
    PAD5("Pad (1-5-8)"), MAJOR("Maior"), MINOR("Menor"), SUS2("Sus2"), SUS4("Sus4"),
    MAJOR7("Maj7"), MINOR7("m7"), DOMINANT7("7"), ADD9("Add9"), MINOR_ADD9("m(add9)")
}

data class EngineStats(
    val activeVoices: Int,
    val load: Float,
    val peak: Float,
    val limiterGain: Float,
    val playing: Boolean,
    val deviceSampleRate: Int,
    val bufferFrames: Int,
    val voiceSteals: Int,
)

/**
 * API Kotlin do motor de pads. Um único motor por processo (o áudio é único).
 *
 * Todas as chamadas de controle são seguras em qualquer thread e não bloqueiam o áudio:
 * no lado nativo viram comandos numa fila sem trava ou parâmetros atômicos.
 */
object PadEngine {
    private val handle: Long by lazy { NativeBridge.nativeCreate() }
    private val worker = Executors.newSingleThreadExecutor { r -> Thread(r, "pad-engine-init") }
    private val main = Handler(Looper.getMainLooper())

    @Volatile var isStarted = false
        private set

    val noteNames = listOf("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")

    /** Abre a saída de áudio e prepara o motor (~1–2 s na primeira vez), fora da thread de UI. */
    fun start(onReady: (Boolean) -> Unit) {
        worker.execute {
            val ok = isStarted || NativeBridge.nativeStart(handle)
            isStarted = ok
            main.post { onReady(ok) }
        }
    }

    /** Fecha a saída de áudio (o estado do motor é mantido). */
    fun shutdownAudio() {
        worker.execute {
            NativeBridge.nativeShutdownAudio(handle)
            isStarted = false
        }
    }

    fun playChord(rootPitchClass: Int, type: ChordType): Boolean = NativeBridge.nativePlayChord(handle, rootPitchClass, type.ordinal)
    fun setKey(rootPitchClass: Int): Boolean = NativeBridge.nativeSetKey(handle, rootPitchClass)
    fun stop(): Boolean = NativeBridge.nativeStopNotes(handle)

    fun setParam(param: PadParam, value: Float) = NativeBridge.nativeSetParam(handle, param.id, value)
    fun getParam(param: PadParam): Float = NativeBridge.nativeGetParam(handle, param.id)

    val presetNames: List<String> by lazy { (0 until NativeBridge.nativePresetCount()).map { NativeBridge.nativePresetName(it) } }
    fun presetDescription(index: Int): String = NativeBridge.nativePresetDescription(index)
    fun applyPreset(index: Int) = NativeBridge.nativeApplyPreset(handle, index)

    fun stats(): EngineStats {
        val s = NativeBridge.nativeStats(handle)
        return EngineStats(s[0].toInt(), s[1], s[2], s[3], s[4] > 0.5f, s[5].toInt(), s[6].toInt(), s[7].toInt())
    }
}
