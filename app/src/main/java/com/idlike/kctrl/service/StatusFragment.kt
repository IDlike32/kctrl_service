package com.idlike.kctrl.service

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.fragment.app.Fragment
import com.google.android.material.button.MaterialButton
import com.google.android.material.card.MaterialCardView
import com.google.android.material.chip.Chip

class StatusFragment : Fragment() {

    private lateinit var statusText: TextView
    private lateinit var statusChip: Chip
    private lateinit var btnRunService: MaterialButton
    private lateinit var btnRunRoot: MaterialButton
    private lateinit var btnShutdown: MaterialButton
    private lateinit var activationMethodsCard: MaterialCardView
    private lateinit var serviceManageCard: MaterialCardView

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View? {
        return inflater.inflate(R.layout.fragment_status, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        statusText = view.findViewById(R.id.status_text)
        statusChip = view.findViewById(R.id.status_chip)
        btnRunService = view.findViewById(R.id.btn_run_service)
        btnRunRoot = view.findViewById(R.id.btn_run_root)
        btnShutdown = view.findViewById(R.id.btn_shutdown)
        activationMethodsCard = view.findViewById(R.id.activation_methods_card)
        serviceManageCard = view.findViewById(R.id.service_manage_card)

        btnRunService.setOnClickListener {
            (activity as? MainActivity)?.startDaemon()
        }

        btnRunRoot.setOnClickListener {
            (activity as? MainActivity)?.startWithRoot()
        }

        btnShutdown.setOnClickListener {
            (activity as? MainActivity)?.shutdownService()
        }
    }

    fun setStatusText(text: String) {
        if (::statusText.isInitialized) {
            statusText.text = text
        }
    }

    fun setServiceRunning(running: Boolean) {
        if (::statusChip.isInitialized) {
            if (running) {
                statusChip.text = "正在运行"
                statusChip.chipBackgroundColor = 
                    resources.getColorStateList(R.color.status_running, null)
            } else {
                statusChip.text = "未运行"
                statusChip.chipBackgroundColor = 
                    resources.getColorStateList(R.color.status_stopped, null)
            }
        }
    }

    fun setActivationMethodsVisible(visible: Boolean) {
        if (::activationMethodsCard.isInitialized) {
            activationMethodsCard.visibility = if (visible) View.VISIBLE else View.GONE
        }
    }

    fun setServiceManageVisible(visible: Boolean) {
        if (::serviceManageCard.isInitialized) {
            serviceManageCard.visibility = if (visible) View.VISIBLE else View.GONE
        }
    }
}
