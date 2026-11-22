package com.eleonardop.pad.app

import android.Manifest
import android.animation.LayoutTransition
import android.animation.ObjectAnimator
import android.animation.ValueAnimator
import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.pm.PackageManager
import android.content.res.ColorStateList
import android.content.res.Configuration
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.TextUtils
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.WindowManager
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.FrameLayout
import android.widget.GridLayout
import android.widget.HorizontalScrollView
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
 * Tela do PAD Worship (v0.6).
 *
 * Organização (a mesma lógica em qualquer tamanho de tela):
 *  - topo: nome, estado (parado/tocando, som, tom) e "Sair";
 *  - biblioteca: categorias em abas e os sons da categoria em cartões; o som ativo em dourado;
 *  - controles principais (6) e "Avançado" recolhido (versões anteriores, todos os parâmetros,
 *    desempenho, créditos);
 *  - painel de execução sempre visível: tipo de acorde, as 12 tonalidades, SWELL e PARAR.
 *    No celular em pé ele fica embaixo (alcance do polegar); deitado ou em tablet, à direita.
 *
 * Ciclo de vida: ver [PadEngine] e [PadPlaybackService]. Girar a tela não reinicia a tela nem o
 * áudio (configChanges no manifesto): a interface é só remontada.
 */
class MainActivity : Activity() {

    private val ui = Handler(Looper.getMainLooper())

    // ---- estado (sobrevive à remontagem da interface)
    private var chordType = ChordType.PAD5
    private var currentKey = -1
    private var currentPreset = -1
    private var currentCategory = 0
    private var advancedOpen = false
    private var busy = false
    private var refreshing = false
    private var worstLoad = 0f
    private var ready = false
    private var exiting = false
    private var wasPlaying = false

    // ---- vistas (recriadas em buildUi)
    private lateinit var statusText: TextView
    private lateinit var statusDot: View
    private lateinit var description: TextView
    private lateinit var perf: TextView
    private lateinit var advanced: LinearLayout
    private lateinit var advancedToggle: TextView
    private lateinit var presetGrid: GridLayout
    private val categoryTabs = mutableListOf<TextView>()
    private val sliders = mutableMapOf<PadParam, Pair<SeekBar, TextView>>()
    private val choices = mutableMapOf<PadParam, Spinner>()
    private val presetCards = mutableMapOf<Int, LinearLayout>()
    private val keyButtons = mutableListOf<TextView>()
    private val chordChips = mutableListOf<TextView>()
    private var pulse: ObjectAnimator? = null

    // ---- identidade visual
    private val cBg = Color.rgb(13, 15, 19)
    private val cSurface = Color.rgb(22, 25, 31)
    private val cSurface2 = Color.rgb(31, 35, 43)
    private val cStroke = Color.rgb(44, 49, 60)
    private val cText = Color.rgb(242, 239, 232)
    private val cDim = Color.rgb(150, 156, 168)
    private val cFaint = Color.rgb(98, 104, 116)
    private val cGold = Color.rgb(227, 184, 104)
    private val cGoldDeep = Color.rgb(58, 48, 30)
    private val cStop = Color.rgb(120, 52, 48)
    private val cPlaying = Color.rgb(132, 204, 150)

    private val mainParams = listOf(PadParam.MASTER_VOLUME, PadParam.REVERB, PadParam.TONE_CUT, PadParam.INTENSITY, PadParam.EVOLUTION, PadParam.ACTIVITY)
    private val exitListener: () -> Unit = { if (!isFinishing) finishAndRemoveTask() }

    // =============================================================== ciclo de vida

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        PadEngine.init(this)
        PadEngine.addExitListener(exitListener)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        window.statusBarColor = cBg
        window.navigationBarColor = cSurface
        setContentView(buildUi())
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
        }
    }

    override fun onStart() {
        super.onStart()
        PadPlaybackService.uiVisible = true
        ui.removeCallbacks(suspendWhenIdle)
        openAudio()
        ui.post(statsTick)
    }

    override fun onStop() {
        PadPlaybackService.uiVisible = false
        ui.removeCallbacks(statsTick)
        stopPulse()
        // Em segundo plano e sem som: depois de 10 s fecha a saída de áudio (o callback para,
        // CPU zero). Os sons continuam carregados para voltar na hora. Tocando, nada muda:
        // o serviço em primeiro plano mantém o som (culto com a tela desligada).
        if (!exiting) ui.postDelayed(suspendWhenIdle, 10_000)
        super.onStop()
    }

    override fun onDestroy() {
        ui.removeCallbacksAndMessages(null)
        PadEngine.removeExitListener(exitListener)
        // Tela fechada de vez (voltar, deslizar nos recentes) sem som tocando: libera tudo.
        if (isFinishing && !isChangingConfigurations && !PadPlaybackService.running) PadEngine.release()
        super.onDestroy()
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        setContentView(buildUi())
        if (ready) syncAll()
    }

    @Deprecated("Botão voltar do sistema")
    override fun onBackPressed() {
        if (PadEngine.isPlaying()) {
            // Tocando: voltar só minimiza (o pad continua). Sair é explícito.
            moveTaskToBack(true)
            Toast.makeText(this, "O pad continua tocando. Para encerrar, toque em Sair.", Toast.LENGTH_LONG).show()
        } else {
            exitApp()
        }
    }

    private val suspendWhenIdle = Runnable {
        if (!PadPlaybackService.uiVisible && !PadEngine.isPlaying()) {
            PadEngine.suspendAudio()
            ready = false
        }
    }

    private fun openAudio() {
        if (PadEngine.isStarted && ready) return
        if (!ready) setStatus("Preparando os sons…", false)
        PadEngine.start { ok ->
            if (exiting) return@start
            if (!ok) { setStatus("Não foi possível abrir a saída de áudio.", false); return@start }
            val first = currentPreset < 0
            ready = true
            if (first) {
                val lib = PadEngine.library
                selectPreset(lib.firstOrNull()?.second?.firstOrNull() ?: 0)
            } else {
                // só a saída de áudio foi reaberta (o motor manteve preset e ajustes)
                syncAll()
            }
        }
    }

    private fun exitApp() {
        if (exiting) return
        exiting = true
        setStatus("Encerrando…", false)
        currentKey = -1
        PadPlaybackService.shutdown(this)
        finishAndRemoveTask()
    }

    // =============================================================== atualização

    private val statsTick = object : Runnable {
        override fun run() {
            if (PadEngine.isStarted && !busy && ready) {
                val s = PadEngine.stats()
                worstLoad = maxOf(worstLoad, s.loadPeak)
                val name = if (currentPreset >= 0) PadEngine.presetNames[currentPreset] else ""
                val key = if (currentKey >= 0) "${PadEngine.noteNames[currentKey]} ${chordType.label}" else null
                setStatus(if (s.playing) "Tocando  ·  $name${key?.let { "  ·  $it" } ?: ""}" else "Parado  ·  $name", s.playing)
                if (!s.playing && wasPlaying && currentKey >= 0) { currentKey = -1; highlightKeys() }
                wasPlaying = s.playing
                if (advancedOpen) {
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

    private fun setStatus(text: String, playing: Boolean) {
        if (!::statusText.isInitialized) return
        statusText.text = text
        (statusDot.background as? GradientDrawable)?.setColor(if (playing) cPlaying else cFaint)
        if (playing) startPulse() else stopPulse()
    }

    private fun startPulse() {
        if (pulse?.isRunning == true) return
        pulse = ObjectAnimator.ofFloat(statusDot, View.ALPHA, 1f, 0.35f).apply {
            duration = 1400; repeatMode = ValueAnimator.REVERSE; repeatCount = ValueAnimator.INFINITE; start()
        }
    }

    private fun stopPulse() {
        pulse?.cancel(); pulse = null
        if (::statusDot.isInitialized) statusDot.alpha = 1f
    }

    // =============================================================== construção da tela

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()
    private fun dpf(v: Float) = v * resources.displayMetrics.density
    private val medium: Typeface by lazy { Typeface.create("sans-serif-medium", Typeface.NORMAL) }

    private fun shape(fill: Int, radius: Float = 14f, stroke: Int = 0, strokeW: Int = 1) = GradientDrawable().apply {
        setColor(fill)
        cornerRadius = dpf(radius)
        if (stroke != 0) setStroke(dp(strokeW), stroke)
    }

    private fun touchable(fill: Int, radius: Float = 14f, stroke: Int = 0, strokeW: Int = 1) =
        RippleDrawable(ColorStateList.valueOf(Color.argb(40, 255, 255, 255)), shape(fill, radius, stroke, strokeW), null)

    private fun text(t: String, size: Float, color: Int = cText, face: Typeface? = null) = TextView(this).apply {
        text = t; textSize = size; setTextColor(color); face?.let { typeface = it }
    }

    private fun caps(t: String) = text(t, 11f, cDim, medium).apply { letterSpacing = 0.14f }

    /** Pressionar: leve redução (resposta tátil visual, discreta). */
    private fun pressFeedback(v: View) {
        v.setOnTouchListener { view, e ->
            when (e.actionMasked) {
                MotionEvent.ACTION_DOWN -> view.animate().scaleX(0.96f).scaleY(0.96f).setDuration(70).start()
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> view.animate().scaleX(1f).scaleY(1f).setDuration(120).start()
            }
            false
        }
    }

    private fun isWide(): Boolean {
        val c = resources.configuration
        return c.orientation == Configuration.ORIENTATION_LANDSCAPE || c.screenWidthDp >= 720
    }

    private fun buildUi(): View {
        categoryTabs.clear(); sliders.clear(); choices.clear(); presetCards.clear(); keyButtons.clear(); chordChips.clear()
        stopPulse()
        val wide = isWide()
        val pad = dp(if (resources.configuration.screenWidthDp >= 600) 24 else 16)

        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setBackgroundColor(cBg) }
        root.addView(buildHeader(pad))

        val library = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, 0, pad, dp(24))
            layoutTransition = LayoutTransition().apply { setDuration(180) }
        }
        buildLibrary(library, wide)
        buildControls(library)
        val scroll = ScrollView(this).apply { isFillViewport = true; isVerticalScrollBarEnabled = false; addView(library) }

        val perform = buildPerformance(pad, wide)
        if (wide) {
            val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
            row.addView(scroll, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1.25f))
            row.addView(perform, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f))
            root.addView(row, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        } else {
            root.addView(scroll, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
            root.addView(perform, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        }

        // Android 15 desenha atrás das barras do sistema: respeitar as áreas seguras.
        val frame = FrameLayout(this).apply { setBackgroundColor(cBg); addView(root) }
        frame.setOnApplyWindowInsetsListener { _, insets ->
            val (l, t, r, b) = if (Build.VERSION.SDK_INT >= 30) {
                val i = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
                listOf(i.left, i.top, i.right, i.bottom)
            } else {
                @Suppress("DEPRECATION") listOf(insets.systemWindowInsetLeft, insets.systemWindowInsetTop, insets.systemWindowInsetRight, insets.systemWindowInsetBottom)
            }
            root.setPadding(l, t, r, 0)
            perform.setPadding(perform.paddingLeft, perform.paddingTop, perform.paddingRight, (perform.tag as Int) + b)
            insets
        }
        return frame
    }

    private fun buildHeader(pad: Int): View {
        val h = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(pad, dp(12), pad, dp(10))
        }
        val titles = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        titles.addView(text("PAD Worship", 21f, cText, medium).apply { letterSpacing = 0.01f })
        val st = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; setPadding(0, dp(4), 0, 0) }
        statusDot = View(this).apply { background = GradientDrawable().apply { shape = GradientDrawable.OVAL; setColor(cFaint) } }
        st.addView(statusDot, LinearLayout.LayoutParams(dp(8), dp(8)).apply { rightMargin = dp(8) })
        statusText = text("", 13f, cDim).apply { maxLines = 1; ellipsize = TextUtils.TruncateAt.END }
        st.addView(statusText)
        titles.addView(st)
        h.addView(titles, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        val exit = text("Sair", 14f, cDim, medium).apply {
            gravity = Gravity.CENTER
            setPadding(dp(16), dp(8), dp(16), dp(8))
            background = touchable(cSurface, 18f, cStroke)
            contentDescription = "Sair do aplicativo"
            setOnClickListener { confirmExit() }
        }
        h.addView(exit, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(40)))
        return h
    }

    private fun confirmExit() {
        if (!PadEngine.isPlaying()) { exitApp(); return }
        AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
            .setTitle("Encerrar o PAD Worship?")
            .setMessage("O som vai parar e o aplicativo será fechado.")
            .setPositiveButton("Encerrar") { _, _ -> exitApp() }
            .setNegativeButton("Continuar tocando", null)
            .show()
    }

    // ---- biblioteca

    private fun buildLibrary(box: LinearLayout, wide: Boolean) {
        val lib = PadEngine.library
        if (currentPreset >= 0) lib.indexOfFirst { currentPreset in it.second }.takeIf { it >= 0 }?.let { currentCategory = it }
        val tabsRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, dp(4), 0, dp(10)) }
        lib.forEachIndexed { i, (cat, _) ->
            val t = text(prettyCategory(cat), 13f, cDim, medium).apply {
                gravity = Gravity.CENTER
                setPadding(dp(14), 0, dp(14), 0)
                setOnClickListener { currentCategory = i; showCategory() }
            }
            categoryTabs += t
            tabsRow.addView(t, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(36)).apply { rightMargin = dp(8) })
        }
        box.addView(HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false; addView(tabsRow) })

        val cols = when {
            resources.configuration.screenWidthDp >= 900 -> 3
            wide && resources.configuration.screenWidthDp < 700 -> 2
            else -> 2
        }
        presetGrid = GridLayout(this).apply { columnCount = cols }
        box.addView(presetGrid)
        description = text("", 13f, cDim).apply { setPadding(dp(2), dp(10), dp(2), dp(4)); setLineSpacing(0f, 1.15f) }
        box.addView(description)
        showCategory()
    }

    private fun prettyCategory(c: String) = when (c) {
        "SOFT PADS" -> "Soft Pads"
        "FOUNDATION" -> "Foundation"
        "ATMOSPHERE" -> "Atmosphere"
        "CHARACTER" -> "Character"
        "TEXTURES" -> "Textures"
        else -> c
    }

    /** Descrição curta para o cartão: o texto depois de "CATEGORIA — " até o primeiro ";" ou ":". */
    private fun shortDescription(i: Int): String {
        val d = PadEngine.presetDescription(i).substringAfter("— ")
        return d.split(';', ':').first().trim().replaceFirstChar { it.uppercase() }
    }

    private fun fullDescription(i: Int) = PadEngine.presetDescription(i).substringAfter("— ").replaceFirstChar { it.uppercase() }

    private fun showCategory() {
        val lib = PadEngine.library
        if (lib.isEmpty()) return
        currentCategory = currentCategory.coerceIn(0, lib.size - 1)
        categoryTabs.forEachIndexed { i, t ->
            val on = i == currentCategory
            t.background = touchable(if (on) cGoldDeep else cSurface, 18f, if (on) cGold else cStroke)
            t.setTextColor(if (on) cGold else cDim)
        }
        presetGrid.removeAllViews()
        presetCards.clear()
        for (i in lib[currentCategory].second) {
            val card = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(dp(14), dp(12), dp(14), dp(12))
                isClickable = true
                setOnClickListener { selectPreset(i) }
                contentDescription = PadEngine.presetNames[i]
            }
            card.addView(text(PadEngine.presetNames[i], 16f, cText, medium).apply { maxLines = 1; ellipsize = TextUtils.TruncateAt.END })
            card.addView(text(shortDescription(i), 12f, cDim).apply { maxLines = 2; ellipsize = TextUtils.TruncateAt.END; setPadding(0, dp(4), 0, 0) })
            pressFeedback(card)
            presetCards[i] = card
            presetGrid.addView(card, GridLayout.LayoutParams().apply {
                width = 0; height = dp(84)
                columnSpec = GridLayout.spec(GridLayout.UNDEFINED, 1f)
                setMargins(dp(4), dp(4), dp(4), dp(4))
            })
            card.alpha = 0f
            card.animate().alpha(1f).setDuration(160).start()
        }
        paintCards()
    }

    private fun paintCards() {
        for ((i, c) in presetCards) {
            val on = i == currentPreset
            c.background = touchable(if (on) cGoldDeep else cSurface, 14f, if (on) cGold else cStroke, if (on) 2 else 1)
            (c.getChildAt(0) as TextView).setTextColor(if (on) cGold else cText)
        }
    }

    // ---- controles

    private fun buildControls(box: LinearLayout) {
        box.addView(caps("CONTROLES").apply { setPadding(dp(2), dp(18), 0, dp(6)) })
        val card = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            background = shape(cSurface, 16f, cStroke)
            setPadding(dp(14), dp(8), dp(14), dp(10))
        }
        for (p in mainParams) addSlider(card, p, mainLabel(p))
        box.addView(card)

        advancedToggle = text(if (advancedOpen) "Avançado  ▾" else "Avançado  ▸", 14f, cDim, medium).apply {
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(14), 0, dp(14), 0)
            background = touchable(cSurface, 14f, cStroke)
            setOnClickListener { toggleAdvanced() }
        }
        box.addView(advancedToggle, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(48)).apply { topMargin = dp(14) })
        advanced = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            visibility = if (advancedOpen) View.VISIBLE else View.GONE
            setPadding(dp(2), dp(4), dp(2), 0)
        }
        buildAdvanced(advanced)
        box.addView(advanced)
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
        box.addView(caps("VERSÕES ANTERIORES (comparação)").apply { setPadding(0, dp(14), 0, dp(4)) })
        val legacy = PadEngine.legacyPresets
        val sp = spinner(listOf("— escolher —") + legacy.map { PadEngine.presetNames[it] })
        sp.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(a: AdapterView<*>?, v: View?, pos: Int, id: Long) {
                if (pos > 0 && !busy) selectPreset(legacy[pos - 1])
            }
            override fun onNothingSelected(a: AdapterView<*>?) {}
        }
        box.addView(sp)

        box.addView(caps("FONTES").apply { setPadding(0, dp(14), 0, dp(4)) })
        for (p in PadParam.entries.filter { it.choices != null }) {
            box.addView(text(p.label, 13f, cDim))
            val s = spinner(p.choices!!)
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
            "CARÁTER" to listOf(PadParam.VELVET, PadParam.CHORUS, PadParam.WARMTH, PadParam.BRIGHTNESS, PadParam.TONE_CUT, PadParam.AIR, PadParam.LOW,
                PadParam.WIDTH, PadParam.MOVEMENT, PadParam.DETUNE),
            "REVERB" to listOf(PadParam.REVERB_DECAY, PadParam.REVERB_SIZE, PadParam.REVERB_DAMPING, PadParam.PRE_DELAY,
                PadParam.EARLY_REFLECTIONS, PadParam.DIFFUSION, PadParam.SHIMMER),
            "ENVELOPE" to listOf(PadParam.ATTACK, PadParam.RELEASE, PadParam.CROSSFADE_TIME),
        )
        for ((title, list) in groups) {
            box.addView(caps(title).apply { setPadding(0, dp(16), 0, dp(2)) })
            for (p in list) if (p !in mainParams) addSlider(box, p, p.label)
        }

        box.addView(caps("DESEMPENHO E INFORMAÇÕES").apply { setPadding(0, dp(16), 0, dp(4)) })
        perf = text("", 12f, cDim).apply { setPadding(0, dp(2), 0, dp(8)); setLineSpacing(0f, 1.15f) }
        box.addView(perf)
        box.addView(secondaryButton("Teste de desempenho rápido (~30 s)") { benchmark(true) })
        box.addView(secondaryButton("Teste de desempenho completo (~4 min)") { benchmark(false) })
        box.addView(secondaryButton("Créditos e licenças dos sons") { showCredits() })
    }

    private fun spinner(items: List<String>) = Spinner(this).apply {
        adapter = object : ArrayAdapter<String>(this@MainActivity, android.R.layout.simple_spinner_item, items) {
            override fun getView(position: Int, convertView: View?, parent: ViewGroup): View =
                (super.getView(position, convertView, parent) as TextView).apply { setTextColor(cText); textSize = 14f }
        }.also { it.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item) }
        background = shape(cSurface, 12f, cStroke)
        setPadding(dp(8), 0, dp(8), 0)
        layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(44)).apply { topMargin = dp(4); bottomMargin = dp(6) }
    }

    private fun secondaryButton(t: String, onClick: () -> Unit) = text(t, 14f, cText).apply {
        gravity = Gravity.CENTER
        background = touchable(cSurface, 12f, cStroke)
        setOnClickListener { onClick() }
        layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(46)).apply { topMargin = dp(6) }
    }

    private fun toggleAdvanced() {
        advancedOpen = !advancedOpen
        advanced.visibility = if (advancedOpen) View.VISIBLE else View.GONE
        advancedToggle.text = if (advancedOpen) "Avançado  ▾" else "Avançado  ▸"
    }

    private fun addSlider(box: LinearLayout, p: PadParam, title: String) {
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; setPadding(0, dp(6), 0, 0) }
        val name = text(title, 14f, cText)
        val value = text("", 13f, cDim).apply { gravity = Gravity.END }
        row.addView(name, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        row.addView(value)
        val bar = SeekBar(this).apply {
            max = 1000
            progressTintList = ColorStateList.valueOf(cGold)
            thumbTintList = ColorStateList.valueOf(cGold)
            progressBackgroundTintList = ColorStateList.valueOf(cStroke)
            setPadding(dp(6), dp(6), dp(6), dp(6))
            contentDescription = title
        }
        bar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(s: SeekBar?, progress: Int, fromUser: Boolean) {
                val v = toValue(p, progress)
                if (fromUser && !busy) PadEngine.setParam(p, v)
                value.text = format(p, v)
            }
            override fun onStartTrackingTouch(s: SeekBar?) {}
            override fun onStopTrackingTouch(s: SeekBar?) {}
        })
        sliders[p] = bar to value
        box.addView(row)
        box.addView(bar, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(36)))
    }

    // ---- painel de execução

    private fun buildPerformance(pad: Int, wide: Boolean): LinearLayout {
        val panel = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            val top = dp(12)
            setPadding(pad, top, pad, dp(12))
            tag = dp(12)
            background = if (wide) shape(cSurface, 0f) else GradientDrawable().apply {
                setColor(cSurface)
                cornerRadii = floatArrayOf(dpf(22f), dpf(22f), dpf(22f), dpf(22f), 0f, 0f, 0f, 0f)
            }
            elevation = dpf(8f)
            if (wide) gravity = Gravity.CENTER_VERTICAL
        }
        // tipo de acorde
        val chips = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        for (ct in ChordType.entries) {
            val c = text(ct.label, 13f, cDim, medium).apply {
                gravity = Gravity.CENTER
                setPadding(dp(12), 0, dp(12), 0)
                setOnClickListener { chooseChord(ct) }
            }
            chordChips += c
            chips.addView(c, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(34)).apply { rightMargin = dp(6) })
        }
        panel.addView(HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false; addView(chips) })

        // 12 tonalidades: 6 × 2 em pé (dedos grandes), 4 × 3 na lateral
        val cols = if (wide) 4 else 6
        val keyH = when { wide && resources.configuration.screenHeightDp >= 700 -> 76; wide -> 60; else -> 56 }
        val grid = GridLayout(this).apply { columnCount = cols; setPadding(0, dp(10), 0, dp(6)) }
        for (k in 0 until 12) {
            val b = text(PadEngine.noteNames[k], 19f, cText, medium).apply {
                gravity = Gravity.CENTER
                contentDescription = "Tom ${PadEngine.noteNames[k]}"
                setOnClickListener { onKey(k) }
            }
            pressFeedback(b)
            keyButtons += b
            grid.addView(b, GridLayout.LayoutParams().apply {
                width = 0; height = dp(keyH)
                columnSpec = GridLayout.spec(GridLayout.UNDEFINED, 1f)
                setMargins(dp(3), dp(3), dp(3), dp(3))
            })
        }
        panel.addView(grid)

        val actions = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, dp(4), 0, 0) }
        val swell = text("SWELL", 15f, cGold, medium).apply {
            gravity = Gravity.CENTER; letterSpacing = 0.08f
            background = touchable(cGoldDeep, 14f, cGold)
            setOnClickListener { if (PadEngine.isStarted && !busy && currentKey >= 0) PadEngine.swell() }
        }
        val stop = text("PARAR", 15f, cText, medium).apply {
            gravity = Gravity.CENTER; letterSpacing = 0.08f
            background = touchable(cStop, 14f)
            setOnClickListener {
                PadEngine.stop(); currentKey = -1; highlightKeys()
                PadPlaybackService.notesStopped(this@MainActivity)
            }
        }
        pressFeedback(swell); pressFeedback(stop)
        actions.addView(swell, LinearLayout.LayoutParams(0, dp(54), 1f).apply { rightMargin = dp(6) })
        actions.addView(stop, LinearLayout.LayoutParams(0, dp(54), 1f).apply { leftMargin = dp(6) })
        panel.addView(actions)
        paintChords()
        highlightKeys()
        return panel
    }

    private fun chooseChord(ct: ChordType) {
        chordType = ct
        paintChords()
        if (!busy && currentKey >= 0 && PadEngine.isPlaying()) PadEngine.playChord(currentKey, chordType)
    }

    private fun paintChords() {
        ChordType.entries.forEachIndexed { i, ct ->
            val on = ct == chordType
            chordChips.getOrNull(i)?.apply {
                background = touchable(if (on) cGoldDeep else cSurface2, 17f, if (on) cGold else 0)
                setTextColor(if (on) cGold else cDim)
            }
        }
    }

    // =============================================================== ações

    private fun selectPreset(index: Int) {
        if (busy) return
        PadEngine.applyPreset(index)
        currentPreset = index
        PadEngine.library.indexOfFirst { index in it.second }.takeIf { it >= 0 }?.let {
            if (it != currentCategory) { currentCategory = it; showCategory() }
        }
        description.text = fullDescription(index)
        paintCards()
        presetCards[index]?.let { it.animate().scaleX(1.03f).scaleY(1.03f).setDuration(90).withEndAction { it.animate().scaleX(1f).scaleY(1f).setDuration(140).start() }.start() }
        ui.postDelayed({ refreshControls() }, 50)
    }

    private fun onKey(k: Int) {
        if (!PadEngine.isStarted || busy) { if (!ready) Toast.makeText(this, "Aguarde: preparando os sons…", Toast.LENGTH_SHORT).show(); return }
        PadPlaybackService.ensureRunning(this)
        PadEngine.playChord(k, chordType)
        currentKey = k
        highlightKeys()
    }

    private fun highlightKeys() {
        keyButtons.forEachIndexed { i, b ->
            val on = i == currentKey
            b.background = touchable(if (on) cGold else cSurface2, 14f, if (on) 0 else cStroke)
            b.setTextColor(if (on) Color.rgb(28, 22, 10) else cText)
        }
    }

    /** Reaplica na tela o estado atual (depois de girar ou de reabrir o áudio). */
    private fun syncAll() {
        // (o motor guarda o preset e os ajustes; aqui só a tela é atualizada)
        if (currentPreset >= 0) description.text = fullDescription(currentPreset)
        paintCards(); paintChords(); highlightKeys(); refreshControls()
    }

    private fun benchmark(quick: Boolean) {
        if (!PadEngine.isStarted || busy) return
        busy = true
        currentKey = -1
        highlightKeys()
        setStatus("Teste de desempenho em andamento: o som fica desligado até terminar…", false)
        val device = "Aparelho: ${Build.MANUFACTURER} ${Build.MODEL}, Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})" +
            (if (Build.VERSION.SDK_INT >= 31) ", processador ${Build.SOC_MANUFACTURER} ${Build.SOC_MODEL}" else "") +
            ", ABI ${Build.SUPPORTED_ABIS.firstOrNull() ?: "?"}\n"
        PadEngine.runBenchmark(quick) { report ->
            busy = false
            if (isFinishing) return@runBenchmark
            setStatus("Teste concluído.", false)
            if (currentPreset >= 0) selectPreset(currentPreset)
            val text = device + report +
                "\nEste teste roda fora do áudio (thread comum). A carga real do callback aparece em Avançado → Desempenho."
            AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
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
        AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
            .setTitle("Créditos e licenças")
            .setView(ScrollView(this).apply { addView(tv) })
            .setPositiveButton("Fechar", null)
            .show()
    }

    private fun pct(v: Float) = "%.0f%%".format(v * 100f)

    private fun isTime(p: PadParam) = p == PadParam.ATTACK || p == PadParam.RELEASE || p == PadParam.CROSSFADE_TIME ||
        p == PadParam.REVERB_DECAY || p == PadParam.SWELL_TIME

    private fun format(p: PadParam, v: Float) = when {
        isTime(p) || p == PadParam.PRE_DELAY -> "%.2f s".format(v)
        else -> "%.0f%%".format(100f * (v - p.range[0]) / (p.range[1] - p.range[0]))
    }

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
