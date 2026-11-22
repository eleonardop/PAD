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
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import com.eleonardop.pad.R
import com.eleonardop.pad.app.MainActivity

/**
 * Serviço em primeiro plano (tipo "mediaPlayback") que mantém o processo vivo com a tela
 * desligada ou com o usuário em outro app. O áudio em si roda no thread de tempo real do
 * Oboe/AAudio; o serviço só garante que o Android não encerre o app durante o culto.
 *
 * v0.6: o serviço só existe enquanto há som. Ele para sozinho quando as notas terminam e a tela
 * não está visível (e então fecha a saída de áudio), e encerra tudo quando o app é removido dos
 * recentes ou quando o usuário toca "Encerrar" na notificação.
 */
class PadPlaybackService : Service() {

    private val main = Handler(Looper.getMainLooper())

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        PadEngine.init(this)
        // Sempre entra em primeiro plano primeiro (o Android exige em até 5 s depois de
        // startForegroundService, mesmo que a próxima linha seja parar).
        val notification = buildNotification()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
        when (intent?.action) {
            ACTION_STOP_NOTES -> { PadEngine.stop(); scheduleIdleCheck() }
            ACTION_SHUTDOWN -> { shutdownEverything(); return START_NOT_STICKY }
        }
        // NOT_STICKY: se o sistema matar o processo, ele não volta sozinho tocando nada.
        return START_NOT_STICKY
    }

    /** Usuário deslizou o app para fora dos recentes: é um "sair". */
    override fun onTaskRemoved(rootIntent: Intent?) {
        shutdownEverything()
        super.onTaskRemoved(rootIntent)
    }

    override fun onDestroy() {
        main.removeCallbacksAndMessages(null)
        running = false
        super.onDestroy()
    }

    private fun shutdownEverything() {
        main.removeCallbacksAndMessages(null)
        PadEngine.release {
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
        }
    }

    /** Depois de parar as notas: quando o som acabar e a tela não estiver visível, fecha tudo. */
    private fun scheduleIdleCheck() {
        main.removeCallbacksAndMessages(null)
        main.postDelayed(object : Runnable {
            override fun run() {
                if (PadEngine.isPlaying()) { main.postDelayed(this, 1000); return }
                if (!uiVisible) PadEngine.suspendAudio()
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
            }
        }, 1500)
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
        val shutdown = PendingIntent.getService(
            this, 2, Intent(this, PadPlaybackService::class.java).setAction(ACTION_SHUTDOWN),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        return Notification.Builder(this, CHANNEL_ID)
            .setContentTitle(getString(R.string.app_name))
            .setContentText(getString(R.string.notification_text))
            .setSmallIcon(R.drawable.ic_notification)
            .setContentIntent(openApp)
            .setOngoing(true)
            .addAction(Notification.Action.Builder(null as Icon?, getString(R.string.stop_notes), stopNotes).build())
            .addAction(Notification.Action.Builder(null as Icon?, getString(R.string.shutdown), shutdown).build())
            .build()
    }

    companion object {
        private const val CHANNEL_ID = "pad_playback"
        private const val NOTIFICATION_ID = 1001
        const val ACTION_STOP_NOTES = "com.eleonardop.pad.STOP_NOTES"
        const val ACTION_SHUTDOWN = "com.eleonardop.pad.SHUTDOWN"

        /** A tela está visível? (atualizado pela MainActivity em onStart/onStop) */
        @Volatile var uiVisible = false
        @Volatile var running = false
            private set

        /** Chamado ao tocar uma nota: garante o serviço (som com a tela desligada). */
        fun ensureRunning(context: Context) {
            if (running) return
            running = true
            context.startForegroundService(Intent(context, PadPlaybackService::class.java))
        }

        /** Parar as notas pela tela: o serviço sai quando o som terminar. */
        fun notesStopped(context: Context) {
            if (!running) return
            context.startService(Intent(context, PadPlaybackService::class.java).setAction(ACTION_STOP_NOTES))
        }

        /** Encerrar tudo a partir da tela (botão Sair / voltar sem som). */
        fun shutdown(context: Context) {
            if (running) context.startService(Intent(context, PadPlaybackService::class.java).setAction(ACTION_SHUTDOWN))
            else PadEngine.release()
        }
    }
}
