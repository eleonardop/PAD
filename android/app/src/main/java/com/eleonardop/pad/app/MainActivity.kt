package com.eleonardop.pad.app

import android.Manifest
import android.app.Activity
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
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
import com.eleonardop.pad.engine.ChordType
import com.eleonardop.pad.engine.PadEngine
import com.eleonardop.pad.engine.PadParam
import com.eleonardop.pad.engine.PadPlaybackService

/**
 * BANCADA DE TESTE PROVISÓRIA do motor de áudio (etapa 1).
 * Não é a interface do músico: serve apenas para ouvir o motor no aparelho real,
 * trocar tons/acordes/presets e ajustar os parâmetros. A interface definitiva será
 * construída na próxima etapa sobre a mesma API [PadEngine].
 */
class MainActivity : Activity() {

    private val ui = Handler(Looper.getMainLooper())
    private lateinit var status: TextView
    private val sliders = mutableMapOf<PadParam, Pair<SeekBar, TextView>>()
    private var chordType = ChordType.PAD5
    private var currentKey = -1
    private val keyButtons = mutableListOf<Button>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        setContentView(buildUi())
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
        }
        status.text = "Preparando o motor de áudio..."
        PadEngine.start { ok ->
            status.text = if (ok) "Motor pronto." else "Não foi possível abrir a saída de áudio."
            if (ok) refreshSliders()
        }
        ui.post(statsTick)
    }

    override fun onDestroy() {
        ui.removeCallbacks(statsTick)
        super.onDestroy()
    }

    private val statsTick = object : Runnable {
        override fun run() {
            if (PadEngine.isStarted) {
                val s = PadEngine.stats()
                val key = if (currentKey >= 0) PadEngine.noteNames[currentKey] else "-"
                status.text = "Tom: $key  ${chordType.label}   ${if (s.playing) "tocando" else "parado"}   vozes: ${s.activeVoices}   " +
                    "saída: ${s.deviceSampleRate} Hz"
            }
            ui.postDelayed(this, 500)
        }
    }

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    private fun buildUi(): View {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(16), dp(16), dp(16))
            setBackgroundColor(Color.rgb(18, 20, 24))
        }
        root.addView(TextView(this).apply {
            text = "PAD Worship — bancada de teste do motor"
            setTextColor(Color.WHITE); textSize = 18f; typeface = Typeface.DEFAULT_BOLD
        })
        status = TextView(this).apply { setTextColor(Color.rgb(150, 190, 255)); textSize = 13f; setPadding(0, dp(6), 0, dp(10)) }
        root.addView(status)

        // Preset e tipo de acorde
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val presetSpinner = Spinner(this)
        presetSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, PadEngine.presetNames)
        presetSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(p: AdapterView<*>?, v: View?, pos: Int, id: Long) { PadEngine.applyPreset(pos); ui.postDelayed({ refreshSliders() }, 50) }
            override fun onNothingSelected(p: AdapterView<*>?) {}
        }
        val chordSpinner = Spinner(this)
        chordSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, ChordType.entries.map { it.label })
        chordSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(p: AdapterView<*>?, v: View?, pos: Int, id: Long) {
                chordType = ChordType.entries[pos]
                if (currentKey >= 0 && PadEngine.stats().playing) PadEngine.playChord(currentKey, chordType)
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
        root.addView(Button(this).apply {
            text = "PARAR (release)"
            setOnClickListener { PadEngine.stop(); currentKey = -1; highlightKey() }
        })

        // Parâmetros
        val params = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(0, dp(12), 0, 0) }
        for (p in PadParam.entries) {
            val label = TextView(this).apply { setTextColor(Color.LTGRAY); textSize = 13f }
            val bar = SeekBar(this).apply { max = 1000 }
            bar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: SeekBar?, progress: Int, fromUser: Boolean) {
                    val v = toValue(p, progress)
                    if (fromUser) PadEngine.setParam(p, v)
                    label.text = "${p.label}: ${"%.2f".format(v)}"
                }
                override fun onStartTrackingTouch(s: SeekBar?) {}
                override fun onStopTrackingTouch(s: SeekBar?) {}
            })
            sliders[p] = bar to label
            params.addView(label)
            params.addView(bar)
        }
        root.addView(params)
        return ScrollView(this).apply { addView(root) }
    }

    private fun onKey(k: Int) {
        if (!PadEngine.isStarted) return
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

    // Parâmetros de tempo usam escala exponencial na barra (mais precisão nos valores curtos).
    private fun isTime(p: PadParam) = p == PadParam.ATTACK || p == PadParam.RELEASE || p == PadParam.CROSSFADE_TIME || p == PadParam.REVERB_DECAY

    private fun toValue(p: PadParam, progress: Int): Float {
        val (min, max) = p.range[0] to p.range[1]
        val t = progress / 1000f
        return if (isTime(p)) (min * Math.pow((max / min).toDouble(), t.toDouble())).toFloat() else min + (max - min) * t
    }

    private fun toProgress(p: PadParam, v: Float): Int {
        val (min, max) = p.range[0] to p.range[1]
        val t = if (isTime(p)) (Math.log((v / min).toDouble()) / Math.log((max / min).toDouble())).toFloat() else (v - min) / (max - min)
        return (t.coerceIn(0f, 1f) * 1000).toInt()
    }

    private fun refreshSliders() {
        for ((p, pair) in sliders) pair.first.progress = toProgress(p, PadEngine.getParam(p))
        highlightKey()
    }
}
