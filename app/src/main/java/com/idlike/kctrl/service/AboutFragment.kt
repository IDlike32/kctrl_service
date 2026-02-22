package com.idlike.kctrl.service

import android.media.MediaPlayer
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.fragment.app.Fragment
import java.io.File
import java.io.FileOutputStream

class AboutFragment : Fragment() {

    private lateinit var tipText: TextView
    private lateinit var videoView: CroppedVideoView
    private var tempVideoFile: File? = null

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View? {
        return inflater.inflate(R.layout.fragment_about, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        tipText = view.findViewById(R.id.tip_text)
        videoView = view.findViewById(R.id.video_view)
        
        setupVideoView()
    }

    private fun setupVideoView() {
        try {
            val context = requireContext()
            tempVideoFile = File(context.cacheDir, "cyrene_seeu.mp4")
            
            context.assets.open("Cyrene_seeu.mp4").use { input ->
                FileOutputStream(tempVideoFile).use { output ->
                    input.copyTo(output)
                }
            }
            
            videoView.setVideoPath(tempVideoFile!!.absolutePath)
            videoView.setOnPreparedListener { mediaPlayer ->
                val videoWidth = mediaPlayer.videoWidth
                val videoHeight = mediaPlayer.videoHeight
                videoView.setVideoSize(videoWidth, videoHeight)
                
                mediaPlayer.isLooping = true
                mediaPlayer.setVolume(0f, 0f)
                videoView.start()
            }
            
            videoView.setOnErrorListener { _, _, _ ->
                true
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    override fun onPause() {
        super.onPause()
        if (::videoView.isInitialized) {
            videoView.pause()
        }
    }

    override fun onResume() {
        super.onResume()
        if (::videoView.isInitialized && !videoView.isPlaying) {
            videoView.start()
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        if (::videoView.isInitialized) {
            videoView.stopPlayback()
        }
        tempVideoFile?.delete()
    }
}
