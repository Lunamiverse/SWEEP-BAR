// Global JUCE + plugin configuration for the hand-rolled build (build_windows.sh).
// The CMakeLists.txt build sets the same values through juce_add_plugin instead.
#pragma once

#define JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED 1

#define JUCE_MODULE_AVAILABLE_juce_core                1
#define JUCE_MODULE_AVAILABLE_juce_events              1
#define JUCE_MODULE_AVAILABLE_juce_data_structures     1
#define JUCE_MODULE_AVAILABLE_juce_graphics            1
#define JUCE_MODULE_AVAILABLE_juce_gui_basics          1
#define JUCE_MODULE_AVAILABLE_juce_gui_extra           1
#define JUCE_MODULE_AVAILABLE_juce_audio_basics        1
#define JUCE_MODULE_AVAILABLE_juce_audio_processors    1
#define JUCE_MODULE_AVAILABLE_juce_audio_plugin_client 1

#define JUCE_STANDALONE_APPLICATION 0
#define JUCE_USE_CURL               0
#define JUCE_WEB_BROWSER            0
#define JUCE_USE_WIN_WEBVIEW2       0
#define JUCE_PLUGINHOST_VST3        0
#define JUCE_PLUGINHOST_VST         0
#define JUCE_PLUGINHOST_LADSPA      0
#define JUCE_PLUGINHOST_LV2         0
#define JUCE_STRICT_REFCOUNTEDPOINTER 1
#define JUCE_VST3_CAN_REPLACE_VST2  0

#define JucePlugin_Build_VST3       1
#define JucePlugin_Build_VST        0
#define JucePlugin_Build_AU         0
#define JucePlugin_Build_AUv3       0
#define JucePlugin_Build_AAX        0
#define JucePlugin_Build_Standalone 0
#define JucePlugin_Build_Unity      0
#define JucePlugin_Build_LV2        0
#define JucePlugin_Enable_ARA       0

#define JucePlugin_Name                 "Sweepbar"
#define JucePlugin_Desc                 "Beat-synced vocal ear candy"
#define JucePlugin_Manufacturer         "Sweepbar"
#define JucePlugin_ManufacturerWebsite  ""
#define JucePlugin_ManufacturerEmail    ""
#define JucePlugin_ManufacturerCode     0x53776270 // 'Swbp'
#define JucePlugin_PluginCode           0x53776231 // 'Swb1'
#define JucePlugin_IsSynth              0
#define JucePlugin_WantsMidiInput       0
#define JucePlugin_ProducesMidiOutput   0
#define JucePlugin_IsMidiEffect         0
#define JucePlugin_EditorRequiresKeyboardFocus 0
#define JucePlugin_Version              0.5.0
#define JucePlugin_VersionCode          0x00000500
#define JucePlugin_VersionString        "0.5.0"
#define JucePlugin_VSTUniqueID          JucePlugin_PluginCode
#define JucePlugin_VSTNumMidiInputs     0
#define JucePlugin_VSTNumMidiOutputs    0
#define JucePlugin_Vst3Category         "Fx|Modulation"
#define JucePlugin_AAXIdentifier        com.sweepbar.sweepbar
