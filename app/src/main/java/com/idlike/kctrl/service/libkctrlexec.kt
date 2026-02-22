package com.idlike.kctrl.service

import java.io.BufferedReader
import java.io.BufferedWriter
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import java.net.Socket

class libkctrlexec {

    private val HOST = "127.0.0.1"
    private val PORT = 50501
    private val TIMEOUT_MS = 5000

    private var socketPassword: String? = null

    private fun fetchPassword(): Boolean {
        if (socketPassword != null) return true
        
        return try {
            Socket(HOST, PORT).use { socket ->
                socket.soTimeout = TIMEOUT_MS
                val out = BufferedWriter(OutputStreamWriter(socket.getOutputStream()))
                val `in` = BufferedReader(InputStreamReader(socket.getInputStream()))

                out.write("socket_passwd\n")
                out.flush()

                val response = `in`.readLine()
                if (response != null && response.length == 8 && !response.startsWith("ERROR")) {
                    socketPassword = response
                    true
                } else {
                    false
                }
            }
        } catch (e: Exception) {
            false
        }
    }


    fun execCommand(cmd: String): String {
        if (!fetchPassword()) {
            return "Error: Failed to get password"
        }
        
        val password = socketPassword ?: return "Error: password not fetched"
        val commandWithPass = "$password exec $cmd"
        return sendRawCommand(commandWithPass)
    }
    fun isworking(): Boolean{
        return try {
            Socket(HOST, PORT).use { socket ->
                socket.soTimeout = TIMEOUT_MS
                val out = BufferedWriter(OutputStreamWriter(socket.getOutputStream()))
                val `in` = BufferedReader(InputStreamReader(socket.getInputStream()))

                out.write("testng\n")
                out.flush()

                val response = `in`.readLine()
                if (response != null  && response.startsWith("working")) {
                    true
                } else {
                    false
                }
            }
        } catch (e: Exception) {
            false
        }
    }
    fun shutdown(): String {
        val commandWithPass = "12345678 exit"
        return sendRawCommand(commandWithPass)
    }

    private fun sendRawCommand(command: String): String {
        return try {
            Socket(HOST, PORT).use { socket ->
                socket.soTimeout = TIMEOUT_MS
                val out = BufferedWriter(OutputStreamWriter(socket.getOutputStream()))
                val `in` = BufferedReader(InputStreamReader(socket.getInputStream()))

                out.write(command)
                out.write("\n")
                out.flush()

                `in`.readLine() ?: "Error: empty response"
            }
        } catch (e: Exception) {
            "Error: ${e.javaClass.simpleName}: ${e.message}"
        }
    }
}