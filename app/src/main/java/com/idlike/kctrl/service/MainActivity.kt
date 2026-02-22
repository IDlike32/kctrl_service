package com.idlike.kctrl.service

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
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.viewpager2.widget.ViewPager2
import com.google.android.material.bottomnavigation.BottomNavigationView
import rikka.shizuku.Shizuku


class MainActivity : AppCompatActivity() {

    private val handler = Handler(Looper.getMainLooper())
    private val REQUEST_CODE = 1
    private val PREFS_NAME = "kctrl_prefs"
    private val KEY_START_METHOD = "start_method"

    private var userService: IUserService? = null
    private lateinit var bottomNavigation: BottomNavigationView
    private lateinit var viewPager: ViewPager2
    private lateinit var viewPagerAdapter: ViewPagerAdapter

    private val binderReceivedListener = Shizuku.OnBinderReceivedListener {
        updateStatus("Shizuku 已就绪")
        checkAndRequestPermission()
    }

    private val binderDeadListener = Shizuku.OnBinderDeadListener {
        updateStatus("Shizuku 未运行")
    }

    private val permissionResultListener = Shizuku.OnRequestPermissionResultListener { requestCode, grantResult ->
        if (requestCode == REQUEST_CODE) {
            val granted = grantResult == PackageManager.PERMISSION_GRANTED
            if (granted) {
                updateStatus("Shizuku 权限已授予")
            } else {
                updateStatus("Shizuku 权限被拒绝")
            }
        }
    }

    private val userServiceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName, service: IBinder) {
            userService = IUserService.Stub.asInterface(service)
            updateStatus("Shizuku 服务已连接")
        }

        override fun onServiceDisconnected(name: ComponentName) {
            userService = null
            updateStatus("Shizuku 服务已断开")
        }
    }

    fun getElfPath(): String {
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

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        bottomNavigation = findViewById(R.id.bottom_navigation)
        viewPager = findViewById(R.id.view_pager)

        viewPagerAdapter = ViewPagerAdapter(this)
        viewPager.adapter = viewPagerAdapter

        viewPager.registerOnPageChangeCallback(object : ViewPager2.OnPageChangeCallback() {
            override fun onPageSelected(position: Int) {
                when (position) {
                    0 -> bottomNavigation.selectedItemId = R.id.nav_status
                    1 -> bottomNavigation.selectedItemId = R.id.nav_about
                }
            }
        })

        bottomNavigation.setOnItemSelectedListener { item ->
            when (item.itemId) {
                R.id.nav_status -> viewPager.currentItem = 0
                R.id.nav_about -> viewPager.currentItem = 1
            }
            true
        }

        viewPager.offscreenPageLimit = 1

        updateStatus("等待选择激活方式...")

        Shizuku.addBinderReceivedListener(binderReceivedListener)
        Shizuku.addBinderDeadListener(binderDeadListener)
        Shizuku.addRequestPermissionResultListener(permissionResultListener)

        if (Shizuku.getBinder() != null) {
            binderReceivedListener.onBinderReceived()
        } else {
            updateStatus("等待 Shizuku 连接...")
        }

        checkServiceRunning()
    }

    private fun getStatusFragment(): StatusFragment? {
        val fragment = supportFragmentManager.findFragmentByTag("f0")
        return fragment as? StatusFragment
    }

    private fun checkServiceRunning() {
        Thread {
            val exec = libkctrlexec()
            val running = exec.isworking()
            handler.post {
                getStatusFragment()?.let { fragment ->
                    fragment.setServiceRunning(running)
                    if (running) {
                        updateStatus("服务正在运行")
                        fragment.setActivationMethodsVisible(false)
                    } else {
                        updateStatus("服务未运行")
                    }
                }
            }
        }.start()
    }

    private fun checkAndRequestPermission() {
        if (Shizuku.isPreV11()) {
            updateStatus("Shizuku 版本过旧（需要 v11+）")
            return
        }

        if (Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED) {
            updateStatus("Shizuku 权限已授予")
            bindUserService()
        } else if (Shizuku.shouldShowRequestPermissionRationale()) {
            updateStatus("请在 Shizuku 中手动授权")
        } else {
            updateStatus("请求 Shizuku 权限中...")
            Shizuku.requestPermission(REQUEST_CODE)
        }
    }

    private fun bindUserService() {
        updateStatus("连接 Shizuku 服务中...")

        try {
            val args = Shizuku.UserServiceArgs(
                ComponentName(this, DaemonService::class.java)
            )
                .daemon(true)
                .processNameSuffix("daemon")
                .version(1)

            Shizuku.bindUserService(args, userServiceConnection)
        } catch (e: Exception) {
            updateStatus("Shizuku 服务连接失败: ${e.message}")
        }
    }

    private fun saveStartMethod(method: String) {
        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        prefs.edit().putString(KEY_START_METHOD, method).apply()
    }

    fun startDaemon() {
        Thread {
            val exec = libkctrlexec()
            if (exec.isworking()) {
                handler.post {
                    updateStatus("服务已在运行")
                    getStatusFragment()?.setServiceRunning(true)
                    Toast.makeText(this@MainActivity, "服务已在运行", Toast.LENGTH_SHORT).show()
                }
                return@Thread
            }

            val servicePath = getElfPath()

            try {
                userService?.startBinary(servicePath, emptyArray())
                handler.post {
                    updateStatus("正在启动服务...")
                }

                Thread.sleep(1000)
                checkServiceStatusAndSave("shizuku")
            } catch (e: RemoteException) {
                handler.post {
                    updateStatus("激活失败: ${e.message}")
                    Toast.makeText(this@MainActivity, "激活失败: ${e.message}", Toast.LENGTH_LONG).show()
                }
            } catch (e: Exception) {
                handler.post {
                    updateStatus("Shizuku 服务未连接")
                    Toast.makeText(this@MainActivity, "请先连接 Shizuku 服务", Toast.LENGTH_SHORT).show()
                }
            }
        }.start()
    }

    private fun checkSuAvailable(): Boolean {
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

    fun startWithRoot() {
        Thread {
            val exec = libkctrlexec()
            if (exec.isworking()) {
                handler.post {
                    updateStatus("服务已在运行")
                    getStatusFragment()?.setServiceRunning(true)
                    Toast.makeText(this@MainActivity, "服务已在运行", Toast.LENGTH_SHORT).show()
                }
                return@Thread
            }

            if (!checkSuAvailable()) {
                handler.post {
                    updateStatus("Root 权限不可用")
                    Toast.makeText(this@MainActivity, "Root 权限不可用", Toast.LENGTH_LONG).show()
                }
                return@Thread
            }

            val servicePath = getElfPath()
            handler.post {
                updateStatus("正在启动服务...")
            }

            try {
                val process = Runtime.getRuntime().exec(arrayOf("su", "-c", "chmod 755 $servicePath && $servicePath"))
                val exitCode = process.waitFor()

                handler.post {
                    if (exitCode == 0) {
                        Thread {
                            Thread.sleep(1000)
                            checkServiceStatusAndSave("root")
                        }.start()
                    } else {
                        updateStatus("激活失败 (退出码: $exitCode)")
                        Toast.makeText(this@MainActivity, "激活失败", Toast.LENGTH_LONG).show()
                    }
                }
            } catch (e: Exception) {
                handler.post {
                    updateStatus("激活异常: ${e.message}")
                    Toast.makeText(this@MainActivity, "激活异常: ${e.message}", Toast.LENGTH_LONG).show()
                }
            }
        }.start()
    }

    private fun checkServiceStatusAndSave(method: String) {
        Thread {
            val exec = libkctrlexec()
            val running = exec.isworking()
            handler.post {
                getStatusFragment()?.let { fragment ->
                    fragment.setServiceRunning(running)
                    if (running) {
                        saveStartMethod(method)
                        updateStatus("服务已启动")
                        fragment.setActivationMethodsVisible(false)
                    } else {
                        updateStatus("服务启动失败")
                    }
                }
            }
        }.start()
    }

    fun shutdownService() {
        Thread {
            val exec = libkctrlexec()
            if (!exec.isworking()) {
                handler.post {
                    Toast.makeText(this@MainActivity, "服务未运行", Toast.LENGTH_SHORT).show()
                }
                return@Thread
            }

            val result = exec.shutdown()
            handler.post {
                if (result.contains("OK") || result.contains("shutdown")) {
                    getStatusFragment()?.let { fragment ->
                        fragment.setServiceRunning(false)
                        fragment.setActivationMethodsVisible(true)
                    }
                    updateStatus("服务已关闭")
                    Toast.makeText(this@MainActivity, "服务已关闭", Toast.LENGTH_SHORT).show()
                } else {
                    updateStatus("关闭失败: $result")
                    Toast.makeText(this@MainActivity, "关闭失败", Toast.LENGTH_SHORT).show()
                }
            }
        }.start()
    }

    private fun updateStatus(text: String) {
        getStatusFragment()?.setStatusText(text)
    }

    override fun onDestroy() {
        super.onDestroy()
        Shizuku.removeBinderReceivedListener(binderReceivedListener)
        Shizuku.removeBinderDeadListener(binderDeadListener)
        Shizuku.removeRequestPermissionResultListener(permissionResultListener)

        if (userService != null) {
            try {
                unbindService(userServiceConnection)
            } catch (e: Exception) {
            }
        }
    }
}
