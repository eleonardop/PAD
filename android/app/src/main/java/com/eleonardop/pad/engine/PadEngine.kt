package com.eleonardop.pad.engine

import android.content.Context
import android.os.Handler
import android.os.Looper
import java.util.concurrent.Executors

/** Parâmetros do motor. A ordem é a mesma de `ParamId` em engine/src/core/Params.h. */
enum class PadParam(val label: String, val choices: List<String>? = null) {
    LAYER_WARM("Camada Warm (PADsynth)"),
    LAYER_AIR("Camada Air (harmônica)"),
    LAYER_STRINGS("Camada Strings"),
    LAYER_SHIMMER("Camada Shimmer (Glass)"),
    LAYER_LOW("Camada Low"),
    WARMTH("Calor (Warmth)"),
    BRIGHTNESS("Brilho"),
    AIR("Ar (Air)"),
    LOW("Graves (Low)"),
    WIDTH("Largura estéreo"),
    MOVEMENT("Movimento"),
    DETUNE("Detune"),
    REVERB("Reverb (mixagem)"),
    REVERB_DECAY("Duração do reverb (s)"),
    REVERB_SIZE("Tamanho da sala"),
    REVERB_DAMPING("Amortecimento do reverb"),
    SHIMMER("Shimmer do reverb"),
    ATTACK("Attack (s)"),
    RELEASE("Release (s)"),
    CROSSFADE_TIME("Crossfade (s)"),
    MASTER_VOLUME("Volume geral"),
    // v0.2
    LAYER_VOX("Camada Vox (coral)"),
    LAYER_TEXTURE("Camada Textura (piano/órgão)"),
    CHORUS("Chorus ensemble"),
    EVOLUTION("Evolução"),
    INTENSITY("Intensidade"),
    REVERB_TYPE("Tipo de reverb", listOf("Clássico (v0.1)", "Plate (Dattorro)", "Cloud (Cloud Seed)")),
    PRE_DELAY("Pré-delay (s)"),
    EARLY_REFLECTIONS("Reflexões iniciais"),
    DIFFUSION("Difusão"),
    STRINGS_SOURCE("Fonte das cordas", listOf("Cordas reais (VSCO 2 CE)", "Cordas sintéticas (v0.1)")),
    TEXTURE_TYPE("Tipo de textura", listOf("Piano Cathedral (Salamander)", "Órgão suave (VCSL)")),
    // v0.3
    LAYER_KEYS("Teclas esparsas (TEXTURE)"),
    KEYS_TYPE("Tipo de teclas", listOf("Piano suave (VCSL)", "EP (FM próprio)")),
    LAYER_SPARKLE("Brilho de vibrafone (SPARKLE)"),
    LAYER_METAL("Pratos (baqueta macia)"),
    TAPE("Fita (Vintage)"),
    ACTIVITY("Atividade das texturas"),
    BLOOM("Transições: intensidade (Bloom)"),
    BLOOM_CHANCE("Transições: frequência"),
    SWELL_TIME("Tempo do Swell manual (s)");

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
    val load: Float,          // carga média do callback de áudio (1,0 = 100% do tempo disponível)
    val peak: Float,
    val limiterGain: Float,
    val playing: Boolean,
    val deviceSampleRate: Int,
    val bufferFrames: Int,
    val voiceSteals: Int,
    val loadPeak: Float,      // pior bloco desde a leitura anterior
    val sourcesMemoryMb: Float,
    val sourcesReady: Int,
    val sourcesFailed: Int,
    val xRuns: Int,           // falhas de buffer contadas pelo Android (-1 = indisponível)
    val burstFrames: Int,
    val textureVoices: Int,
    val textureEvents: Int,
    val blooms: Int,
    val swells: Int,
    val lifeStage: Int,       // 0 silêncio, 1 estabelecimento, 2 expansão, 3 transformação, 4 sustentação, 5 dissolução
)

/**
 * API Kotlin do motor de pads. Um único motor por processo (o áudio é único).
 *
 * Todas as chamadas de controle são seguras em qualquer thread e não bloqueiam o áudio:
 * no lado nativo viram comandos numa fila sem trava ou parâmetros atômicos.
 */
object PadEngine {
    @Volatile private var appContext: Context? = null

    /** Chamar uma vez (Activity ou Service) antes de qualquer outra função: dá acesso aos samples do APK. */
    fun init(context: Context) {
        if (appContext == null) appContext = context.applicationContext
    }

    private val handle: Long by lazy { NativeBridge.nativeCreate(appContext?.assets) }
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
    /** Pre-swell manual: cresce durante "Tempo do Swell" e atinge o pico no fim (troque o acorde ali). */
    fun swell(): Boolean = NativeBridge.nativeSwell(handle)

    fun setParam(param: PadParam, value: Float) = NativeBridge.nativeSetParam(handle, param.id, value)
    fun getParam(param: PadParam): Float = NativeBridge.nativeGetParam(handle, param.id)

    val presetNames: List<String> by lazy { (0 until NativeBridge.nativePresetCount()).map { NativeBridge.nativePresetName(it) } }

    /** Ordem de exibição: presets da v0.3 primeiro (índices 8+), depois os da v0.2. */
    val presetDisplayOrder: List<Int> by lazy {
        val n = presetNames.size
        (8 until n).toList() + (0 until minOf(8, n)).toList()
    }
    fun presetDescription(index: Int): String = NativeBridge.nativePresetDescription(index)
    fun applyPreset(index: Int) = NativeBridge.nativeApplyPreset(handle, index)

    /** Espera as fontes pedidas pelo preset/parâmetros atuais (fora da thread de UI). */
    fun waitForSources(seconds: Double = 20.0, onDone: (Boolean) -> Unit) {
        worker.execute {
            val ok = NativeBridge.nativeWaitForSources(handle, seconds)
            main.post { onDone(ok) }
        }
    }

    /**
     * Teste de desempenho no próprio aparelho. Para o som, renderiza todos os presets fora do
     * tempo real, mede o tempo de cada bloco e religa a saída. Leva ~2–3 min (rápido: ~30 s).
     */
    fun runBenchmark(quick: Boolean, onDone: (String) -> Unit) {
        worker.execute {
            val r = NativeBridge.nativeBenchmark(handle, quick)
            main.post { onDone(r) }
        }
    }

    fun stats(): EngineStats {
        val s = NativeBridge.nativeStats(handle)
        return EngineStats(
            s[0].toInt(), s[1], s[2], s[3], s[4] > 0.5f, s[5].toInt(), s[6].toInt(), s[7].toInt(),
            s[8], s[9], s[10].toInt(), s[11].toInt(), s[12].toInt(), s[13].toInt(),
            s[14].toInt(), s[15].toInt(), s[16].toInt(), s[17].toInt(), s[18].toInt(),
        )
    }
}
