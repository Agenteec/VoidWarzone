package com.voidwarzone.game

import android.content.Intent
import org.libsdl.app.SDLActivity
import java.util.concurrent.atomic.AtomicReference

class VoidWarzoneActivity : SDLActivity() {
    private val pendingJoin = AtomicReference<String?>(null)

    override fun getLibraries(): Array<String> = arrayOf("main")

    override fun getArguments(): Array<String> {
        val extra = intent?.getStringExtra("args")?.trim().orEmpty()
        val args = if (extra.isEmpty()) mutableListOf() else extra.split(Regex("\\s+")).toMutableList()
        val code = InviteLink.parse(intent?.data)
        if (code != null && "--join" !in args)
            args += listOf("--join", code)
        return args.toTypedArray()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        InviteLink.parse(intent.data)?.let(pendingJoin::set)
    }

    // Called from native code (src/client/Platform.cpp) by name and signature
    fun takePendingJoin(): String? = pendingJoin.getAndSet(null)

    fun shareText(title: String, text: String) {
        runOnUiThread {
            val send = Intent(Intent.ACTION_SEND).apply {
                type = "text/plain"
                putExtra(Intent.EXTRA_SUBJECT, title)
                putExtra(Intent.EXTRA_TEXT, text)
            }
            startActivity(Intent.createChooser(send, title))
        }
    }
}
