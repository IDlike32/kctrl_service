package com.idlike.kctrl.service

import android.content.Context
import android.util.AttributeSet
import android.view.View
import android.widget.VideoView

class CroppedVideoView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : VideoView(context, attrs, defStyleAttr) {

    private var videoWidth = 0
    private var videoHeight = 0

    fun setVideoSize(width: Int, height: Int) {
        videoWidth = width
        videoHeight = height
        requestLayout()
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val containerWidth = MeasureSpec.getSize(widthMeasureSpec)
        val containerHeight = MeasureSpec.getSize(heightMeasureSpec)

        if (videoWidth > 0 && videoHeight > 0) {
            val aspectRatio = videoWidth.toFloat() / videoHeight.toFloat()
            val containerRatio = containerWidth.toFloat() / containerHeight.toFloat()

            if (containerRatio > aspectRatio) {
                val newWidth = (containerHeight * aspectRatio).toInt()
                setMeasuredDimension(newWidth, containerHeight)
            } else {
                val newHeight = (containerWidth / aspectRatio).toInt()
                setMeasuredDimension(containerWidth, newHeight)
            }
        } else {
            super.onMeasure(widthMeasureSpec, heightMeasureSpec)
        }
    }
}
