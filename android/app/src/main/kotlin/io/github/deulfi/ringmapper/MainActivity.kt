package io.github.deulfi.ringmapper

import android.app.Activity
import android.app.AlertDialog
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.os.Build
import android.os.Bundle
import android.text.InputType
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.view.WindowInsets
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Spinner
import android.widget.TextView
import rikka.shizuku.Shizuku
import java.util.concurrent.Executors

/** Settings screen. All privileged work runs on [io]; nothing keeps running after the app closes. */
class MainActivity : Activity() {
    private val io = Executors.newSingleThreadExecutor()
    private lateinit var status: TextView
    private lateinit var logView: TextView
    private lateinit var nameEdit: EditText
    private lateinit var modeSpinner: Spinner
    private lateinit var swipeEdit: EditText
    private lateinit var autostartBox: CheckBox
    private lateinit var profileSpinner: Spinner
    private val actionSpinners = mutableMapOf<String, Spinner>()
    private val keyEdits = mutableMapOf<String, EditText>()
    private val multEdits = mutableMapOf<String, EditText>()

    private val permListener = Shizuku.OnRequestPermissionResultListener { _, result ->
        if (result == PackageManager.PERMISSION_GRANTED) refresh() else setStatus("Shizuku permission denied.")
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(buildUi())
        Shizuku.addRequestPermissionResultListener(permListener)
        loadPrefs()
        refresh()
    }

    override fun onDestroy() {
        Shizuku.removeRequestPermissionResultListener(permListener)
        io.shutdown()
        super.onDestroy()
    }

    // ---------- UI ----------

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    private fun label(text: String, bold: Boolean = false) = TextView(this).apply {
        this.text = text
        textSize = if (bold) 18f else 14f
        if (bold) setTypeface(typeface, Typeface.BOLD)
        setPadding(0, dp(12), 0, dp(2))
    }

    private fun numberEdit(signed: Boolean) = EditText(this).apply {
        inputType = InputType.TYPE_CLASS_NUMBER or (if (signed) InputType.TYPE_NUMBER_FLAG_SIGNED else 0)
    }

    private fun spinner(items: List<String>) = Spinner(this).apply {
        adapter = ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item, items)
    }

    private fun buildUi(): View {
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(16), dp(16), dp(32))
        }
        col.addView(label("Ring Mapper", bold = true))
        status = TextView(this).apply { setPadding(0, dp(4), 0, dp(4)) }
        col.addView(status)

        col.addView(label("Privilege backend"))
        modeSpinner = spinner(listOf("Auto (Shizuku, then root)", "Root (su)", "Shizuku"))
        col.addView(modeSpinner)
        col.addView(Button(this).apply { text = "Grant Shizuku permission"; setOnClickListener { requestShizuku() } })

        col.addView(label("Profile"))
        profileSpinner = Spinner(this)
        col.addView(profileSpinner)
        val profRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        for ((t, f) in listOf<Pair<String, () -> Unit>>("Load" to ::loadProfile, "Save…" to ::saveProfile, "Delete" to ::deleteProfile))
            profRow.addView(Button(this).apply { text = t; setOnClickListener { f() } }, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        col.addView(profRow)
        refreshProfiles()

        col.addView(label("Ring device name"))
        val nameRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        nameEdit = EditText(this).apply { isSingleLine = true }
        nameRow.addView(nameEdit, LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        nameRow.addView(Button(this).apply { text = "Pick…"; setOnClickListener { pickDevice() } })
        col.addView(nameRow)

        val labels = Config.ACTIONS.map { it.second }
        for (d in Config.DIRS) {
            col.addView(label("Swipe ${d.replaceFirstChar { it.uppercase() }}", bold = true))
            val sp = spinner(labels)
            val key = numberEdit(false).apply { hint = "Android keycode (e.g. 22)"; visibility = View.GONE }
            sp.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
                override fun onItemSelected(p: AdapterView<*>?, v: View?, pos: Int, id: Long) {
                    key.visibility = if (Config.ACTIONS[pos].first == "custom") View.VISIBLE else View.GONE
                }
                override fun onNothingSelected(p: AdapterView<*>?) {}
            }
            val mult = numberEdit(true).apply { hint = "Multiplier" }
            actionSpinners[d] = sp; keyEdits[d] = key; multEdits[d] = mult
            col.addView(sp); col.addView(key)
            col.addView(TextView(this).apply { text = "Multiplier (repeats; distance for the swipes, negative = reverse)" })
            col.addView(mult)
        }

        col.addView(label("Swipe speed (ms): lower = faster fling"))
        swipeEdit = numberEdit(false)
        col.addView(swipeEdit)

        autostartBox = CheckBox(this).apply { text = "Start at boot (root, or Shizuku started at boot)" }
        col.addView(autostartBox)

        val buttons = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        buttons.addView(Button(this).apply { text = "Apply & start"; setOnClickListener { applyConfig() } },
            LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        buttons.addView(Button(this).apply { text = "Stop"; setOnClickListener { stopDaemon() } },
            LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        buttons.addView(Button(this).apply { text = "Refresh"; setOnClickListener { refresh() } },
            LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f))
        col.addView(buttons)

        col.addView(label("Daemon log"))
        logView = TextView(this).apply { typeface = Typeface.MONOSPACE; textSize = 11f; setTextIsSelectable(true) }
        col.addView(logView)

        val scroll = ScrollView(this).apply { addView(col, MATCH_PARENT, WRAP_CONTENT) }
        // targetSdk 35+ draws edge-to-edge: keep content out from under the system bars.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            scroll.setOnApplyWindowInsetsListener { v, insets ->
                val b = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.ime())
                v.setPadding(b.left, b.top, b.right, b.bottom)
                insets
            }
        }
        return scroll
    }

    private fun loadPrefs() {
        nameEdit.setText(Config.name(this))
        modeSpinner.setSelection(Config.mode(this))
        for (d in Config.DIRS) {
            var a = Config.action(this, d)
            if (a == "page_prev") a = "dpad_left"
            if (a == "page_next") a = "dpad_right"
            actionSpinners[d]!!.setSelection(Config.ACTIONS.indexOfFirst { it.first == a }.coerceAtLeast(0))
            keyEdits[d]!!.setText(Config.key(this, d).toString())
            multEdits[d]!!.setText(Config.mult(this, d).toString())
        }
        swipeEdit.setText(Config.swipeMs(this).toString())
        autostartBox.isChecked = Config.autostart(this)
    }

    private fun intOf(e: EditText, def: Int, min: Int, max: Int) =
        (e.text.toString().trim().toIntOrNull() ?: def).coerceIn(min, max)

    private fun savePrefs() {
        val ed = Config.prefs(this).edit()
        ed.putString("name", nameEdit.text.toString().trim().ifEmpty { "JX-11" })
        ed.putInt("mode", modeSpinner.selectedItemPosition)
        for (d in Config.DIRS) {
            ed.putString(d, Config.ACTIONS[actionSpinners[d]!!.selectedItemPosition].first)
            ed.putInt("${d}_key", intOf(keyEdits[d]!!, 22, 1, 999))
            ed.putInt("${d}_mult", intOf(multEdits[d]!!, 1, -50, 50))
        }
        ed.putInt("swipe_duration_ms", intOf(swipeEdit, 200, 10, 2000))
        ed.putBoolean("autostart", autostartBox.isChecked)
        ed.apply()
        loadPrefs()   // show clamped values
    }

    // ---------- profiles (the form's mapping, saved under a name) ----------

    private fun formValues(): Map<String, String> = buildMap {
        put("name", nameEdit.text.toString().trim())
        for (d in Config.DIRS) {
            put(d, Config.ACTIONS[actionSpinners[d]!!.selectedItemPosition].first)
            put("${d}_key", keyEdits[d]!!.text.toString().trim())
            put("${d}_mult", multEdits[d]!!.text.toString().trim())
        }
        put("swipe_duration_ms", swipeEdit.text.toString().trim())
    }

    private fun refreshProfiles(select: String? = null) {
        val names = Config.profileNames(this)
        profileSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item,
            names.ifEmpty { listOf("(no saved profiles)") })
        if (select != null) profileSpinner.setSelection(names.indexOf(select).coerceAtLeast(0))
    }

    private fun selectedProfile(): String? =
        Config.profileNames(this).getOrNull(profileSpinner.selectedItemPosition)

    private fun saveProfile() {
        val input = EditText(this).apply { isSingleLine = true; setText(selectedProfile() ?: "") }
        AlertDialog.Builder(this)
            .setTitle("Save profile as")
            .setView(input)
            .setPositiveButton("Save") { _, _ ->
                val n = input.text.toString().trim()
                if (n.isEmpty()) return@setPositiveButton
                Config.saveProfile(this, n, formValues())
                refreshProfiles(n)
                setStatus("Saved profile \"$n\". Apply & start uses whatever the form shows.")
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun loadProfile() {
        val n = selectedProfile() ?: return setStatus("No saved profile to load.")
        val v = Config.loadProfile(this, n)
        v["name"]?.takeIf { it.isNotEmpty() }?.let { nameEdit.setText(it) }
        for (d in Config.DIRS) {
            v[d]?.let { a -> actionSpinners[d]!!.setSelection(Config.ACTIONS.indexOfFirst { it.first == a }.coerceAtLeast(0)) }
            v["${d}_key"]?.let { keyEdits[d]!!.setText(it) }
            v["${d}_mult"]?.let { multEdits[d]!!.setText(it) }
        }
        v["swipe_duration_ms"]?.let { swipeEdit.setText(it) }
        setStatus("Loaded profile \"$n\". Tap Apply & start to use it.")
    }

    private fun deleteProfile() {
        val n = selectedProfile() ?: return
        AlertDialog.Builder(this)
            .setMessage("Delete profile \"$n\"?")
            .setPositiveButton("Delete") { _, _ -> Config.deleteProfile(this, n); refreshProfiles() }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun setStatus(s: String) = runOnUiThread { status.text = s }

    // ---------- actions ----------

    private fun requestShizuku() {
        when {
            !ShizukuPriv.running() -> setStatus("Shizuku is not running. Start it in the Shizuku app first.")
            ShizukuPriv.granted() -> setStatus("Shizuku permission already granted.")
            else -> Shizuku.requestPermission(1)
        }
    }

    /** Runs [block] with a privileged shell on the background thread, then closes it. */
    private fun withPriv(block: (Priv) -> String) {
        val mode = modeSpinner.selectedItemPosition
        setStatus("Working…")
        io.execute {
            val why = StringBuilder()
            val p = Priv.open(this, mode, why)
            if (p == null) {
                setStatus("No privileges: $why")
                return@execute
            }
            p.use {
                val msg = try { block(it) } catch (e: Exception) { "Error: ${e.message}" }
                val st = Daemon.status(it)
                val log = Daemon.log(it)
                runOnUiThread {
                    status.text = "$msg\nBackend: ${it.label}\n" +
                        (if (st.isEmpty()) "Daemon: not running" else "Running:\n$st")
                    logView.text = log
                }
            }
        }
    }

    private fun refresh() = withPriv { "" }

    private fun applyConfig() {
        savePrefs()
        val text = Config.toFileText(this)
        withPriv { Daemon.apply(this, it, text) }
    }

    private fun stopDaemon() = withPriv { Daemon.stop(it) }

    private fun pickDevice() {
        val mode = modeSpinner.selectedItemPosition
        io.execute {
            val p = Priv.open(this, mode) ?: run { setStatus("No privileges to list devices."); return@execute }
            val names = p.use { Daemon.listDevices(this, it) }
            runOnUiThread {
                if (names.isEmpty()) { status.text = "No input devices found. Is the ring connected?"; return@runOnUiThread }
                AlertDialog.Builder(this)
                    .setTitle("Input devices")
                    .setItems(names.toTypedArray()) { _, i -> nameEdit.setText(names[i]) }
                    .show()
            }
        }
    }
}
