package com.voidwarzone.game;

import android.content.Intent;

import org.libsdl.app.SDLActivity;

public class VoidWarzoneActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    @Override
    protected String[] getArguments() {
        Intent intent = getIntent();
        String args = intent != null ? intent.getStringExtra("args") : null;
        if (args == null || args.trim().isEmpty())
            return new String[0];
        return args.trim().split("\s+");
    }
}
