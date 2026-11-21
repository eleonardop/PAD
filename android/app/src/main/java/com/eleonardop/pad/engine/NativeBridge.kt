package com.eleonardop.pad.engine

/**
 * Declarações JNI do motor nativo (libpadjni.so). Uso interno: a API pública é [PadEngine].
 */
internal object NativeBridge {
    init {
        System.loadLibrary("padjni")
    }

    @JvmStatic external fun nativeCreate(): Long
    @JvmStatic external fun nativeDestroy(handle: Long)
    @JvmStatic external fun nativeStart(handle: Long): Boolean
    @JvmStatic external fun nativeShutdownAudio(handle: Long)

    @JvmStatic external fun nativePlayChord(handle: Long, root: Int, type: Int): Boolean
    @JvmStatic external fun nativeSetKey(handle: Long, root: Int): Boolean
    @JvmStatic external fun nativeStopNotes(handle: Long): Boolean

    @JvmStatic external fun nativeSetParam(handle: Long, id: Int, value: Float)
    @JvmStatic external fun nativeGetParam(handle: Long, id: Int): Float
    @JvmStatic external fun nativeApplyPreset(handle: Long, index: Int)

    @JvmStatic external fun nativePresetCount(): Int
    @JvmStatic external fun nativePresetName(index: Int): String
    @JvmStatic external fun nativePresetDescription(index: Int): String
    @JvmStatic external fun nativeParamCount(): Int
    @JvmStatic external fun nativeParamName(id: Int): String
    @JvmStatic external fun nativeParamRange(id: Int): FloatArray

    @JvmStatic external fun nativeStats(handle: Long): FloatArray
}
