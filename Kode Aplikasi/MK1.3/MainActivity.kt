// Body Muscle Evaluator (BME)
// Dibuat oleh
// Michael Liebing / 18323016
// Jonathan Otto / 18323017
// Jerry Alexander Tjoa / 18323026
// Sarjana Teknik Biomedis
// Sekolah Teknik Elektro dan Informatika Rekayasa
// Institut Teknologi Bandung

// Nama Projek Android Studio
package com.example.bme

// Library
import android.graphics.Color
import android.os.Bundle
import android.view.View
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.ViewCompat
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.components.AxisBase
import com.github.mikephil.charting.components.XAxis
import com.github.mikephil.charting.data.Entry
import com.github.mikephil.charting.data.LineData
import com.github.mikephil.charting.data.LineDataSet
import com.github.mikephil.charting.formatter.ValueFormatter
import com.google.android.material.bottomnavigation.BottomNavigationView
import kotlinx.coroutines.*
import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.ServerSocket
import java.net.Socket
import java.util.concurrent.CopyOnWriteArrayList
import kotlin.math.abs
import kotlin.math.roundToInt
import org.jtransforms.fft.DoubleFFT_1D

// Fungsi Sistem
class MainActivity : AppCompatActivity() {
    // Variable Nilai Awal
    private val PORT = 8080
    private val DEFAULT_Y_MIN = -1650f
    private val DEFAULT_Y_MAX = 1650f
    private val SAMPLING_RATE = 2500f
    private val DEFAULT_CAMERA_SEC = 4
    private val SPIKE_IGNORE_DUR = 1000L
    private val WINDOW_SIZE = 500
    private val connectedClients = CopyOnWriteArrayList<Socket>()
    private var serverJob: Job? = null

    // Variabel Plotter
    private val entries1Raw = ArrayList<Entry>()
    private val entries1Rect = ArrayList<Entry>()
    private val entries2Raw = ArrayList<Entry>()
    private val entries2Rect = ArrayList<Entry>()
    private lateinit var lineDataSet1: LineDataSet
    private lateinit var lineDataSet2: LineDataSet
    private var timeIndex1Raw = 0f
    private var timeIndex1Rect = 0f
    private var timeIndex2Raw = 0f
    private var timeIndex2Rect = 0f
    private var lastPlotUpdate = 0L
    private var cameraLength = DEFAULT_CAMERA_SEC * SAMPLING_RATE
    private var maxBuffer = 26000
    private var isFullScreen = false
    private var mosfetOnTime: Long = 0
    private var esp1Colors = intArrayOf(0, 0, 0, 0, 0, 0)
    private var esp2Colors = intArrayOf(0, 0, 0, 0, 0, 0)

    // Variabel Layout
    private lateinit var layoutHome: View
    private lateinit var layoutPlot: View
    private lateinit var layoutSettings: View
    private lateinit var tvBattery1: TextView
    private lateinit var tvBattery2: TextView
    private lateinit var btnStartServer: Button
    private lateinit var tvConsole: TextView
    private lateinit var lineChart: LineChart
    private lateinit var swViewMode: Switch
    private lateinit var swRectifyMode: Switch
    private lateinit var rgPlotSelect: RadioGroup
    private lateinit var rlChartContainer: RelativeLayout
    private lateinit var svConsole: ScrollView
    private lateinit var btnFullScreen: Button
    private lateinit var spnEspSelect: Spinner

    // Variabel Animasi Buffering
    private lateinit var pbBuffering: ProgressBar

    // Variabel Model Pembelajaran Mesin
    private var isLoadEstimationEnabled = false
    private lateinit var cvEstimasiBeban: View
    private lateinit var tvEstimasiBeban: TextView
    private val mlBuffer = ArrayList<Float>()
    private val history = mutableListOf<Double>()

    // Variabel Model Pengukuran Berat Beban
    private val meanWL = 2007.68813605
    private val scaleWL = 1032.42894477
    private val meanMNF = 69.22652728
    private val scaleMNF = 10.97181885
    private val beratbeban_movingaverage_sample = 40.0

    // Variabel Indikator Kontraksi Otot
    private lateinit var cvOnsetIndicator: androidx.cardview.widget.CardView
    private lateinit var tvOnsetIndicator: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        supportActionBar?.hide()
        window.statusBarColor = Color.parseColor("#121212")
        WindowCompat.getInsetsController(window, window.decorView).isAppearanceLightStatusBars = false
        setContentView(R.layout.activity_main)
        val container = findViewById<FrameLayout>(R.id.fragment_container)
        ViewCompat.setOnApplyWindowInsetsListener(container) { view, insets ->
            val statusBar = insets.getInsets(WindowInsetsCompat.Type.statusBars())
            view.setPadding(view.paddingLeft, statusBar.top, view.paddingRight, view.paddingBottom)
            insets
        }
        val bottomNav = findViewById<BottomNavigationView>(R.id.bottom_navigation)
        layoutHome = layoutInflater.inflate(R.layout.layout_home, null)
        layoutPlot = layoutInflater.inflate(R.layout.layout_plot, null)
        layoutSettings = layoutInflater.inflate(R.layout.layout_settings, null)
        container.addView(layoutHome)
        container.addView(layoutPlot)
        container.addView(layoutSettings)
        setupHomeUI()
        setupPlotUI()
        setupSettingsUI()
        showTab("home")
        bottomNav.setOnItemSelectedListener { item ->
            when (item.itemId) {
                R.id.nav_home -> { showTab("home"); sendCommandToAll("MOS:0"); true }
                R.id.nav_plot -> {
                    showTab("plot")
                    sendCommandToAll("MOS:1")
                    mosfetOnTime = System.currentTimeMillis()
                    cvEstimasiBeban.visibility = if (isLoadEstimationEnabled) View.VISIBLE else View.GONE
                    timeIndex1Raw = 0f
                    timeIndex1Rect = 0f
                    timeIndex2Raw = 0f
                    timeIndex2Rect = 0f
                    entries1Raw.clear()
                    entries1Rect.clear()
                    entries2Raw.clear()
                    entries2Rect.clear()
                    mlBuffer.clear()
                    entries1Raw.add(Entry(timeIndex1Raw++, 0f))
                    entries1Rect.add(Entry(timeIndex1Rect++, 0f))
                    entries2Raw.add(Entry(timeIndex2Raw++, 0f))
                    entries2Rect.add(Entry(timeIndex2Rect++, 0f))
                    lineChart.fitScreen()
                    lineChart.data.notifyDataChanged()
                    lineChart.notifyDataSetChanged()
                    lineChart.invalidate()
                    pbBuffering.visibility = View.GONE
                    lineChart.visibility = View.VISIBLE
                    true
                }
                R.id.nav_settings -> { showTab("settings"); sendCommandToAll("MOS:0"); true }
                else -> false
            }
        }
        if (serverJob == null || serverJob?.isActive == false) startServer()
    }

    private fun showTab(tab: String) {
        layoutHome.visibility = if (tab == "home") View.VISIBLE else View.GONE
        layoutPlot.visibility = if (tab == "plot") View.VISIBLE else View.GONE
        layoutSettings.visibility = if (tab == "settings") View.VISIBLE else View.GONE
    }

    private fun setupHomeUI() {
        btnStartServer = layoutHome.findViewById(R.id.btnStartServer)
        tvBattery1 = layoutHome.findViewById(R.id.tvBattery1)
        tvBattery2 = layoutHome.findViewById(R.id.tvBattery2) ?: tvBattery1
        val etCamera = layoutHome.findViewById<EditText>(R.id.etCameraLength)
        layoutHome.findViewById<Button>(R.id.btnSetRange)?.setOnClickListener {
            val minInt = (layoutHome.findViewById<EditText>(R.id.etMinPlot).text.toString().replace(",", ".").toFloatOrNull() ?: DEFAULT_Y_MIN).roundToInt()
            val maxInt = (layoutHome.findViewById<EditText>(R.id.etMaxPlot).text.toString().replace(",", ".").toFloatOrNull() ?: DEFAULT_Y_MAX).roundToInt()
            if (maxInt > minInt) {
                lineChart.axisLeft.axisMinimum = minInt.toFloat()
                lineChart.axisLeft.axisMaximum = maxInt.toFloat()
                lineChart.invalidate()
                Toast.makeText(this, "Rentang magnitudo diubah", Toast.LENGTH_SHORT).show()
                layoutHome.findViewById<EditText>(R.id.etMinPlot).setText(minInt.toString())
                layoutHome.findViewById<EditText>(R.id.etMaxPlot).setText(maxInt.toString())
            } else {
                Toast.makeText(this, "Maksimum harus lebih besar dari minimum", Toast.LENGTH_SHORT).show()
            }
        }
        layoutHome.findViewById<Button>(R.id.btnSetCamera)?.setOnClickListener {
            val newCamSeconds = etCamera?.text.toString().toIntOrNull()
            if (newCamSeconds != null && newCamSeconds > 0) {
                cameraLength = (newCamSeconds * SAMPLING_RATE)
                maxBuffer = maxOf((cameraLength * 1.2f).toInt(), 5000)
                lineChart.setVisibleXRangeMaximum(cameraLength)
                Toast.makeText(this, "Lebar kamera diatur ke $newCamSeconds detik", Toast.LENGTH_SHORT).show()
            }
        }
        btnStartServer.setOnClickListener {
            if (connectedClients.isNotEmpty()) {
                btnStartServer.text = "BME Connected"; btnStartServer.isEnabled = false
            } else {
                Toast.makeText(this, "Belum ada BME yang terhubung", Toast.LENGTH_SHORT).show()
                if (serverJob == null || serverJob?.isActive == false) startServer()
            }
        }
        val sliders = arrayOf(R.id.sbBrightness1, R.id.sbBrightness2, R.id.sbBrightness3)
        val texts = arrayOf(R.id.tvBrightness1, R.id.tvBrightness2, R.id.tvBrightness3)
        val colorLabels = arrayOf("LED Merah (R)", "LED Hijau (G)", "LED Biru (B)")
        val pwmCommands = arrayOf("PWM1", "PWM2", "PWM3")
        for (i in 0..2) {
            val sb = layoutHome.findViewById<SeekBar>(sliders[i])
            val tv = layoutHome.findViewById<TextView>(texts[i])
            sb?.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: SeekBar?, p: Int, f: Boolean) { tv?.text = "${colorLabels[i]}: $p" }
                override fun onStartTrackingTouch(s: SeekBar?) {}
                override fun onStopTrackingTouch(s: SeekBar?) { sendCommandToAll("${pwmCommands[i]}:${s?.progress ?: 0}") }
            })
        }
    }

    private fun setupSettingsUI() {
        spnEspSelect = layoutSettings.findViewById(R.id.spnEspSelect)
        val adapter = ArrayAdapter(this, android.R.layout.simple_spinner_item, arrayOf("Konfigurasi BME 1", "Konfigurasi BME 2"))
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        spnEspSelect.adapter = adapter
        spnEspSelect.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
                updateSlidersFromMemory(position + 1)
            }
            override fun onNothingSelected(parent: AdapterView<*>?) {}
        }
        val sliderIds = arrayOf(R.id.sbSrchR, R.id.sbSrchG, R.id.sbSrchB, R.id.sbConnR, R.id.sbConnG, R.id.sbConnB)
        val textIds = arrayOf(R.id.tvSrchR, R.id.tvSrchG, R.id.tvSrchB, R.id.tvConnR, R.id.tvConnG, R.id.tvConnB)
        val colorNames = arrayOf("Red", "Green", "Blue", "Red", "Green", "Blue")
        val commands = arrayOf("SR_R", "SR_G", "SR_B", "CN_R", "CN_G", "CN_B")
        for (i in 0..5) {
            val sb = layoutSettings.findViewById<SeekBar>(sliderIds[i])
            val tv = layoutSettings.findViewById<TextView>(textIds[i])
            sb?.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: SeekBar?, p: Int, fromUser: Boolean) {
                    tv?.text = "${colorNames[i]}: $p"
                    if (fromUser) {
                        val espId = spnEspSelect.selectedItemPosition + 1
                        if (espId == 1) esp1Colors[i] = p else esp2Colors[i] = p
                    }
                }
                override fun onStartTrackingTouch(s: SeekBar?) {}
                override fun onStopTrackingTouch(s: SeekBar?) {
                    val espId = spnEspSelect.selectedItemPosition + 1
                    sendCommandToAll("${espId}_${commands[i]}:${s?.progress ?: 0}")
                }
            })
        }
        val swFatigueMode = layoutSettings.findViewById<Switch>(R.id.swFatigueMode)
        swFatigueMode?.setOnCheckedChangeListener { _, isChecked ->
            if (isChecked) Toast.makeText(this, "Pengukuran Kelelahan Otot : ON", Toast.LENGTH_SHORT).show()
            else Toast.makeText(this, "Pengukuran Kelelahan Otot : OFF", Toast.LENGTH_SHORT).show()
        }
        val swLoadEstimation = layoutSettings.findViewById<Switch>(R.id.swLoadEstimation)
        swLoadEstimation?.setOnCheckedChangeListener { _, isChecked ->
            isLoadEstimationEnabled = isChecked
            if (!isChecked) mlBuffer.clear()
        }
    }

    private fun updateSlidersFromMemory(espId: Int) {
        val colors = if (espId == 1) esp1Colors else esp2Colors
        val sliderIds = arrayOf(R.id.sbSrchR, R.id.sbSrchG, R.id.sbSrchB, R.id.sbConnR, R.id.sbConnG, R.id.sbConnB)
        for (i in 0..5) {
            val sb = layoutSettings.findViewById<SeekBar>(sliderIds[i])
            sb?.progress = colors[i]
        }
    }

    private fun syncColorsFromESP(espId: Int, data: String) {
        val parts = data.split(",")
        if (parts.size == 6) {
            val colors = if (espId == 1) esp1Colors else esp2Colors
            for (i in 0..5) colors[i] = parts[i].trim().toIntOrNull() ?: 0
            runOnUiThread {
                if (spnEspSelect.selectedItemPosition + 1 == espId) {
                    updateSlidersFromMemory(espId)
                }
            }
        }
    }

    private fun setupPlotUI() {
        tvConsole = layoutPlot.findViewById(R.id.tvConsole)
        lineChart = layoutPlot.findViewById(R.id.lineChart)
        swViewMode = layoutPlot.findViewById(R.id.swViewMode)
        swRectifyMode = layoutPlot.findViewById(R.id.swRectifyMode)
        rgPlotSelect = layoutPlot.findViewById(R.id.rgPlotSelect)
        rlChartContainer = layoutPlot.findViewById(R.id.rlChartContainer)
        svConsole = layoutPlot.findViewById(R.id.svConsole)
        btnFullScreen = layoutPlot.findViewById(R.id.btnFullScreen)
        btnFullScreen.textSize = 12f
        btnFullScreen.setPadding(8, 0, 8, 0)
        btnFullScreen.text = "Full Screen"
        cvEstimasiBeban = layoutPlot.findViewById(R.id.cvEstimasiBeban)
        tvEstimasiBeban = layoutPlot.findViewById(R.id.tvEstimasiBeban)
        cvOnsetIndicator = layoutPlot.findViewById(R.id.cvOnsetIndicator)
        tvOnsetIndicator = layoutPlot.findViewById(R.id.tvOnsetIndicator)
        pbBuffering = ProgressBar(this).apply {
            isIndeterminate = true
            visibility = View.GONE
            indeterminateDrawable?.setTint(Color.parseColor("#BB86FC"))
            val sizeInDp = 80f
            val scale = resources.displayMetrics.density
            val sizeInPx = (sizeInDp * scale + 0.5f).toInt()
            val params = RelativeLayout.LayoutParams(sizeInPx, sizeInPx)
            params.addRule(RelativeLayout.CENTER_IN_PARENT)
            layoutParams = params
        }
        rlChartContainer.addView(pbBuffering)
        swViewMode.setOnCheckedChangeListener { _, isChecked ->
            rlChartContainer.visibility = if (isChecked) View.VISIBLE else View.GONE
            svConsole.visibility = if (isChecked) View.GONE else View.VISIBLE
        }
        swRectifyMode.setOnCheckedChangeListener { _, isChecked ->
            swRectifyMode.text = if (isChecked) "EMG Rectified" else "EMG Mentah"
            updatePlotVisibility()
        }
        btnFullScreen.setOnClickListener {
            isFullScreen = !isFullScreen
            val bottomNav = findViewById<BottomNavigationView>(R.id.bottom_navigation)
            if (isFullScreen) {
                supportActionBar?.hide()
                bottomNav.visibility = View.GONE; swViewMode.visibility = View.GONE; rgPlotSelect.visibility = View.GONE; swRectifyMode.visibility = View.GONE
                btnFullScreen.text = "Keluar Full Screen"
                window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY)
            } else {
                bottomNav.visibility = View.VISIBLE; swViewMode.visibility = View.VISIBLE; rgPlotSelect.visibility = View.VISIBLE; swRectifyMode.visibility = View.VISIBLE
                btnFullScreen.text = "Full Screen"
                window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_VISIBLE
            }
        }
        if (entries1Raw.isEmpty()) entries1Raw.add(Entry(timeIndex1Raw++, 0f))
        if (entries1Rect.isEmpty()) entries1Rect.add(Entry(timeIndex1Rect++, 0f))
        if (entries2Raw.isEmpty()) entries2Raw.add(Entry(timeIndex2Raw++, 0f))
        if (entries2Rect.isEmpty()) entries2Rect.add(Entry(timeIndex2Rect++, 0f))
        lineDataSet1 = LineDataSet(entries1Raw, "BME 1").apply { color = Color.RED; setDrawCircles(false); setDrawValues(false); lineWidth = 1.0f; isHighlightEnabled = false }
        lineDataSet2 = LineDataSet(entries2Raw, "BME 2").apply { color = Color.BLUE; setDrawCircles(false); setDrawValues(false); lineWidth = 1.0f; isHighlightEnabled = false }
        lineChart.data = LineData(lineDataSet1, lineDataSet2)
        lineChart.description.isEnabled = false
        lineChart.axisRight.isEnabled = false
        lineChart.setDrawBorders(false)
        lineChart.setDrawGridBackground(false)
        lineChart.setMaxVisibleValueCount(0)
        lineChart.setTouchEnabled(true)
        lineChart.isDragEnabled = true
        lineChart.isScaleXEnabled = false
        lineChart.isDragXEnabled = false
        lineChart.isScaleYEnabled = true
        lineChart.isDragYEnabled = true
        lineChart.axisLeft.axisMinimum = DEFAULT_Y_MIN
        lineChart.axisLeft.axisMaximum = DEFAULT_Y_MAX
        lineChart.xAxis.position = XAxis.XAxisPosition.TOP
        lineChart.xAxis.textColor = Color.WHITE
        lineChart.axisLeft.textColor = Color.WHITE
        lineChart.legend.textColor = Color.WHITE
        lineChart.xAxis.setLabelCount(6, true)
        lineChart.xAxis.valueFormatter = object : ValueFormatter() {
            override fun getAxisLabel(value: Float, axis: AxisBase?): String {
                return String.format("%.1f s", value / SAMPLING_RATE)
            }
        }
        rgPlotSelect.setOnCheckedChangeListener { _, _ -> updatePlotVisibility() }
    }

    private fun updatePlotVisibility() {
        val showRectified = swRectifyMode.isChecked
        val checkedId = rgPlotSelect.checkedRadioButtonId
        lineDataSet1.values = if (showRectified) entries1Rect else entries1Raw
        lineDataSet2.values = if (showRectified) entries2Rect else entries2Raw
        lineDataSet1.isVisible = (checkedId == R.id.rbEsp1 || checkedId == R.id.rbBoth)
        lineDataSet2.isVisible = (checkedId == R.id.rbEsp2 || checkedId == R.id.rbBoth)
        lineDataSet1.notifyDataSetChanged()
        lineDataSet2.notifyDataSetChanged()
        lineChart.data.notifyDataChanged()
        lineChart.notifyDataSetChanged()
        lineChart.invalidate()
    }

    private fun sendCommandToAll(cmd: String) {
        CoroutineScope(Dispatchers.IO).launch {
            for (socket in connectedClients) {
                try { if (socket.isConnected && !socket.isClosed) { socket.getOutputStream().write(("$cmd\n").toByteArray()); socket.getOutputStream().flush() } } catch (e: Exception) { }
            }
        }
    }

    private fun startServer() {
        serverJob = CoroutineScope(Dispatchers.IO).launch {
            try {
                val ss = ServerSocket(PORT)
                withContext(Dispatchers.Main) { tvConsole.append("Server dimulai...\n") }
                while (isActive) {
                    val client = ss.accept()
                    connectedClients.add(client)
                    withContext(Dispatchers.Main) { btnStartServer.text = "BME Terhubung"; btnStartServer.isEnabled = false }
                    launch {
                        delay(500)
                        val mosfetCmd = if (layoutPlot.visibility == View.VISIBLE) { mosfetOnTime = System.currentTimeMillis(); "MOS:1\n" } else { "MOS:0\n" }
                        try { client.getOutputStream().write(mosfetCmd.toByteArray()); client.getOutputStream().flush() } catch (e: Exception) {}
                        readClientData(client)
                    }
                }
            } catch (e: Exception) { }
        }
    }

    private suspend fun CoroutineScope.readClientData(client: Socket) {
        try {
            val reader = BufferedReader(InputStreamReader(client.inputStream))
            while (isActive && client.isConnected) {
                val line = reader.readLine() ?: break
                withContext(Dispatchers.Main) {
                    if (layoutPlot.visibility == View.VISIBLE && !swViewMode.isChecked) {
                        tvConsole.append("$line\n")
                        if (tvConsole.text.length > 3000) tvConsole.text = tvConsole.text.substring(1500)
                    }
                    when {
                        line.startsWith("B1:") -> tvBattery1.text = "Bat 1: ${line.substring(3).trim()}%"
                        line.startsWith("B2:") -> tvBattery2.text = "Bat 2: ${line.substring(3).trim()}%"
                        line.startsWith("E1:") -> updateChart(1, line.substring(3), false)
                        line.startsWith("R1:") -> updateChart(1, line.substring(3), true)
                        line.startsWith("E2:") -> updateChart(2, line.substring(3), false)
                        line.startsWith("R2:") -> updateChart(2, line.substring(3), true)
                        line.startsWith("O1:") -> updateOnsetIndicator(1, line.substring(3))
                        line.startsWith("O2:") -> updateOnsetIndicator(2, line.substring(3))
                        line.startsWith("SYNC1:") -> syncColorsFromESP(1, line.substring(6))
                        line.startsWith("SYNC2:") -> syncColorsFromESP(2, line.substring(6))
                        else -> { if (layoutPlot.visibility == View.VISIBLE && !swViewMode.isChecked && !line.startsWith("SYNC")) tvConsole.append("Unknown: $line\n") }
                    }
                }
            }
        } catch (e: Exception) {
        } finally {
            connectedClients.remove(client); client.close()
            withContext(Dispatchers.Main) { if (connectedClients.isEmpty()) { btnStartServer.text = "Hubungkan ke BME"; btnStartServer.isEnabled = true } }
        }
    }

    private fun updateChart(espId: Int, valueStr: String, isRectified: Boolean) {
        val nowTime = System.currentTimeMillis()
        if (nowTime - mosfetOnTime < SPIKE_IGNORE_DUR) {
            if (pbBuffering.visibility == View.GONE) {
                pbBuffering.visibility = View.VISIBLE
                lineChart.visibility = View.INVISIBLE
            }
            return
        } else if (pbBuffering.visibility == View.VISIBLE) {
            pbBuffering.visibility = View.GONE
            lineChart.visibility = View.VISIBLE
        }
        val valuesArray = valueStr.split(",")
        var addedCount = 0
        for (vStr in valuesArray) {
            val value = vStr.trim().replace(",", ".").toFloatOrNull() ?: continue
            addedCount++
            if (espId == 1) {
                if (isRectified) {
                    entries1Rect.add(Entry(timeIndex1Rect++, value))
                } else {
                    entries1Raw.add(Entry(timeIndex1Raw++, value))
                    if (isLoadEstimationEnabled) {
                        mlBuffer.add(value)
                        if (mlBuffer.size >= WINDOW_SIZE) {
                            val dataArray = mlBuffer.toFloatArray()
                            mlBuffer.clear()
                            processMLPrediction(dataArray)
                        }
                    }
                }
            } else {
                if (isRectified) entries2Rect.add(Entry(timeIndex2Rect++, value))
                else entries2Raw.add(Entry(timeIndex2Raw++, value))
            }
        }
        if (addedCount == 0) return
        if (entries1Raw.size > maxBuffer) entries1Raw.subList(0, entries1Raw.size - maxBuffer).clear()
        if (entries1Rect.size > maxBuffer) entries1Rect.subList(0, entries1Rect.size - maxBuffer).clear()
        if (entries2Raw.size > maxBuffer) entries2Raw.subList(0, entries2Raw.size - maxBuffer).clear()
        if (entries2Rect.size > maxBuffer) entries2Rect.subList(0, entries2Rect.size - maxBuffer).clear()
        if (layoutPlot.visibility == View.VISIBLE && swViewMode.isChecked && (nowTime - lastPlotUpdate > 50)) {
            val isShowRectified = swRectifyMode.isChecked
            if (isRectified != isShowRectified) return
            lastPlotUpdate = nowTime
            lineDataSet1.notifyDataSetChanged()
            lineDataSet2.notifyDataSetChanged()
            lineChart.data.notifyDataChanged()
            lineChart.notifyDataSetChanged()
            val refTime1 = if (isShowRectified) timeIndex1Rect else timeIndex1Raw
            val refTime2 = if (isShowRectified) timeIndex2Rect else timeIndex2Raw
            val maxTime = maxOf(refTime1, refTime2)
            lineChart.setVisibleXRangeMaximum(cameraLength)
            val scrollTarget = if (maxTime > cameraLength) maxTime - cameraLength else 0f
            lineChart.moveViewToX(scrollTarget)
            lineChart.invalidate()
        }
    }

    private fun updateOnsetIndicator(espId: Int, valueStr: String) {
        val isActive = valueStr.trim() == "1"
        cvOnsetIndicator.visibility = View.VISIBLE
        if (isActive) {
            tvOnsetIndicator.text = "KONTRAKSI"
            cvOnsetIndicator.setCardBackgroundColor(Color.parseColor("#4CAF50")) // Hijau
        } else {
            tvOnsetIndicator.text = "RELAKSASI"
            cvOnsetIndicator.setCardBackgroundColor(Color.parseColor("#F44336")) // Merah
        }
    }

    private fun calculateMNF(data: FloatArray, fs: Double): Double {
        val n = data.size
        val fftData = DoubleArray(n * 2)
        for (i in data.indices) fftData[i] = data[i].toDouble()
        val fft = DoubleFFT_1D(n.toLong())
        fft.realForward(fftData)
        val powerSpectrum = DoubleArray(n / 2)
        for (i in 0 until n / 2) {
            val real = fftData[2 * i]
            val imag = fftData[2 * i + 1]
            powerSpectrum[i] = (real * real) + (imag * imag)
        }
        var sumFreqPower = 0.0
        var sumPower = 0.0
        for (i in powerSpectrum.indices) {
            val freq = i * fs / n
            sumFreqPower += freq * powerSpectrum[i]
            sumPower += powerSpectrum[i]
        }
        return if (sumPower > 0) sumFreqPower / sumPower else 0.0
    }

    private fun processMLPrediction(data: FloatArray) {
        CoroutineScope(Dispatchers.Default).launch {
            try {
                val n = data.size
                if (n == 0) return@launch
                var wl = 0.0
                for (i in 1 until n) {
                    wl += abs(data[i] - data[i - 1]).toDouble()
                }
                val mnf = calculateMNF(data, 2500.0)
                val scaledWL = (wl - meanWL) / scaleWL
                val scaledMNF = (mnf - meanMNF) / scaleMNF
                val featuresArray = doubleArrayOf(scaledWL, scaledMNF)
                val predictedWeight = ModelBebanRF.score(featuresArray)
                val finalWeight = if (predictedWeight < 0.0) 0.0 else predictedWeight
                history.add(finalWeight)
                if (history.size > beratbeban_movingaverage_sample) history.removeAt(0)
                val smoothedWeight = history.average()
                withContext(Dispatchers.Main) {
                    tvEstimasiBeban.text = String.format("%.1f Kg", smoothedWeight)
                }
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }
    }
    override fun onDestroy() { super.onDestroy(); serverJob?.cancel() }
}
