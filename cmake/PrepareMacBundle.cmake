# Finder/FileProvider can attach FinderInfo and fileprovider attributes to generated .app
# directories in Documents. Clear them recursively on our build product only; codesign itself
# reports any remaining signing failure.
execute_process(COMMAND /usr/bin/xattr -cr "${APP_BUNDLE}"
    OUTPUT_QUIET ERROR_QUIET)
