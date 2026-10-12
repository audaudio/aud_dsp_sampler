// @license
// Copyright (c) Audanika. All Rights Reserved.
//
// Use of this source code is governed by terms that can be
// found in the LICENSE file in the root of this package.

package com.audanika.aud_dsp_sampler;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.content.res.AssetManager;
import android.database.Cursor;
import android.net.Uri;

/**
 * Hands the AssetManager of the app to the native library of the sampler
 * when the app process starts, before any Dart code runs, so that "apk:/"
 * paths read the assets of the APK in place (ticket 27). It serves no data.
 *
 * <p>Flutter packages the library of the build hook under {@code lib/<abi>/};
 * Java loads it here and Dart reaches the same file as code asset. A release
 * build whose link hook dropped the library, because the app does not use
 * the sampler, has nothing to load.
 */
public final class AudDspSamplerAssets extends ContentProvider {
  @Override
  public boolean onCreate() {
    Context context = getContext();
    if (context == null) {
      return true;
    }
    try {
      System.loadLibrary("aud_dsp_sampler");
    } catch (UnsatisfiedLinkError error) {
      return true;
    }
    nativeSetAssetManager(context.getAssets());
    return true;
  }

  /** Registers {@code assets} as the backend of "apk:/" paths in the native library. */
  private static native void nativeSetAssetManager(AssetManager assets);

  @Override
  public Cursor query(
      Uri uri, String[] projection, String selection, String[] args, String order) {
    return null;
  }

  @Override
  public String getType(Uri uri) {
    return null;
  }

  @Override
  public Uri insert(Uri uri, ContentValues values) {
    return null;
  }

  @Override
  public int delete(Uri uri, String selection, String[] args) {
    return 0;
  }

  @Override
  public int update(Uri uri, ContentValues values, String selection, String[] args) {
    return 0;
  }
}
