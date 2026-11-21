package com.eleonardop.pad.engine

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.graphics.drawable.Icon
import android.os.Build
import android.os.IBinder
import com.eleonardop.pad.R
import com.eleonardop.pad.app.MainActivity

/**
 * Serviço em primeiro plano (tipo "mediaPlayback") que mantém o processo vivo com a tela
 * desligada ou com o usuário em outro app. O áudio em si roda no thread de tempo real do
 * Oboe/AAudio; o serviço só garante que o Android não encerre o app durante o culto.
 */
class PadPlaybackService : Service() {

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP_NOTES -> PadEngine.stop()
            ACTION_SHUTDOWN -> {
                PadEngine.stop()
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
                return START_NOT_STICKY
            }
        }
        val notification = buildNotification()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
        return START_STICKY
    }

    private fun buildNotification(): Notification {
        val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        if (nm.getNotificationChannel(CHANNEL_ID) == null) {
            nm.createNotificationChannel(
                NotificationChannel(CHANNEL_ID, getString(R.string.channel_name), NotificationManager.IMPORTANCE_LOW).apply {
                    setShowBadge(false)
                }
            )
        }
        val openApp = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val stopNotes = PendingIntent.getService(
            this, 1, Intent(this, PadPlaybackService::class.java).setAction(ACTION_STOP_NOTES),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        return Notification.Builder(this, CHANNEL_ID)
            .setContentTitle(getString(R.string.app_name))
            .setContentText(getString(R.string.notification_text))
            .setSmallIcon(R.drawable.ic_notification)
            .setContentIntent(openApp)
            .setOngoing(true)
            .addAction(Notification.Action.Builder(null as Icon?, getString(R.string.stop_notes), stopNotes).build())
            .build()
    }

    companion object {
        private const val CHANNEL_ID = "pad_playback"
        private const val NOTIFICATION_ID = 1001
        const val ACTION_STOP_NOTES = "com.eleonardop.pad.STOP_NOTES"
        const val ACTION_SHUTDOWN = "com.eleonardop.pad.SHUTDOWN"

        fun ensureRunning(context: Context) {
            context.startForegroundService(Intent(context, PadPlaybackService::class.java))
        }
    }
}
