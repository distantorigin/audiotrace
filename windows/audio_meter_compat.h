#ifndef AUDIO_TRACE_METER_COMPAT_H
#define AUDIO_TRACE_METER_COMPAT_H

#include <endpointvolume.h>

/* Some MinGW-w64 SDKs only forward-declare this standard Windows interface.
   ABI matches Microsoft's endpointvolume.h; no third-party runtime is needed. */
#ifndef __IAudioMeterInformation_INTERFACE_DEFINED__
#define __IAudioMeterInformation_INTERFACE_DEFINED__
typedef struct IAudioMeterInformationVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IAudioMeterInformation *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IAudioMeterInformation *);
    ULONG (STDMETHODCALLTYPE *Release)(IAudioMeterInformation *);
    HRESULT (STDMETHODCALLTYPE *GetPeakValue)(IAudioMeterInformation *, float *);
    HRESULT (STDMETHODCALLTYPE *GetMeteringChannelCount)(IAudioMeterInformation *, UINT *);
    HRESULT (STDMETHODCALLTYPE *GetChannelsPeakValues)(IAudioMeterInformation *, UINT32, float *);
    HRESULT (STDMETHODCALLTYPE *QueryHardwareSupport)(IAudioMeterInformation *, DWORD *);
} IAudioMeterInformationVtbl;
struct IAudioMeterInformation { CONST_VTBL IAudioMeterInformationVtbl *lpVtbl; };
static const IID IID_IAudioMeterInformation = {
    0xc02216f6, 0x8c67, 0x4b5b, {0x9d, 0x00, 0xd0, 0x08, 0xe7, 0x3e, 0x00, 0x64}
};
#define IAudioMeterInformation_Release(This) ((This)->lpVtbl->Release(This))
#define IAudioMeterInformation_GetPeakValue(This, peak) ((This)->lpVtbl->GetPeakValue(This, peak))
#endif

#endif
