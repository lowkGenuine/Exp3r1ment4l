"""Patch JUCE 7's unused native-window snapshot path for the macOS 15 SDK."""
from pathlib import Path
import sys

path = Path(sys.argv[1]) / 'modules/juce_gui_basics/native/juce_Windowing_mac.mm'
source = path.read_text(encoding='utf-8')
marker = '// Exp3r1ment4l: native-window capture is unused; macOS 15 SDK removed its API.'
if marker not in source:
    signature = 'static Image createNSWindowSnapshot (NSWindow* nsWindow)\n{'
    start = source.index(signature) + len(signature)
    end = source.index('\nImage createSnapshotOfNativeWindow', start)
    body_end = source.rfind('\n}', start, end)
    assert body_end > start and 'CGWindowListCreateImage' in source[start:body_end]
    replacement = f'''
   #if MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
    {marker}
    ignoreUnused (nsWindow);
    return {{}};
   #else'''
    source = source[:start] + replacement + source[start:body_end] + '\n   #endif' + source[body_end:]
    path.write_text(source, encoding='utf-8')
print('Applied JUCE macOS SDK compatibility patch for Exp3r1ment4l.')
