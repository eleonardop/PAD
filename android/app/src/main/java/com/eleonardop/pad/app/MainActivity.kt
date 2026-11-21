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
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
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
 * BANCADA DE TESTE PROVISÓRIA do motor de áudio (etapas 1, 2 e v0.3).
 * Não é a interface do músico: serve para ouvir o motor no aparelho real, trocar
 * tons/acordes/presets, ajustar todos os parâmetros, medir o desempenho e ver os créditos
 * dos samples. A interface definitiva será construída depois sobre a mesma API [PadEngine].
 */
class MainActivity : Activity() {

    private val ui = Handler(Looper.getMainLooper())
    private lateinit var status: TextView
    private lateinit var perf: TextView
    private val sliders = mutableMapOf<PadParam, Pair<SeekBar, TextView>>()
    private val choices = mutableMapOf<PadParam, Spinner>()
    private var chordType = ChordType.PAD5
    private var currentKey = -1
    private val keyButtons = mutableListOf<Button>()
    private var busy = false          // teste de desempenho em andamento
    private var refreshing = false    // evita reenviar valores ao atualizar os controles
    private var worstLoad = 0f

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        PadEngine.init(this)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        setContentView(buildUi())
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
        }
        status.text = "Preparando o motor de áudio (gerando timbres e lendo samples)..."
        PadEngine.start { ok ->
            status.text = if (ok) "Motor pronto." else "Não foi possível abrir a saída de áudio."
            if (ok) refreshControls()
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
                val key = if (currentKey >= 0) PadEngine.noteNames[currentKey] else "-"
                status.text = "Tom: $key  ${chordType.label}   ${if (s.playing) "tocando" else "parado"}   vozes: ${s.activeVoices}"
                perf.text = "Carga do áudio: média ${pct(s.load)}, pior bloco ${pct(s.loadPeak)} (pior desde o início ${pct(worstLoad)})\n" +
                    "Saída ${s.deviceSampleRate} Hz, burst ${s.burstFrames}, buffer ${s.bufferFrames} quadros, " +
                    "falhas de buffer: ${if (s.xRuns < 0) "n/d" else s.xRuns}\n" +
                    "Fontes na memória: ${"%.0f".format(s.sourcesMemoryMb)} MB   texturas: ${s.textureVoices} vozes, " +
                    "${s.textureEvents} eventos, ${s.blooms} blooms, ${s.swells} swells   estado: ${stageName(s.lifeStage)}\n" +
                    (if (s.sourcesFailed != 0) "   (alguma fonte falhou: usando reserva)" else "")
            }
            ui.postDelayed(this, 500)
        }
    }

    private fun stageName(s: Int) = when (s) {
        1 -> "estabelecimento"; 2 -> "expansão"; 3 -> "transformação"; 4 -> "sustentação"; 5 -> "dissolução"; else -> "silêncio"
    }

    private fun pct(v: Float) = "%.0f%%".format(v * 100f)
    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    private fun section(root: LinearLayout, title: String) {
        root.addView(TextView(this).apply {
            text = title
            setTextColor(Color.rgb(230, 200, 120)); textSize = 15f; typeface = Typeface.DEFAULT_BOLD
            setPadding(0, dp(16), 0, dp(4))
        })
    }

    private fun buildUi(): View {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(16), dp(16), dp(16))
            setBackgroundColor(Color.rgb(18, 20, 24))
        }
        root.addView(TextView(this).apply {
            text = "PAD Worship — bancada de teste do motor (v0.3)"
            setTextColor(Color.WHITE); textSize = 18f; typeface = Typeface.DEFAULT_BOLD
        })
        status = TextView(this).apply { setTextColor(Color.rgb(150, 190, 255)); textSize = 13f; setPadding(0, dp(6), 0, dp(2)) }
        root.addView(status)
        perf = TextView(this).apply { setTextColor(Color.rgb(140, 150, 160)); textSize = 11f; setPadding(0, 0, 0, dp(8)) }
        root.addView(perf)

        // Preset e tipo de acorde
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val presetSpinner = Spinner(this)
        val order = PadEngine.presetDisplayOrder
        presetSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, order.map { PadEngine.presetNames[it] })
        presetSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(p: AdapterView<*>?, v: View?, pos: Int, id: Long) {
                if (busy) return
                val index = order[pos]
                PadEngine.applyPreset(index)
                ui.postDelayed({ refreshControls() }, 50)
                Toast.makeText(this@MainActivity, PadEngine.presetDescription(index), Toast.LENGTH_SHORT).show()
            }
            override fun onNothingSelected(p: AdapterView<*>?) {}
        }
        val chordSpinner = Spinner(this)
        chordSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, ChordType.entries.map { it.label })
        chordSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(p: AdapterView<*>?, v: View?, pos: Int, id: Long) {
                chordType = ChordType.entries[pos]
                if (!busy && currentKey >= 0 && PadEngine.stats().playing) PadEngine.playChord(currentKey, chordType)
            }
            override fun onNothingSelected(p: AdapterView<*>?) {}
        }
        row.addView(presetSpinner, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        row.addView(chordSpinner, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        root.addView(row)

        // 12 tonalidades
        val grid = GridLayout(this).apply { columnCount = 4; setPadding(0, dp(10), 0, dp(6)) }
        for (k in 0 until 12) {
            val b = Button(this).apply {
                text = PadEngine.noteNames[k]
                textSize = 18f
                setOnClickListener { onKey(k) }
            }
            keyButtons += b
            grid.addView(b, GridLayout.LayoutParams().apply {
                width = 0; height = dp(64)
                columnSpec = GridLayout.spec(GridLayout.UNDEFINED, 1f)
                setMargins(dp(3), dp(3), dp(3), dp(3))
            })
        }
        root.addView(grid)
        val actions = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        actions.addView(Button(this).apply {
            text = "SWELL"
            setOnClickListener { if (PadEngine.isStarted && !busy && currentKey >= 0) PadEngine.swell() }
        }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        actions.addView(Button(this).apply {
            text = "PARAR (release)"
            setOnClickListener { PadEngine.stop(); currentKey = -1; highlightKey() }
        }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        root.addView(actions)

        // Escolhas (fonte das cordas, textura, tipo de reverb)
        section(root, "Fontes e espaço")
        for (p in PadParam.entries.filter { it.choices != null }) {
            root.addView(TextView(this).apply { text = p.label; setTextColor(Color.LTGRAY); textSize = 13f })
            val sp = Spinner(this)
            sp.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, p.choices!!)
            sp.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
                override fun onItemSelected(a: AdapterView<*>?, v: View?, pos: Int, id: Long) {
                    if (!refreshing && !busy) PadEngine.setParam(p, pos.toFloat())
                }
                override fun onNothingSelected(a: AdapterView<*>?) {}
            }
            choices[p] = sp
            root.addView(sp)
        }

        // Parâmetros contínuos, agrupados
        val groups = listOf(
            "Camadas" to listOf(PadParam.LAYER_WARM, PadParam.LAYER_VOX, PadParam.LAYER_STRINGS, PadParam.LAYER_TEXTURE,
                PadParam.LAYER_AIR, PadParam.LAYER_SHIMMER, PadParam.LAYER_LOW),
            "Texturas e transições (v0.3)" to listOf(PadParam.LAYER_KEYS, PadParam.LAYER_SPARKLE, PadParam.LAYER_METAL,
                PadParam.ACTIVITY, PadParam.BLOOM, PadParam.BLOOM_CHANCE, PadParam.SWELL_TIME, PadParam.TAPE),
            "Caráter" to listOf(PadParam.EVOLUTION, PadParam.INTENSITY, PadParam.CHORUS, PadParam.WARMTH, PadParam.BRIGHTNESS,
                PadParam.AIR, PadParam.LOW, PadParam.WIDTH, PadParam.MOVEMENT, PadParam.DETUNE),
            "Reverb" to listOf(PadParam.REVERB, PadParam.REVERB_DECAY, PadParam.REVERB_SIZE, PadParam.REVERB_DAMPING,
                PadParam.PRE_DELAY, PadParam.EARLY_REFLECTIONS, PadParam.DIFFUSION, PadParam.SHIMMER),
            "Envelope e saída" to listOf(PadParam.ATTACK, PadParam.RELEASE, PadParam.CROSSFADE_TIME, PadParam.MASTER_VOLUME),
        )
        for ((title, list) in groups) {
            section(root, title)
            for (p in list) addSlider(root, p)
        }

        // Ferramentas
        section(root, "Ferramentas")
        root.addView(Button(this).apply {
            text = "Teste de desempenho rápido (~30 s)"
            setOnClickListener { benchmark(quick = true) }
        })
        root.addView(Button(this).apply {
            text = "Teste de desempenho completo (~3 min)"
            setOnClickListener { benchmark(quick = false) }
        })
        root.addView(Button(this).apply {
            text = "Créditos e licenças dos sons"
            setOnClickListener { showCredits() }
        })
        return ScrollView(this).apply { addView(root) }
    }

    private fun addSlider(root: LinearLayout, p: PadParam) {
        val label = TextView(this).apply { setTextColor(Color.LTGRAY); textSize = 13f }
        val bar = SeekBar(this).apply { max = 1000 }
        bar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(s: SeekBar?, progress: Int, fromUser: Boolean) {
                val v = toValue(p, progress)
                if (fromUser && !busy) PadEngine.setParam(p, v)
                label.text = "${p.label}: ${"%.2f".format(v)}"
            }
            override fun onStartTrackingTouch(s: SeekBar?) {}
            override fun onStopTrackingTouch(s: SeekBar?) {}
        })
        sliders[p] = bar to label
        root.addView(label)
        root.addView(bar)
    }

    private fun onKey(k: Int) {
        if (!PadEngine.isStarted || busy) return
        PadPlaybackService.ensureRunning(this) // mantém o áudio com a tela desligada
        PadEngine.playChord(k, chordType)
        currentKey = k
        highlightKey()
    }

    private fun highlightKey() {
        keyButtons.forEachIndexed { i, b ->
            b.setBackgroundColor(if (i == currentKey) Color.rgb(40, 110, 220) else Color.rgb(55, 58, 66))
            b.setTextColor(Color.WHITE)
        }
    }

    private fun benchmark(quick: Boolean) {
        if (!PadEngine.isStarted || busy) return
        busy = true
        currentKey = -1
        highlightKey()
        status.text = "Teste de desempenho em andamento: o som fica desligado até terminar..."
        val device = "Aparelho: ${Build.MANUFACTURER} ${Build.MODEL}, Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})" +
            (if (Build.VERSION.SDK_INT >= 31) ", processador ${Build.SOC_MANUFACTURER} ${Build.SOC_MODEL}" else "") +
            ", ABI ${Build.SUPPORTED_ABIS.firstOrNull() ?: "?"}\n"
        PadEngine.runBenchmark(quick) { report ->
            busy = false
            status.text = "Teste concluído."
            refreshControls()
            val text = device + report +
                "\nObservação: o teste roda num thread comum, não no thread de áudio de alta prioridade; " +
                "a carga real durante o uso aparece no topo da tela (\"Carga do áudio\")."
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

    // Parâmetros de tempo usam escala exponencial na barra (mais precisão nos valores curtos).
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
        for ((p, sp) in choices) sp.setSelection(Math.round(PadEngine.getParam(p)).coerceIn(0, p.choices!!.size - 1))
        // Os Spinners avisam a mudança depois; libera o envio só quando a fila de UI esvaziar.
        ui.post { refreshing = false }
        highlightKey()
    }
}
