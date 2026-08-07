meta:
	ADDON_NAME = ofxPlotterKit
	ADDON_DESCRIPTION = Optional ofKitty integration: Reusable ImGui windows and panels for ofxPlotter.
	ADDON_AUTHOR = ofKitty
	ADDON_TAGS = "addon" "plotter" "imgui" "ofkitty"
	ADDON_URL = https://github.com/ofxyz/ofxPlotterKit

common:
	# ofxPixelPlotter: PlotterEffectGraphECS / EffectPhase used by GcodeGeneratorPanel
	ADDON_DEPENDENCIES = ofxPlotter ofxPixelPlotter ofxGrbl ofxKit ofxDocumentKit ofxVectorKit ofxImGuiStyle ofxImGuiMarkdown ofxImGuiTextEdit ofxPlotGenerators ofxPlotGeneratorsLSystem ofxPlotProcessors ofxGCode

linux64:
vs:
linuxarmv6l:
linuxarmv7l:
android/armeabi:
android/armeabi-v7a:
osx:
ios:
tvos:
