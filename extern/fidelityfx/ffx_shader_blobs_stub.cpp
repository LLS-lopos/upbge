// ffx_shader_blobs_stub.cpp
//
// Null stubs for FidelityFX shader blob accessors not compiled into UPBGE.
//
// ffx_shader_blobs.cpp dispatches GetShaderBlobVK() calls by effect at
// runtime. For effects whose provider is never created (everything except
// FSR3 upscaler), the dispatch never executes — but the linker still needs
// the symbol defined. These stubs satisfy that requirement with zero cost.
//
// If you add another FidelityFX effect to UPBGE later, remove its stub
// here and add its real shaderblobs.cpp to the CMakeLists sources.

#include <cstddef>

// Signature matches what ffx_shader_blobs.cpp calls via function pointer:
//   void effect_GetPermutationBlobByIndex(
//       uint32_t permutationOptions,
//       const uint8_t** outBlob,
//       uint32_t* outSize);
//
// Returning outSize=0 / outBlob=nullptr is safe because these contexts
// are never created — the dispatch branch is never reached at runtime.

#define FFX_BLOB_STUB(fnName)                               \
    void fnName(unsigned int, const unsigned char** ppBlob, \
                unsigned int* pSize)                        \
    {                                                       \
        if (ppBlob) *ppBlob = nullptr;                      \
        if (pSize)  *pSize  = 0;                            \
    }

// Effects present in the SDK but not used in UPBGE:
FFX_BLOB_STUB(fsr1GetPermutationBlobByIndex)
FFX_BLOB_STUB(fsr2GetPermutationBlobByIndex)
FFX_BLOB_STUB(casGetPermutationBlobByIndex)
FFX_BLOB_STUB(spdGetPermutationBlobByIndex)
FFX_BLOB_STUB(cacaoGetPermutationBlobByIndex)
FFX_BLOB_STUB(blurGetPermutationBlobByIndex)
FFX_BLOB_STUB(dofGetPermutationBlobByIndex)
FFX_BLOB_STUB(lensGetPermutationBlobByIndex)
FFX_BLOB_STUB(lpmGetPermutationBlobByIndex)
FFX_BLOB_STUB(vrsGetPermutationBlobByIndex)
FFX_BLOB_STUB(classifierGetPermutationBlobByIndex)
FFX_BLOB_STUB(denoiserGetPermutationBlobByIndex)
FFX_BLOB_STUB(sssrGetPermutationBlobByIndex)
FFX_BLOB_STUB(parallelsortGetPermutationBlobByIndex)
FFX_BLOB_STUB(frameinterpolationGetPermutationBlobByIndex)
FFX_BLOB_STUB(opticalflowGetPermutationBlobByIndex)
FFX_BLOB_STUB(brixelizerGetPermutationBlobByIndex)
FFX_BLOB_STUB(brixelizergiGetPermutationBlobByIndex)
FFX_BLOB_STUB(breadcrumbsGetPermutationBlobByIndex)

#undef FFX_BLOB_STUB