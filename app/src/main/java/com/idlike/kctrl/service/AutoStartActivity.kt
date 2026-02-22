package com.idlike.kctrl.service

import android.animation.Animator
import android.animation.AnimatorListenerAdapter
import android.content.ClipData
import android.content.ClipboardManager
import android.content.ComponentName
import android.content.Context
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.RemoteException
import android.view.View
import android.view.ViewAnimationUtils
import android.view.animation.Animation
import android.view.animation.AnimationUtils
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.Spinner
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.cardview.widget.CardView
import com.google.android.material.button.MaterialButton
import rikka.shizuku.Shizuku

class AutoStartActivity : AppCompatActivity() {

    private val PREFS_NAME = "kctrl_prefs"
    private val KEY_START_METHOD = "start_method"

    private var userService: IUserService? = null
    private val handler = Handler(Looper.getMainLooper())

    private lateinit var titleText: TextView
    private lateinit var statusText: TextView
    private lateinit var methodText: TextView
    private lateinit var progressBar: ProgressBar
    private lateinit var progressLayout: LinearLayout
    private lateinit var selectorLayout: LinearLayout
    private lateinit var methodSpinner: Spinner
    private lateinit var btnActivate: MaterialButton
    private lateinit var shellHintText: TextView
    private lateinit var shellPathText: TextView
    private lateinit var btnClose: android.widget.ImageView
    private lateinit var dialogCard: CardView

    private var isAnimating = false

    private var elfPath: String? = null
    private var shizukuAvailable = false
    private var rootAvailable = false

    private var displayMethods = mutableListOf<String>()
    private var selectedMethodIndex = 0

    private val userServiceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName, service: IBinder) {
            userService = IUserService.Stub.asInterface(service)
            executeStart()
        }

        override fun onServiceDisconnected(name: ComponentName) {
            userService = null
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_auto_start)

        titleText = findViewById(R.id.title_text)
        statusText = findViewById(R.id.status_text)
        methodText = findViewById(R.id.method_text)
        progressBar = findViewById(R.id.progress_bar)
        progressLayout = findViewById(R.id.progress_layout)
        selectorLayout = findViewById(R.id.selector_layout)
        methodSpinner = findViewById(R.id.method_spinner)
        btnActivate = findViewById(R.id.btn_activate)
        shellHintText = findViewById(R.id.shell_hint_text)
        shellPathText = findViewById(R.id.shell_path_text)
        btnClose = findViewById(R.id.btn_close)
        dialogCard = findViewById(R.id.dialog_card)

        btnClose.setOnClickListener {
            animateExitAndFinish()
        }

        elfPath = getElfPath()
        shellPathText.text = elfPath ?: "无法获取路径"
        shellPathText.setOnClickListener {
            val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
            val clip = ClipData.newPlainText("KCTRL Path", elfPath)
            clipboard.setPrimaryClip(clip)
            Toast.makeText(this, "路径已复制", Toast.LENGTH_SHORT).show()
        }

        btnActivate.setOnClickListener {
            val selectedMethod = getSelectedMethodKey()
            if (selectedMethod != null && selectedMethod != "shell") {
                startWithMethod(selectedMethod)
            }
        }

        checkAndStart()
    }

    private fun checkAvailability() {
        shizukuAvailable = checkShizukuAvailable()
        rootAvailable = checkRootAvailable()

        displayMethods.clear()
        if (shizukuAvailable) {
            displayMethods.add("Shizuku")
        } else {
            displayMethods.add("Shizuku [不可用]")
        }
        if (rootAvailable) {
            displayMethods.add("Root")
        } else {
            displayMethods.add("Root [不可用]")
        }
        displayMethods.add("Shell命令")

        val adapter = ArrayAdapter(this, android.R.layout.simple_spinner_item, displayMethods.toTypedArray())
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        methodSpinner.adapter = adapter

        methodSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>, view: View?, position: Int, id: Long) {
                selectedMethodIndex = position
                updateUIForMethod()
            }
            override fun onNothingSelected(parent: AdapterView<*>) {}
        }

        selectedMethodIndex = 0
        if (!shizukuAvailable && rootAvailable) {
            selectedMethodIndex = 1
        }
        methodSpinner.setSelection(selectedMethodIndex)
    }

    private fun getSelectedMethodKey(): String? {
        return when {
            selectedMethodIndex == 0 && shizukuAvailable -> "shizuku"
            selectedMethodIndex == 0 && !shizukuAvailable -> null
            selectedMethodIndex == 1 && rootAvailable -> "root"
            selectedMethodIndex == 1 && !rootAvailable -> null
            selectedMethodIndex == 2 -> "shell"
            else -> null
        }
    }

    private fun checkShizukuAvailable(): Boolean {
        return try {
            Shizuku.getBinder() != null && Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED
        } catch (e: Exception) {
            false
        }
    }

    private fun checkRootAvailable(): Boolean {
        return try {
            val process = Runtime.getRuntime().exec(arrayOf("su", "-c", "echo test"))
            val reader = java.io.BufferedReader(java.io.InputStreamReader(process.inputStream))
            val output = reader.readLine()
            process.waitFor()
            output == "test"
        } catch (e: Exception) {
            false
        }
    }

    private fun updateUIForMethod() {
        val methodKey = getSelectedMethodKey()
        
        if (methodKey == "shell") {
            btnActivate.visibility = View.GONE
            shellHintText.visibility = View.VISIBLE
            shellPathText.visibility = View.VISIBLE
            btnActivate.isEnabled = false
        } else if (methodKey == null) {
            btnActivate.visibility = View.VISIBLE
            btnActivate.isEnabled = false
            shellHintText.visibility = View.GONE
            shellPathText.visibility = View.GONE
        } else {
            btnActivate.visibility = View.VISIBLE
            btnActivate.isEnabled = true
            shellHintText.visibility = View.GONE
            shellPathText.visibility = View.GONE
        }
    }

    private fun checkAndStart() {
        animateDialogEnter()
        
        Thread {
            val exec = libkctrlexec()
            if (exec.isworking()) {
                handler.post { animateExitAndFinish() }
                return@Thread
            }

            val savedMethod = getStartMethod()
            if (savedMethod != null) {
                val canUse = when (savedMethod) {
                    "shizuku" -> checkShizukuAvailable()
                    "root" -> checkRootAvailable()
                    else -> false
                }
                
                if (canUse) {
                    handler.post {
                        showProgress("正在启动服务...", "使用 ${savedMethod.replaceFirstChar { it.uppercase() }}")
                    }
                    tryStartWithMethod(savedMethod) { success ->
                        if (success) {
                            handler.post { animateExitAndFinish() }
                        } else {
                            handler.post { showSelector() }
                        }
                    }
                } else {
                    handler.post { showSelector() }
                }
            } else {
                handler.post { showSelector() }
            }
        }.start()
    }

    private fun showProgress(status: String, method: String) {
        titleText.visibility = View.GONE
        statusText.text = status
        methodText.text = method
        
        if (selectorLayout.visibility == View.VISIBLE) {
            val outAnim = AnimationUtils.loadAnimation(this, R.anim.slide_out_bottom)
            outAnim.setAnimationListener(object : Animation.AnimationListener {
                override fun onAnimationStart(animation: Animation?) {}
                override fun onAnimationRepeat(animation: Animation?) {}
                override fun onAnimationEnd(animation: Animation?) {
                    selectorLayout.visibility = View.GONE
                    progressLayout.visibility = View.VISIBLE
                    val inAnim = AnimationUtils.loadAnimation(this@AutoStartActivity, R.anim.slide_in_bottom)
                    progressLayout.startAnimation(inAnim)
                }
            })
            selectorLayout.startAnimation(outAnim)
        } else {
            progressLayout.visibility = View.VISIBLE
            val inAnim = AnimationUtils.loadAnimation(this, R.anim.fade_scale_in)
            progressLayout.startAnimation(inAnim)
        }
    }

    private fun showSelector() {
        checkAvailability()
        
        if (progressLayout.visibility == View.VISIBLE) {
            val outAnim = AnimationUtils.loadAnimation(this, R.anim.slide_out_bottom)
            outAnim.setAnimationListener(object : Animation.AnimationListener {
                override fun onAnimationStart(animation: Animation?) {}
                override fun onAnimationRepeat(animation: Animation?) {}
                override fun onAnimationEnd(animation: Animation?) {
                    progressLayout.visibility = View.GONE
                    titleText.visibility = View.VISIBLE
                    selectorLayout.visibility = View.VISIBLE
                    updateUIForMethod()
                    val inAnim = AnimationUtils.loadAnimation(this@AutoStartActivity, R.anim.slide_in_bottom)
                    selectorLayout.startAnimation(inAnim)
                }
            })
            progressLayout.startAnimation(outAnim)
        } else {
            titleText.visibility = View.VISIBLE
            selectorLayout.visibility = View.VISIBLE
            updateUIForMethod()
            val inAnim = AnimationUtils.loadAnimation(this, R.anim.fade_scale_in)
            selectorLayout.startAnimation(inAnim)
        }
    }

    private fun animateExitAndFinish() {
        if (isAnimating) return
        isAnimating = true
        
        val exitAnim = AnimationUtils.loadAnimation(this, R.anim.dialog_exit)
        exitAnim.setAnimationListener(object : Animation.AnimationListener {
            override fun onAnimationStart(animation: Animation?) {}
            override fun onAnimationRepeat(animation: Animation?) {}
            override fun onAnimationEnd(animation: Animation?) {
                finish()
                overridePendingTransition(0, 0)
            }
        })
        dialogCard.startAnimation(exitAnim)
    }

    private fun animateDialogEnter() {
        val enterAnim = AnimationUtils.loadAnimation(this, R.anim.dialog_enter)
        dialogCard.startAnimation(enterAnim)
    }

    private fun startWithMethod(method: String) {
        handler.post {
            showProgress("正在启动服务...", "使用 ${method.replaceFirstChar { it.uppercase() }}")
        }

        tryStartWithMethod(method) { success ->
            if (success) {
                saveStartMethod(method)
                handler.post { animateExitAndFinish() }
            } else {
                handler.post {
                    statusText.text = "启动失败"
                    methodText.text = "请尝试其他方式"
                    handler.postDelayed({ showSelector() }, 1500)
                }
            }
        }
    }

    private fun tryStartWithMethod(method: String, callback: (Boolean) -> Unit) {
        when (method) {
            "shizuku" -> startWithShizuku(callback)
            "root" -> startWithRoot(callback)
            else -> callback(false)
        }
    }

    private fun getStartMethod(): String? {
        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        return prefs.getString(KEY_START_METHOD, null)
    }

    private fun saveStartMethod(method: String) {
        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        prefs.edit().putString(KEY_START_METHOD, method).apply()
    }

    private fun getElfPath(): String {
        val apkFullPath = applicationContext.packageCodePath
        val apkPath = apkFullPath.substringBeforeLast("/")

        val abiMap = mapOf(
            "arm64-v8a" to "arm64",
            "armeabi-v7a" to "armeabi-v7a",
            "x86_64" to "x86_64",
            "x86" to "x86"
        )

        val abi = Build.SUPPORTED_ABIS
            .firstNotNullOfOrNull { abiMap[it] }
            ?: throw RuntimeException("Unsupported ABI: ${Build.SUPPORTED_ABIS.joinToString()}")

        return "$apkPath/lib/$abi/libservice.so"
    }

    private fun startWithShizuku(callback: (Boolean) -> Unit) {
        if (Shizuku.getBinder() == null) {
            callback(false)
            return
        }

        if (Shizuku.checkSelfPermission() != PackageManager.PERMISSION_GRANTED) {
            callback(false)
            return
        }

        val args = Shizuku.UserServiceArgs(
            ComponentName(this, DaemonService::class.java)
        )
            .daemon(true)
            .processNameSuffix("daemon")
            .version(1)

        this.callback = callback
        Shizuku.bindUserService(args, userServiceConnection)
    }

    private var callback: ((Boolean) -> Unit)? = null

    private fun executeStart() {
        Thread {
            val exec = libkctrlexec()
            if (exec.isworking()) {
                callback?.invoke(true)
                callback = null
                return@Thread
            }

            val servicePath = getElfPath()

            try {
                userService?.startBinary(servicePath, emptyArray())
                Thread.sleep(1500)

                if (exec.isworking()) {
                    callback?.invoke(true)
                } else {
                    callback?.invoke(false)
                }
            } catch (e: RemoteException) {
                callback?.invoke(false)
            } catch (e: Exception) {
                callback?.invoke(false)
            }
            callback = null
        }.start()
    }

    private fun startWithRoot(callback: (Boolean) -> Unit) {
        Thread {
            val exec = libkctrlexec()
            if (exec.isworking()) {
                callback(true)
                return@Thread
            }

            val servicePath = getElfPath()

            try {
                val process = Runtime.getRuntime().exec(arrayOf("su", "-c", "chmod 755 $servicePath && $servicePath"))
                val exitCode = process.waitFor()
                Thread.sleep(1500)

                if (exec.isworking()) {
                    callback(true)
                } else {
                    callback(false)
                }
            } catch (e: Exception) {
                callback(false)
            }
        }.start()
    }

    override fun onDestroy() {
        super.onDestroy()
        handler.removeCallbacksAndMessages(null)
        if (userService != null) {
            try {
                unbindService(userServiceConnection)
            } catch (e: Exception) {
            }
        }
    }
}
