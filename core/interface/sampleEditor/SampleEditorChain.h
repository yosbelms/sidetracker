#pragma once
#include <functional>
class cSampleEditor;
void sampleChainOpen(cSampleEditor* editor);
bool sampleChainIsOpen();
bool sampleChainHasDraft();
unsigned sampleChainStageCount();
bool sampleChainUpdate();
bool sampleChainPreview(cSampleEditor* editor);
bool sampleChainDeferEditorAction(std::function<void()> action);
void sampleChainResetControls();
