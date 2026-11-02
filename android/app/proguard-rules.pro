# Called from native code through JNI by name
-keep class org.libsdl.app.** { *; }
-keep class com.voidwarzone.game.VoidWarzoneActivity {
    public *;
}
