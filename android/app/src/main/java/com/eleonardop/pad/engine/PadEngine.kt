package com.eleonardop.pad.engine

import android.content.Context
import android.os.Handler
import android.os.Looper
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit
import java.util.concurrent.locks.ReentrantReadWriteLock
import kotlin.concurrent.read
import kotlin.concurrent.write

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
    SWELL_TIME("Tempo do Swell manual (s)"),
    // v0.5
    TONE_CUT("Abafar (fecha o som inteiro)"),
    VOICING_STYLE("Montagem do acorde", listOf("Padrão", "Quinta por baixo (pads de fundo)")),
    LAYER_GUITAR("Guitarra (swells)"),
    // v0.6
    VELVET("Aveludado");

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
 *
 * Ciclo de vida (v0.6):
 *  - o motor nativo é criado sob demanda e pode ser destruído por [release] (Sair, deslizar o
 *    app para fora dos recentes, "Encerrar" na notificação): fecha a saída de áudio, para o
 *    thread carregador e libera samples e buffers;
 *  - [suspendAudio] fecha só a saída de áudio (app em segundo plano e sem tocar): o callback
 *    deixa de rodar (zero CPU), os sons continuam carregados para voltar na hora;
 *  - o handle nativo é protegido por uma trava de leitura/escrita: as chamadas de controle usam
 *    a leitura (rápida, várias ao mesmo tempo) e a destruição usa a escrita, então nenhuma
 *    chamada usa um motor já liberado.
 */
object PadEngine {
    @Volatile private var appContext: Context? = null

    /** Chamar uma vez (Activity ou Service) antes de qualquer outra função: dá acesso aos samples do APK. */
    fun init(context: Context) {
        if (appContext == null) appContext = context.applicationContext
    }

    private val lock = ReentrantReadWriteLock()
    private var handle: Long = 0L

    /** Executa [f] com o handle vivo (0 se o motor foi liberado). */
    private inline fun <T> withHandle(f: (Long) -> T): T = lock.read { f(handle) }

    private fun ensureHandle(): Long = lock.write {
        if (handle == 0L) {
            handle = NativeBridge.nativeCreate(appContext?.assets)
            if (lastVolume >= 0f) NativeBridge.nativeSetParam(handle, PadParam.MASTER_VOLUME.id, lastVolume)
        }
        handle
    }

    // Um thread só para preparar/abrir/fechar; ele termina sozinho depois de 3 s parado.
    private val worker = ThreadPoolExecutor(1, 1, 3, TimeUnit.SECONDS, LinkedBlockingQueue()) { r ->
        Thread(r, "pad-engine-ctl")
    }.apply { allowCoreThreadTimeOut(true) }
    private val main = Handler(Looper.getMainLooper())
    @Volatile private var lastVolume = -1f

    @Volatile var isStarted = false
        private set

    /** Quem quer saber quando o app está sendo encerrado (a tela fecha a si mesma). */
    private val exitListeners = mutableListOf<() -> Unit>()
    fun addExitListener(l: () -> Unit) { synchronized(exitListeners) { exitListeners += l } }
    fun removeExitListener(l: () -> Unit) { synchronized(exitListeners) { exitListeners -= l } }

    val noteNames = listOf("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")

    /** Abre a saída de áudio e prepara o motor (~1–2 s na primeira vez), fora da thread de UI. */
    fun start(onReady: (Boolean) -> Unit) {
        worker.execute {
            val h = ensureHandle()
            val ok = isStarted || withHandle { if (it == h && it != 0L) NativeBridge.nativeStart(it) else false }
            isStarted = ok
            main.post { onReady(ok) }
        }
    }

    /** Fecha só a saída de áudio (o estado do motor e os sons carregados são mantidos). */
    fun suspendAudio() {
        worker.execute {
            withHandle { if (it != 0L) NativeBridge.nativeShutdownAudio(it) }
            isStarted = false
        }
    }

    /** Compatibilidade com a v0.5. */
    fun shutdownAudio() = suspendAudio()

    /**
     * Encerra tudo: abaixa o volume (sem estalo), fecha a saída de áudio e destrói o motor
     * nativo (thread carregador, samples, buffers). Avisa quem pediu ([addExitListener]).
     * Pode ser chamado de qualquer thread; é idempotente.
     */
    fun release(onDone: (() -> Unit)? = null) {
        val listeners = synchronized(exitListeners) { exitListeners.toList() }
        main.post { listeners.forEach { it() } }
        worker.execute {
            val h = withHandle { it }
            if (h != 0L) {
                val v = NativeBridge.nativeGetParam(h, PadParam.MASTER_VOLUME.id)
                if (v > 0f) lastVolume = v
                NativeBridge.nativeSetParam(h, PadParam.MASTER_VOLUME.id, 0f)
                if (isStarted) Thread.sleep(180) // o volume desce suavizado (~50 ms) antes de fechar
                lock.write {
                    NativeBridge.nativeDestroy(handle)
                    handle = 0L
                }
            }
            isStarted = false
            onDone?.let { main.post(it) }
        }
    }

    /** O motor existe e está tocando notas (ou soltando o fim delas)? */
    fun isPlaying(): Boolean = withHandle { it != 0L && NativeBridge.nativeStats(it).let { s -> s.size > 4 && s[4] > 0.5f } }

    fun playChord(rootPitchClass: Int, type: ChordType): Boolean = withHandle { NativeBridge.nativePlayChord(it, rootPitchClass, type.ordinal) }
    fun setKey(rootPitchClass: Int): Boolean = withHandle { NativeBridge.nativeSetKey(it, rootPitchClass) }
    fun stop(): Boolean = withHandle { NativeBridge.nativeStopNotes(it) }
    /** Pre-swell manual: cresce durante "Tempo do Swell" e atinge o pico no fim (troque o acorde ali). */
    fun swell(): Boolean = withHandle { NativeBridge.nativeSwell(it) }

    fun setParam(param: PadParam, value: Float) = withHandle { NativeBridge.nativeSetParam(it, param.id, value) }
    fun getParam(param: PadParam): Float = withHandle { NativeBridge.nativeGetParam(it, param.id) }

    val presetNames: List<String> by lazy { (0 until NativeBridge.nativePresetCount()).map { NativeBridge.nativePresetName(it) } }

    /** Categoria de cada preset ("" = versão anterior). */
    val presetCategories: List<String> by lazy { (0 until NativeBridge.nativePresetCount()).map { NativeBridge.nativePresetCategory(it) } }

    /** Biblioteca principal (v0.4): categoria -> índices, na ordem de exibição. */
    val library: List<Pair<String, List<Int>>> by lazy {
        listOf("SOFT PADS", "FOUNDATION", "ATMOSPHERE", "CHARACTER", "TEXTURES").map { cat ->
            cat to presetCategories.indices.filter { presetCategories[it] == cat }
        }.filter { it.second.isNotEmpty() }
    }

    /** Versões anteriores (v0.2 e v0.3), só na área Avançado, para comparação. */
    val legacyPresets: List<Int> by lazy { presetCategories.indices.filter { presetCategories[it].isEmpty() } }

    fun presetDescription(index: Int): String = NativeBridge.nativePresetDescription(index)
    fun applyPreset(index: Int) = withHandle { NativeBridge.nativeApplyPreset(it, index) }

    /** Espera as fontes pedidas pelo preset/parâmetros atuais (fora da thread de UI). */
    fun waitForSources(seconds: Double = 20.0, onDone: (Boolean) -> Unit) {
        worker.execute {
            val ok = withHandle { NativeBridge.nativeWaitForSources(it, seconds) }
            main.post { onDone(ok) }
        }
    }

    /**
     * Teste de desempenho no próprio aparelho. Para o som, renderiza todos os presets fora do
     * tempo real, mede o tempo de cada bloco e religa a saída. Leva ~2–3 min (rápido: ~30 s).
     */
    fun runBenchmark(quick: Boolean, onDone: (String) -> Unit) {
        worker.execute {
            val r = withHandle { NativeBridge.nativeBenchmark(it, quick) }
            main.post { onDone(r) }
        }
    }

    fun stats(): EngineStats {
        val s = withHandle { NativeBridge.nativeStats(it) }
        if (s.size < 19) return EngineStats(0, 0f, 0f, 1f, false, 0, 0, 0, 0f, 0f, 0, 0, -1, 0, 0, 0, 0, 0, 0)
        return EngineStats(
            s[0].toInt(), s[1], s[2], s[3], s[4] > 0.5f, s[5].toInt(), s[6].toInt(), s[7].toInt(),
            s[8], s[9], s[10].toInt(), s[11].toInt(), s[12].toInt(), s[13].toInt(),
            s[14].toInt(), s[15].toInt(), s[16].toInt(), s[17].toInt(), s[18].toInt(),
        )
    }
}
