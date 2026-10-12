# aud_dsp_sampler: the native library binds the native method of the shim by
# its name, so R8 must neither remove nor rename the class.
-keep class com.audanika.aud_dsp_sampler.** { *; }
