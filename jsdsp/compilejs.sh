OPTIONS="
-s EXPORT_NAME=jsDSPModule 
-ICMSIS_5/CMSIS/DSP/Include 
-ICMSIS_5/CMSIS/Core/Include 
lib/CMSIS_DSP/libCMSISDSPFiltering.a 
lib/CMSIS_DSP/libCMSISDSPBasicMath.a 
lib/CMSIS_DSP/libCMSISDSPFastMath.a
lib/CMSIS_DSP/libCMSISDSPComplexMath.a
lib/CMSIS_DSP/libCMSISDSPStatistics.a 
lib/CMSIS_DSP/libCMSISDSPTransform.a 
lib/CMSIS_DSP/libCMSISDSPCommon.a 
lib/CMSIS_DSP/libCMSISDSPSupport.a
lib/ANR.c lib/NB.c lib/NR_spectral.c 
lib/libzstd.a lib/libliquid.a 
src/wbfmpll.cpp lib/libfoxenflac.a 
lib/dav1d.cpp lib/libdav1d.a
lib/libopus.a -s
$(node extract_EXPORTED_FUNCTIONS.js ../html-svelte/src/lib/AudioProcessor.js ../html-svelte/src/modules/LiquidDSP.js ../html-svelte/src/lib/wrappers.js \
../html-svelte/src/modules/FoxenFlac.js)
"

COMPILE_OPTIONS="
-O3 -flto
-s MODULARIZE=1 
--bind
-s EXPORTED_RUNTIME_METHODS=['cwrap','ccall','getValue','setValue'] 
-s EXPORT_ES6=1
-s LLD_REPORT_UNDEFINED
-s FILESYSTEM=0
-s USE_ES6_IMPORT_META=0
-s ENVIRONMENT='web,worker'
-s TOTAL_MEMORY=8MB
-Iinclude
"

emcc $COMPILE_OPTIONS $OPTIONS -s WASM=1 -o build/jsDSP.js
emcc $COMPILE_OPTIONS $OPTIONS -s WASM=0 -s LEGACY_VM_SUPPORT=1 -o build/jsDSPnoWasm.js
emcc $COMPILE_OPTIONS --no-entry lib/dav1d.cpp lib/libdav1d.a -s WASM=1 -o build/dav1d.js
emcc $COMPILE_OPTIONS --no-entry lib/dav1d.cpp lib/libdav1d.a -s WASM=0 -s LEGACY_VM_SUPPORT=1 -o build/dav1dnoWasm.js
emcc $COMPILE_OPTIONS --no-entry lib/libopus.a -s WASM=1 -o build/opus.js
emcc $COMPILE_OPTIONS --no-entry lib/libopus.a -s WASM=0 -s LEGACY_VM_SUPPORT=1 -o build/opusnoWasm.js
cp build/* ../html-svelte/src/modules/

