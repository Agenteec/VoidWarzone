package com.voidwarzone.game

import android.net.Uri

object InviteLink {
    private const val SCHEME = "voidwarzone"
    private const val HOST = "join"
    private val code = Regex("^VOID-[A-HJ-NP-Z2-9]{4}$")

    fun parse(uri: Uri?): String? {
        if (uri == null || uri.scheme != SCHEME || uri.host != HOST)
            return null
        val raw = uri.lastPathSegment?.trim()?.uppercase() ?: return null
        val full = if (raw.startsWith("VOID-")) raw else "VOID-$raw"
        return full.takeIf(code::matches)
    }
}
