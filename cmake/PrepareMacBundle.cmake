# Finder/FileProvider can attach FinderInfo to generated .app directories in Documents.
# Remove only that signing-incompatible attribute, and only on our build product.
# Missing attributes are expected; codesign itself reports any remaining failure.
execute_process(COMMAND /usr/bin/xattr -d com.apple.FinderInfo "${APP_BUNDLE}"
    OUTPUT_QUIET ERROR_QUIET)
