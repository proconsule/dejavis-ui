include(BundleUtilities)

set(APP_PATH "${BUNDLE_DIR}")
set(EXTRA_LIBS_DIRS 
    "/usr/local/lib" 
    "/opt/homebrew/lib"
    "/usr/local/opt/portaudio/lib"
)

fixup_bundle("${APP_PATH}" "" "${EXTRA_LIBS_DIRS}")
