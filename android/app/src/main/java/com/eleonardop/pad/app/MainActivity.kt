package com.eleonardop.pad.app

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.GridLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.TextView
import android.widget.Toast
import com.eleonardop.pad.engine.ChordType
import com.eleonardop.pad.engine.PadEngine
import com.eleonardop.pad.engine.PadParam
import com.eleonardop.pad.engine.PadPlaybackService

/**
 * Tela do PAD Worship (v0.5).
 *
 * Principal: biblioteca por categoria (SOFT PADS com 5 sons; as outras com 2), tom, tipo de
 * acorde, SWELL/PARAR e seis controles (Volume, Reverb, Abafar, Intensidade, Evolução,
 * Microeventos). Todo o resto — parâmetros
 * avançados, versões anteriores dos presets, desempenho e créditos — fica em "Avançado".
 */
class MainActivity : Activity() {

    private val ui = Handler(Looper.getMainLooper())
    private lateinit var status: TextView
    private lateinit var description: TextView
    private lateinit var perf: TextView
    private lateinit var advanced: LinearLayout
    private lateinit var advancedToggle: Button
    private val sliders = mutableMapOf<PadParam, Pair<SeekBar, TextView>>()
    private val choices = mutableMapOf<PadParam, Spinner>()
    private val presetButtons = mutableMapOf<Int, Button>()
    private var chordType = ChordType.PAD5
    private var currentKey = -1
    private var currentPreset = -1
    private val keyButtons = mutableListOf<Button>()
    private var busy = false
    private var refreshing = false
    private var worstLoad = 0f

    private val bg = Color.rgb(16, 18, 22)
    private val panel = Color.rgb(30, 33, 40)
    private val accent = Color.rgb(222, 178, 92)
    private val keyActive = Color.rgb(40, 110, 220)
    private val mainParams = listOf(PadParam.MASTER_VOLUME, PadParam.REVERB, PadParam.TONE_CUT, PadParam.INTENSITY, PadParam.EVOLUTION, PadParam.ACTIVITY)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        PadEngine.init(this)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        setContentView(buildUi())
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
        }
        status.text = "Preparando os sons..."
        PadEngine.start { ok ->
            status.text = if (ok) "Pronto. Escolha um som e um tom." else "Não foi possível abrir a saída de áudio."
            if (ok) selectPreset(PadEngine.library.firstOrNull()?.second?.firstOrNull() ?: 0)
        }
        ui.post(statsTick)
    }

    override fun onDestroy() {
        ui.removeCallbacks(statsTick)
        super.onDestroy()
    }

    private val statsTick = object : Runnable {
        override fun run() {
            if (PadEngine.isStarted && !busy) {
                val s = PadEngine.stats()
                worstLoad = maxOf(worstLoad, s.loadPeak)
                val key = if (currentKey >= 0) "${PadEngine.noteNames[currentKey]} ${chordType.label}" else "—"
                val name = if (currentPreset >= 0) PadEngine.presetNames[currentPreset] else ""
                status.text = "$name   ·   $key   ·   ${if (s.playing) "tocando" else "parado"}"
                if (advanced.visibility == View.VISIBLE) {
                    perf.text = "Carga do áudio (callback real): média ${pct(s.load)}, pior bloco ${pct(s.loadPeak)}, pior desde o início ${pct(worstLoad)}\n" +
                        "Falhas de buffer (Android): ${if (s.xRuns < 0) "n/d" else s.xRuns}   ·   saída ${s.deviceSampleRate} Hz, burst ${s.burstFrames}\n" +
                        "Vozes: pad ${s.activeVoices}, texturas ${s.textureVoices}   ·   eventos ${s.textureEvents}, blooms ${s.blooms}, swells ${s.swells}\n" +
                        "Fontes na memória: ${"%.0f".format(s.sourcesMemoryMb)} MB" +
                        (if (s.sourcesFailed != 0) "   (alguma fonte falhou: usando reserva)" else "")
                }
            }
            ui.postDelayed(this, 500)
        }
    }

    private fun pct(v: Float) = "%.0f%%".format(v * 100f)
    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    private fun rounded(color: Int, radius: Int = 10, stroke: Int = 0) = GradientDrawable().apply {
        setColor(color)
        cornerRadius = dp(radius).toFloat()
        if (stroke != 0) setStroke(dp(2), stroke)
    }

    private fun label(text: String, size: Float = 12f, color: Int = Color.rgb(150, 156, 168), bold: Boolean = false) =
        TextView(this).apply {
            this.text = text
            textSize = size
            setTextColor(color)
            if (bold) typeface = Typeface.DEFAULT_BOLD
        }

    private fun sectionTitle(t: String) = label(t, 11f, accent, true).apply {
        setPadding(dp(2), dp(14), 0, dp(4))
        letterSpacing = 0.12f
    }

    private fun buildUi(): View {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(14), dp(16), dp(24))
            setBackgroundColor(bg)
        }
        root.addView(label("PAD Worship", 20f, Color.WHITE, true))
        status = label("", 13f, Color.rgb(150, 190, 255)).apply { setPadding(0, dp(4), 0, dp(4)) }
        root.addView(status)

        // ---- Biblioteca: categorias com dois sons cada
        for ((cat, indices) in PadEngine.library) {
            root.addView(sectionTitle(cat))
            for (chunk in indices.chunked(3)) {
            val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
            for (i in chunk) {
                val b = Button(this).apply {
                    text = PadEngine.presetNames[i]
                    isAllCaps = false
                    textSize = 16f
                    setTextColor(Color.WHITE)
                    background = rounded(panel)
                    setOnClickListener { selectPreset(i) }
                }
                presetButtons[i] = b
                row.addView(b, LinearLayout.LayoutParams(0, dp(56), 1f).apply { setMargins(dp(4), dp(2), dp(4), dp(2)) })
            }
            // linha incompleta: espaço vazio para os botões manterem o mesmo tamanho
            if (indices.size > 2) repeat(3 - chunk.size) { row.addView(View(this), LinearLayout.LayoutParams(0, dp(56), 1f).apply { setMargins(dp(4), dp(2), dp(4), dp(2)) }) }
            root.addView(row)
            }
        }
        description = label("", 12f).apply { setPadding(dp(4), dp(8), dp(4), dp(6)) }
        root.addView(description)

        // ---- Tom e acorde
        val chordRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        chordRow.addView(label("Acorde", 13f, Color.LTGRAY).apply { setPadding(dp(4), 0, dp(12), 0) })
        val chordSpinner = Spinner(this)
        chordSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, ChordType.entries.map { it.label })
        chordSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(p: AdapterView<*>?, v: View?, pos: Int, id: Long) {
                chordType = ChordType.entries[pos]
                if (!busy && currentKey >= 0 && PadEngine.stats().playing) PadEngine.playChord(currentKey, chordType)
            }
            override fun onNothingSelected(p: AdapterView<*>?) {}
        }
        chordRow.addView(chordSpinner, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        root.addView(chordRow)

        val grid = GridLayout(this).apply { columnCount = 4; setPadding(0, dp(6), 0, dp(6)) }
        for (k in 0 until 12) {
            val b = Button(this).apply {
                text = PadEngine.noteNames[k]
                textSize = 18f
                setTextColor(Color.WHITE)
                background = rounded(panel)
                setOnClickListener { onKey(k) }
            }
            keyButtons += b
            grid.addView(b, GridLayout.LayoutParams().apply {
                width = 0; height = dp(60)
                columnSpec = GridLayout.spec(GridLayout.UNDEFINED, 1f)
                setMargins(dp(3), dp(3), dp(3), dp(3))
            })
        }
        root.addView(grid)

        val actions = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        actions.addView(Button(this).apply {
            text = "SWELL"
            setTextColor(Color.BLACK)
            background = rounded(accent)
            setOnClickListener { if (PadEngine.isStarted && !busy && currentKey >= 0) PadEngine.swell() }
        }, LinearLayout.LayoutParams(0, dp(52), 1f).apply { setMargins(dp(3), dp(4), dp(3), dp(4)) })
        actions.addView(Button(this).apply {
            text = "PARAR"
            setTextColor(Color.WHITE)
            background = rounded(Color.rgb(90, 40, 40))
            setOnClickListener { PadEngine.stop(); currentKey = -1; highlightKeys() }
        }, LinearLayout.LayoutParams(0, dp(52), 1f).apply { setMargins(dp(3), dp(4), dp(3), dp(4)) })
        root.addView(actions)

        // ---- Controles principais
        root.addView(sectionTitle("CONTROLES"))
        for (p in mainParams) addSlider(root, p, mainLabel(p))

        // ---- Avançado (recolhido)
        advancedToggle = Button(this).apply {
            text = "Avançado  ▸"
            isAllCaps = false
            setTextColor(Color.LTGRAY)
            background = rounded(panel)
            setOnClickListener { toggleAdvanced() }
        }
        root.addView(advancedToggle, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(48)).apply { topMargin = dp(16) })
        advanced = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; visibility = View.GONE }
        buildAdvanced(advanced)
        root.addView(advanced)
        return ScrollView(this).apply { setBackgroundColor(bg); addView(root) }
    }

    private fun mainLabel(p: PadParam) = when (p) {
        PadParam.MASTER_VOLUME -> "Volume"
        PadParam.REVERB -> "Reverb"
        PadParam.INTENSITY -> "Intensidade"
        PadParam.EVOLUTION -> "Evolução"
        PadParam.ACTIVITY -> "Microeventos"
        PadParam.TONE_CUT -> "Abafar"
        else -> p.label
    }

    private fun buildAdvanced(box: LinearLayout) {
        box.addView(sectionTitle("VERSÕES ANTERIORES (comparação)"))
        val legacy = PadEngine.legacyPresets
        val sp = Spinner(this)
        sp.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, listOf("— escolher —") + legacy.map { PadEngine.presetNames[it] })
        sp.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(a: AdapterView<*>?, v: View?, pos: Int, id: Long) {
                if (pos > 0 && !busy) selectPreset(legacy[pos - 1])
            }
            override fun onNothingSelected(a: AdapterView<*>?) {}
        }
        box.addView(sp)

        box.addView(sectionTitle("FONTES"))
        for (p in PadParam.entries.filter { it.choices != null }) {
            box.addView(label(p.label, 13f, Color.LTGRAY))
            val s = Spinner(this)
            s.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, p.choices!!)
            s.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
                override fun onItemSelected(a: AdapterView<*>?, v: View?, pos: Int, id: Long) {
                    if (!refreshing && !busy) PadEngine.setParam(p, pos.toFloat())
                }
                override fun onNothingSelected(a: AdapterView<*>?) {}
            }
            choices[p] = s
            box.addView(s)
        }
        val groups = listOf(
            "CAMADAS" to listOf(PadParam.LAYER_WARM, PadParam.LAYER_VOX, PadParam.LAYER_STRINGS, PadParam.LAYER_TEXTURE,
                PadParam.LAYER_AIR, PadParam.LAYER_SHIMMER, PadParam.LAYER_LOW),
            "TEXTURAS E TRANSIÇÕES" to listOf(PadParam.LAYER_KEYS, PadParam.LAYER_SPARKLE, PadParam.LAYER_METAL,
                PadParam.BLOOM, PadParam.BLOOM_CHANCE, PadParam.SWELL_TIME, PadParam.TAPE, PadParam.LAYER_GUITAR),
            "CARÁTER" to listOf(PadParam.CHORUS, PadParam.WARMTH, PadParam.BRIGHTNESS, PadParam.TONE_CUT, PadParam.AIR, PadParam.LOW,
                PadParam.WIDTH, PadParam.MOVEMENT, PadParam.DETUNE),
            "REVERB" to listOf(PadParam.REVERB_DECAY, PadParam.REVERB_SIZE, PadParam.REVERB_DAMPING, PadParam.PRE_DELAY,
                PadParam.EARLY_REFLECTIONS, PadParam.DIFFUSION, PadParam.SHIMMER),
            "ENVELOPE" to listOf(PadParam.ATTACK, PadParam.RELEASE, PadParam.CROSSFADE_TIME),
        )
        for ((title, list) in groups) {
            box.addView(sectionTitle(title))
            for (p in list) if (p !in mainParams) addSlider(box, p, p.label)
        }

        box.addView(sectionTitle("DESEMPENHO E INFORMAÇÕES"))
        perf = label("", 11f).apply { setPadding(dp(2), dp(2), dp(2), dp(6)) }
        box.addView(perf)
        box.addView(Button(this).apply { text = "Teste de desempenho rápido (~30 s)"; setOnClickListener { benchmark(true) } })
        box.addView(Button(this).apply { text = "Teste de desempenho completo (~4 min)"; setOnClickListener { benchmark(false) } })
        box.addView(Button(this).apply { text = "Créditos e licenças dos sons"; setOnClickListener { showCredits() } })
    }

    private fun toggleAdvanced() {
        val show = advanced.visibility != View.VISIBLE
        advanced.visibility = if (show) View.VISIBLE else View.GONE
        advancedToggle.text = if (show) "Avançado  ▾" else "Avançado  ▸"
    }

    private fun selectPreset(index: Int) {
        if (busy) return
        PadEngine.applyPreset(index)
        currentPreset = index
        description.text = PadEngine.presetDescription(index)
        for ((i, b) in presetButtons) {
            b.background = if (i == index) rounded(Color.rgb(48, 44, 30), stroke = accent) else rounded(panel)
            b.setTextColor(if (i == index) accent else Color.WHITE)
        }
        ui.postDelayed({ refreshControls() }, 50)
    }

    private fun addSlider(box: LinearLayout, p: PadParam, title: String) {
        val text = label("", 13f, Color.LTGRAY)
        val bar = SeekBar(this).apply { max = 1000 }
        bar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(s: SeekBar?, progress: Int, fromUser: Boolean) {
                val v = toValue(p, progress)
                if (fromUser && !busy) PadEngine.setParam(p, v)
                text.text = "$title: ${format(p, v)}"
            }
            override fun onStartTrackingTouch(s: SeekBar?) {}
            override fun onStopTrackingTouch(s: SeekBar?) {}
        })
        sliders[p] = bar to text
        box.addView(text)
        box.addView(bar)
    }

    private fun format(p: PadParam, v: Float) = when {
        isTime(p) || p == PadParam.PRE_DELAY -> "%.2f s".format(v)
        else -> "%.0f%%".format(100f * (v - p.range[0]) / (p.range[1] - p.range[0]))
    }

    private fun onKey(k: Int) {
        if (!PadEngine.isStarted || busy) return
        PadPlaybackService.ensureRunning(this)
        PadEngine.playChord(k, chordType)
        currentKey = k
        highlightKeys()
    }

    private fun highlightKeys() {
        keyButtons.forEachIndexed { i, b -> b.background = rounded(if (i == currentKey) keyActive else panel) }
    }

    private fun benchmark(quick: Boolean) {
        if (!PadEngine.isStarted || busy) return
        busy = true
        currentKey = -1
        highlightKeys()
        status.text = "Teste de desempenho em andamento: o som fica desligado até terminar..."
        val device = "Aparelho: ${Build.MANUFACTURER} ${Build.MODEL}, Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})" +
            (if (Build.VERSION.SDK_INT >= 31) ", processador ${Build.SOC_MANUFACTURER} ${Build.SOC_MODEL}" else "") +
            ", ABI ${Build.SUPPORTED_ABIS.firstOrNull() ?: "?"}\n"
        PadEngine.runBenchmark(quick) { report ->
            busy = false
            status.text = "Teste concluído."
            if (currentPreset >= 0) selectPreset(currentPreset)
            val text = device + report +
                "\nEste teste roda fora do áudio (thread comum). A carga real do callback aparece em Avançado → Desempenho."
            AlertDialog.Builder(this)
                .setTitle("Resultado do teste de desempenho")
                .setMessage(text)
                .setPositiveButton("Copiar") { _, _ ->
                    (getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager)
                        .setPrimaryClip(ClipData.newPlainText("PAD desempenho", text))
                    Toast.makeText(this, "Resultado copiado.", Toast.LENGTH_SHORT).show()
                }
                .setNegativeButton("Fechar", null)
                .show()
        }
    }

    private fun showCredits() {
        val text = try {
            assets.open("CREDITOS_E_LICENCAS.txt").bufferedReader().use { it.readText() }
        } catch (e: Exception) {
            "Arquivo de créditos não encontrado no APK."
        }
        val tv = TextView(this).apply { this.text = text; textSize = 12f; setPadding(dp(16), dp(12), dp(16), dp(12)) }
        AlertDialog.Builder(this)
            .setTitle("Créditos e licenças")
            .setView(ScrollView(this).apply { addView(tv) })
            .setPositiveButton("Fechar", null)
            .show()
    }

    private fun isTime(p: PadParam) = p == PadParam.ATTACK || p == PadParam.RELEASE || p == PadParam.CROSSFADE_TIME ||
        p == PadParam.REVERB_DECAY || p == PadParam.SWELL_TIME

    private fun toValue(p: PadParam, progress: Int): Float {
        val (min, max) = p.range[0] to p.range[1]
        val t = progress / 1000f
        return if (isTime(p) && min > 0f) (min * Math.pow((max / min).toDouble(), t.toDouble())).toFloat() else min + (max - min) * t
    }

    private fun toProgress(p: PadParam, v: Float): Int {
        val (min, max) = p.range[0] to p.range[1]
        val t = if (isTime(p) && min > 0f) (Math.log((v / min).toDouble()) / Math.log((max / min).toDouble())).toFloat()
        else (v - min) / (max - min)
        return (t.coerceIn(0f, 1f) * 1000).toInt()
    }

    private fun refreshControls() {
        refreshing = true
        for ((p, pair) in sliders) pair.first.progress = toProgress(p, PadEngine.getParam(p))
        for ((p, s) in choices) s.setSelection(Math.round(PadEngine.getParam(p)).coerceIn(0, p.choices!!.size - 1))
        ui.post { refreshing = false }
        highlightKeys()
    }
}
