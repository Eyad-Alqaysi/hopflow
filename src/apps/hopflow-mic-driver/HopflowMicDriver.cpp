/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

//! Core Audio driver that gives the Mac a virtual microphone fed by Hopflow
/*!
It publishes two devices. Hopflow plays what the other computer's microphone
picks up into the hidden output device "Hopflow Mic Feed", and apps record
it from the input device "Hopflow Microphone". Both run in coreaudiod, so
the audio goes from one to the other through a ring buffer in memory.

Objects and their IDs never change, so properties need no locking; only
the per device IO state does.
*/

#include "LoopbackRing.h"

#include <CoreAudio/AudioServerPlugIn.h>
#include <mach/mach_time.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>

using hopflow::mic::LoopbackRing;

namespace {

constexpr Float64 kSampleRate = 48000.0;
constexpr UInt32 kChannels = 2;
//! Frames between the zero time stamps the HAL uses to follow our clock
constexpr UInt32 kClockPeriodFrames = 16384;

enum ObjectID : AudioObjectID
{
  kPlugIn = kAudioObjectPlugInObject,
  kFeedDevice,
  kFeedStream,
  kMicDevice,
  kMicStream
};

struct DeviceSpec
{
  AudioObjectID device;
  AudioObjectID stream;
  CFStringRef name;
  CFStringRef uid;
  //! Direction of the device's only stream
  bool input;
};

const DeviceSpec kFeed{kFeedDevice, kFeedStream, CFSTR("Hopflow Mic Feed"), CFSTR("HopflowMicFeed_UID"), false};
const DeviceSpec kMic{kMicDevice, kMicStream, CFSTR("Hopflow Microphone"), CFSTR("HopflowMicrophone_UID"), true};

//! Clock and run state of one device
struct DeviceState
{
  std::mutex mutex;
  UInt32 ioClients = 0;
  std::atomic<bool> running{false};
  UInt64 anchorHostTime = 0;
  UInt64 timeStamps = 0;
};

// start 10 ms past what a read asks for, keep no more than 100 ms
LoopbackRing<kChannels, 8192> g_ring(480, 4800);
DeviceState g_feedState;
DeviceState g_micState;
Float64 g_hostTicksPerFrame = 0;
std::atomic<ULONG> g_refCount{0};

const DeviceSpec *deviceFor(AudioObjectID id)
{
  if (id == kFeedDevice || id == kFeedStream) {
    return &kFeed;
  }
  if (id == kMicDevice || id == kMicStream) {
    return &kMic;
  }
  return nullptr;
}

DeviceState *stateFor(AudioObjectID device)
{
  if (device == kFeedDevice) {
    return &g_feedState;
  }
  if (device == kMicDevice) {
    return &g_micState;
  }
  return nullptr;
}

AudioStreamBasicDescription streamFormat()
{
  AudioStreamBasicDescription format{};
  format.mSampleRate = kSampleRate;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  format.mBytesPerPacket = kChannels * sizeof(Float32);
  format.mFramesPerPacket = 1;
  format.mBytesPerFrame = kChannels * sizeof(Float32);
  format.mChannelsPerFrame = kChannels;
  format.mBitsPerChannel = 32;
  return format;
}

//! Answers a property query, either with its size or with its data
class Reply
{
public:
  //! For GetPropertyDataSize: report sizes, write nothing
  Reply() = default;
  Reply(UInt32 capacity, void *data) : m_capacity(capacity), m_data(data)
  {
  }

  template <typename T> OSStatus value(const T &item)
  {
    if (m_data != nullptr) {
      if (m_capacity < sizeof(T)) {
        return kAudioHardwareBadPropertySizeError;
      }
      std::memcpy(m_data, &item, sizeof(T));
    }
    size = sizeof(T);
    return kAudioHardwareNoError;
  }

  //! As many items as fit
  template <typename T> OSStatus array(const T *items, UInt32 count)
  {
    if (m_data != nullptr) {
      count = std::min<UInt32>(count, m_capacity / sizeof(T));
      if (count > 0) {
        std::memcpy(m_data, items, count * sizeof(T));
      }
    }
    size = count * UInt32(sizeof(T));
    return kAudioHardwareNoError;
  }

  UInt32 size = 0;

private:
  UInt32 m_capacity = 0;
  void *m_data = nullptr;
};

OSStatus
plugInProperty(const AudioObjectPropertyAddress &address, UInt32 qualifierSize, const void *qualifier, Reply &reply)
{
  static constexpr AudioObjectID devices[] = {kFeedDevice, kMicDevice};
  switch (address.mSelector) {
  case kAudioObjectPropertyBaseClass:
    return reply.value<AudioClassID>(kAudioObjectClassID);
  case kAudioObjectPropertyClass:
    return reply.value<AudioClassID>(kAudioPlugInClassID);
  case kAudioObjectPropertyOwner:
    return reply.value<AudioObjectID>(kAudioObjectUnknown);
  case kAudioObjectPropertyManufacturer:
    return reply.value(CFSTR("Hopflow"));
  case kAudioObjectPropertyOwnedObjects:
  case kAudioPlugInPropertyDeviceList:
    return reply.array(devices, 2);
  case kAudioPlugInPropertyTranslateUIDToDevice: {
    if (qualifierSize < sizeof(CFStringRef) || qualifier == nullptr) {
      return kAudioHardwareBadPropertySizeError;
    }
    const auto uid = *static_cast<const CFStringRef *>(qualifier);
    AudioObjectID device = kAudioObjectUnknown;
    for (const auto *spec : {&kFeed, &kMic}) {
      if (uid != nullptr && CFStringCompare(uid, spec->uid, 0) == kCFCompareEqualTo) {
        device = spec->device;
      }
    }
    return reply.value(device);
  }
  case kAudioPlugInPropertyResourceBundle:
    return reply.value(CFSTR(""));
  default:
    return kAudioHardwareUnknownPropertyError;
  }
}

OSStatus deviceProperty(const DeviceSpec &spec, const AudioObjectPropertyAddress &address, Reply &reply)
{
  const auto streamScope = spec.input ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput;
  switch (address.mSelector) {
  case kAudioObjectPropertyBaseClass:
    return reply.value<AudioClassID>(kAudioObjectClassID);
  case kAudioObjectPropertyClass:
    return reply.value<AudioClassID>(kAudioDeviceClassID);
  case kAudioObjectPropertyOwner:
    return reply.value<AudioObjectID>(kPlugIn);
  case kAudioObjectPropertyName:
    return reply.value(spec.name);
  case kAudioObjectPropertyManufacturer:
    return reply.value(CFSTR("Hopflow"));
  case kAudioDevicePropertyDeviceUID:
    return reply.value(spec.uid);
  case kAudioDevicePropertyModelUID:
    return reply.value(CFSTR("HopflowMic_ModelUID"));
  case kAudioDevicePropertyTransportType:
    return reply.value<UInt32>(kAudioDeviceTransportTypeVirtual);
  case kAudioDevicePropertyRelatedDevices:
    return reply.array(&spec.device, 1);
  case kAudioDevicePropertyClockDomain:
    return reply.value<UInt32>(0);
  case kAudioDevicePropertyDeviceIsAlive:
    return reply.value<UInt32>(1);
  case kAudioDevicePropertyDeviceIsRunning:
    return reply.value<UInt32>(stateFor(spec.device)->running ? 1 : 0);
  case kAudioDevicePropertyDeviceCanBeDefaultDevice:
    // apps may pick the microphone by default; nothing should play into the feed by accident
    return reply.value<UInt32>(spec.input ? 1 : 0);
  case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
    return reply.value<UInt32>(0);
  case kAudioDevicePropertyLatency:
  case kAudioDevicePropertySafetyOffset:
    return reply.value<UInt32>(0);
  case kAudioObjectPropertyOwnedObjects:
  case kAudioDevicePropertyStreams: {
    const bool matches = address.mScope == kAudioObjectPropertyScopeGlobal || address.mScope == streamScope;
    return reply.array(&spec.stream, matches ? 1 : 0);
  }
  case kAudioObjectPropertyControlList:
    return reply.array<AudioObjectID>(nullptr, 0);
  case kAudioDevicePropertyNominalSampleRate:
    return reply.value(kSampleRate);
  case kAudioDevicePropertyAvailableNominalSampleRates:
    return reply.value(AudioValueRange{kSampleRate, kSampleRate});
  case kAudioDevicePropertyIsHidden:
    return reply.value<UInt32>(spec.input ? 0 : 1);
  case kAudioDevicePropertyZeroTimeStampPeriod:
    return reply.value(kClockPeriodFrames);
  case kAudioDevicePropertyPreferredChannelsForStereo: {
    static constexpr UInt32 channels[] = {1, 2};
    return reply.array(channels, 2);
  }
  default:
    return kAudioHardwareUnknownPropertyError;
  }
}

OSStatus streamProperty(const DeviceSpec &spec, const AudioObjectPropertyAddress &address, Reply &reply)
{
  switch (address.mSelector) {
  case kAudioObjectPropertyBaseClass:
    return reply.value<AudioClassID>(kAudioObjectClassID);
  case kAudioObjectPropertyClass:
    return reply.value<AudioClassID>(kAudioStreamClassID);
  case kAudioObjectPropertyOwner:
    return reply.value(spec.device);
  case kAudioObjectPropertyOwnedObjects:
    return reply.array<AudioObjectID>(nullptr, 0);
  case kAudioStreamPropertyIsActive:
    return reply.value<UInt32>(1);
  case kAudioStreamPropertyDirection:
    return reply.value<UInt32>(spec.input ? 1 : 0);
  case kAudioStreamPropertyTerminalType:
    return reply.value<UInt32>(spec.input ? kAudioStreamTerminalTypeMicrophone : kAudioStreamTerminalTypeSpeaker);
  case kAudioStreamPropertyStartingChannel:
    return reply.value<UInt32>(1);
  case kAudioStreamPropertyLatency:
    return reply.value<UInt32>(0);
  case kAudioStreamPropertyVirtualFormat:
  case kAudioStreamPropertyPhysicalFormat:
    return reply.value(streamFormat());
  case kAudioStreamPropertyAvailableVirtualFormats:
  case kAudioStreamPropertyAvailablePhysicalFormats:
    return reply.value(AudioStreamRangedDescription{streamFormat(), AudioValueRange{kSampleRate, kSampleRate}});
  default:
    return kAudioHardwareUnknownPropertyError;
  }
}

OSStatus property(
    AudioObjectID object, const AudioObjectPropertyAddress *address, UInt32 qualifierSize, const void *qualifier,
    Reply &reply
)
{
  if (address == nullptr) {
    return kAudioHardwareIllegalOperationError;
  }
  if (object == kPlugIn) {
    return plugInProperty(*address, qualifierSize, qualifier, reply);
  }
  const auto *spec = deviceFor(object);
  if (spec == nullptr) {
    return kAudioHardwareBadObjectError;
  }
  return object == spec->device ? deviceProperty(*spec, *address, reply) : streamProperty(*spec, *address, reply);
}

//
// The driver interface
//

HRESULT queryInterface(void *driver, REFIID uuid, LPVOID *out);

ULONG addRef(void *)
{
  return ++g_refCount;
}

ULONG release(void *)
{
  // never below zero, even if the host releases once too often
  auto count = g_refCount.load();
  while (count > 0 && !g_refCount.compare_exchange_weak(count, count - 1)) {
  }
  return count > 0 ? count - 1 : 0;
}

OSStatus initialize(AudioServerPlugInDriverRef, AudioServerPlugInHostRef)
{
  mach_timebase_info_data_t timebase{};
  mach_timebase_info(&timebase);
  const Float64 ticksPerSecond = 1'000'000'000.0 * timebase.denom / timebase.numer;
  g_hostTicksPerFrame = ticksPerSecond / kSampleRate;
  return kAudioHardwareNoError;
}

OSStatus createDevice(AudioServerPlugInDriverRef, CFDictionaryRef, const AudioServerPlugInClientInfo *, AudioObjectID *)
{
  return kAudioHardwareUnsupportedOperationError;
}

OSStatus destroyDevice(AudioServerPlugInDriverRef, AudioObjectID)
{
  return kAudioHardwareUnsupportedOperationError;
}

OSStatus deviceClient(AudioServerPlugInDriverRef, AudioObjectID, const AudioServerPlugInClientInfo *)
{
  return kAudioHardwareNoError;
}

OSStatus configurationChange(AudioServerPlugInDriverRef, AudioObjectID, UInt64, void *)
{
  return kAudioHardwareNoError;
}

Boolean hasProperty(AudioServerPlugInDriverRef, AudioObjectID object, pid_t, const AudioObjectPropertyAddress *address)
{
  // the translation needs a qualifier, but the property exists all the same
  if (object == kPlugIn && address != nullptr && address->mSelector == kAudioPlugInPropertyTranslateUIDToDevice) {
    return true;
  }
  Reply reply;
  return property(object, address, 0, nullptr, reply) == kAudioHardwareNoError;
}

OSStatus isPropertySettable(
    AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t process, const AudioObjectPropertyAddress *address,
    Boolean *out
)
{
  if (out == nullptr) {
    return kAudioHardwareIllegalOperationError;
  }
  if (!hasProperty(driver, object, process, address)) {
    return kAudioHardwareUnknownPropertyError;
  }
  // a single fixed format: nothing can be changed
  *out = false;
  return kAudioHardwareNoError;
}

OSStatus getPropertyDataSize(
    AudioServerPlugInDriverRef, AudioObjectID object, pid_t, const AudioObjectPropertyAddress *address,
    UInt32 qualifierSize, const void *qualifier, UInt32 *outSize
)
{
  if (outSize == nullptr) {
    return kAudioHardwareIllegalOperationError;
  }
  if (object == kPlugIn && address != nullptr && address->mSelector == kAudioPlugInPropertyTranslateUIDToDevice) {
    *outSize = sizeof(AudioObjectID);
    return kAudioHardwareNoError;
  }
  Reply reply;
  const auto status = property(object, address, qualifierSize, qualifier, reply);
  *outSize = reply.size;
  return status;
}

OSStatus getPropertyData(
    AudioServerPlugInDriverRef, AudioObjectID object, pid_t, const AudioObjectPropertyAddress *address,
    UInt32 qualifierSize, const void *qualifier, UInt32 size, UInt32 *outSize, void *outData
)
{
  if (outSize == nullptr || outData == nullptr) {
    return kAudioHardwareIllegalOperationError;
  }
  Reply reply(size, outData);
  const auto status = property(object, address, qualifierSize, qualifier, reply);
  *outSize = reply.size;
  return status;
}

OSStatus setPropertyData(
    AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress *, UInt32, const void *, UInt32,
    const void *
)
{
  return kAudioHardwareUnsupportedOperationError;
}

OSStatus startIO(AudioServerPlugInDriverRef, AudioObjectID device, UInt32)
{
  auto *state = stateFor(device);
  if (state == nullptr) {
    return kAudioHardwareBadObjectError;
  }
  std::scoped_lock lock(state->mutex);
  if (state->ioClients++ == 0) {
    state->anchorHostTime = mach_absolute_time();
    state->timeStamps = 0;
    state->running = true;
  }
  return kAudioHardwareNoError;
}

OSStatus stopIO(AudioServerPlugInDriverRef, AudioObjectID device, UInt32)
{
  auto *state = stateFor(device);
  if (state == nullptr) {
    return kAudioHardwareBadObjectError;
  }
  std::scoped_lock lock(state->mutex);
  if (state->ioClients == 0) {
    return kAudioHardwareIllegalOperationError;
  }
  if (--state->ioClients == 0) {
    state->running = false;
  }
  return kAudioHardwareNoError;
}

//! A clock that ticks at exactly the sample rate on the host's time base
OSStatus getZeroTimeStamp(
    AudioServerPlugInDriverRef, AudioObjectID device, UInt32, Float64 *outSampleTime, UInt64 *outHostTime,
    UInt64 *outSeed
)
{
  auto *state = stateFor(device);
  if (state == nullptr) {
    return kAudioHardwareBadObjectError;
  }
  std::scoped_lock lock(state->mutex);
  const Float64 ticksPerPeriod = g_hostTicksPerFrame * kClockPeriodFrames;
  const auto nextPeriod = state->anchorHostTime + UInt64(Float64(state->timeStamps + 1) * ticksPerPeriod);
  if (nextPeriod <= mach_absolute_time()) {
    ++state->timeStamps;
  }
  *outSampleTime = Float64(state->timeStamps * kClockPeriodFrames);
  *outHostTime = state->anchorHostTime + UInt64(Float64(state->timeStamps) * ticksPerPeriod);
  *outSeed = 1;
  return kAudioHardwareNoError;
}

OSStatus willDoIOOperation(
    AudioServerPlugInDriverRef, AudioObjectID device, UInt32, UInt32 operation, Boolean *outWillDo, Boolean *outInPlace
)
{
  if (outWillDo == nullptr || outInPlace == nullptr) {
    return kAudioHardwareIllegalOperationError;
  }
  *outWillDo = (device == kFeedDevice && operation == kAudioServerPlugInIOOperationWriteMix) ||
               (device == kMicDevice && operation == kAudioServerPlugInIOOperationReadInput);
  *outInPlace = true;
  return kAudioHardwareNoError;
}

OSStatus
ioBoundary(AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, UInt32, const AudioServerPlugInIOCycleInfo *)
{
  return kAudioHardwareNoError;
}

OSStatus doIOOperation(
    AudioServerPlugInDriverRef, AudioObjectID device, AudioObjectID stream, UInt32, UInt32 operation, UInt32 frames,
    const AudioServerPlugInIOCycleInfo *, void *mainBuffer, void *
)
{
  if (mainBuffer == nullptr) {
    return kAudioHardwareIllegalOperationError;
  }
  if (device == kFeedDevice && stream == kFeedStream && operation == kAudioServerPlugInIOOperationWriteMix) {
    g_ring.write(static_cast<const float *>(mainBuffer), frames);
  } else if (device == kMicDevice && stream == kMicStream && operation == kAudioServerPlugInIOOperationReadInput) {
    g_ring.read(static_cast<float *>(mainBuffer), frames);
  }
  return kAudioHardwareNoError;
}

AudioServerPlugInDriverInterface g_interface = {
    nullptr,
    queryInterface,
    addRef,
    release,
    initialize,
    createDevice,
    destroyDevice,
    deviceClient,
    deviceClient,
    configurationChange,
    configurationChange,
    hasProperty,
    isPropertySettable,
    getPropertyDataSize,
    getPropertyData,
    setPropertyData,
    startIO,
    stopIO,
    getZeroTimeStamp,
    willDoIOOperation,
    ioBoundary,
    doIOOperation,
    ioBoundary
};
AudioServerPlugInDriverInterface *g_interfacePointer = &g_interface;

HRESULT queryInterface(void *, REFIID uuid, LPVOID *out)
{
  if (out == nullptr) {
    return kAudioHardwareIllegalOperationError;
  }
  CFUUIDRef requested = CFUUIDCreateFromUUIDBytes(nullptr, uuid);
  if (requested == nullptr) {
    return kAudioHardwareIllegalOperationError;
  }
  const bool known = CFEqual(requested, IUnknownUUID) || CFEqual(requested, kAudioServerPlugInDriverInterfaceUUID);
  CFRelease(requested);
  if (!known) {
    *out = nullptr;
    return E_NOINTERFACE;
  }
  addRef(nullptr);
  *out = &g_interfacePointer;
  return S_OK;
}

} // namespace

//! The factory named in Info.plist; coreaudiod calls it to load the driver
extern "C" __attribute__((visibility("default"))) void *HopflowMic_Create(CFAllocatorRef, CFUUIDRef requestedType)
{
  if (!CFEqual(requestedType, kAudioServerPlugInTypeUUID)) {
    return nullptr;
  }
  addRef(nullptr);
  return &g_interfacePointer;
}
