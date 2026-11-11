juce_add_console_app(sonicator-features PRODUCT_NAME "sonicator-features")
target_sources(sonicator-features PRIVATE main.cpp)
target_link_libraries(sonicator-features PRIVATE SonicatorCore)
