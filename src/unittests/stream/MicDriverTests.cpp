/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MicDriverTests.h"

#include "stream/Microphone.h"

#include <QString>
#include <QTest>

#include <vector>

namespace {

AudioObjectPropertyAddress address(AudioObjectPropertySelector selector, AudioObjectPropertyScope scope)
{
  return AudioObjectPropertyAddress{selector, scope, kAudioObjectPropertyElementMain};
}

template <typename T>
T propertyValue(
    AudioServerPlugInDriverRef driver, AudioObjectID object, AudioObjectPropertySelector selector,
    AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal
)
{
  const auto where = address(selector, scope);
  T value{};
  UInt32 size = 0;
  if ((*driver)->GetPropertyData(driver, object, 0, &where, 0, nullptr, sizeof(T), &size, &value) != noErr ||
      size != sizeof(T)) {
    return T{};
  }
  return value;
}

std::vector<AudioObjectID> objects(
    AudioServerPlugInDriverRef driver, AudioObjectID object, AudioObjectPropertySelector selector,
    AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal
)
{
  const auto where = address(selector, scope);
  UInt32 size = 0;
  if ((*driver)->GetPropertyDataSize(driver, object, 0, &where, 0, nullptr, &size) != noErr) {
    return {};
  }
  std::vector<AudioObjectID> ids(size / sizeof(AudioObjectID));
  if (!ids.empty() &&
      (*driver)->GetPropertyData(driver, object, 0, &where, 0, nullptr, size, &size, ids.data()) != noErr) {
    return {};
  }
  return ids;
}

QString name(AudioServerPlugInDriverRef driver, AudioObjectID object)
{
  return QString::fromCFString(propertyValue<CFStringRef>(driver, object, kAudioObjectPropertyName));
}

//! The device called \p wanted
AudioObjectID device(AudioServerPlugInDriverRef driver, const QString &wanted)
{
  for (const auto id : objects(driver, kAudioObjectPlugInObject, kAudioPlugInPropertyDeviceList)) {
    if (name(driver, id) == wanted) {
      return id;
    }
  }
  return kAudioObjectUnknown;
}

} // namespace

void MicDriverTests::initTestCase()
{
  const auto path = QStringLiteral(HOPFLOW_MIC_DRIVER).toCFString();
  CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, path, kCFURLPOSIXPathStyle, true);
  CFRelease(path);
  m_plugIn = CFPlugInCreate(nullptr, url);
  CFRelease(url);
  QVERIFY2(m_plugIn != nullptr, HOPFLOW_MIC_DRIVER);

  // the lookup coreaudiod does: Info.plist maps the AudioServerPlugIn type to our factory
  CFArrayRef factories = CFPlugInFindFactoriesForPlugInType(kAudioServerPlugInTypeUUID);
  QVERIFY(factories != nullptr);
  QCOMPARE(CFArrayGetCount(factories), CFIndex(1));
  auto factory = static_cast<CFUUIDRef>(CFArrayGetValueAtIndex(factories, 0));
  auto **unknown = static_cast<IUnknownVTbl **>(CFPlugInInstanceCreate(nullptr, factory, kAudioServerPlugInTypeUUID));
  CFRelease(factories);
  QVERIFY(unknown != nullptr);

  void *driver = nullptr;
  QCOMPARE(
      (*unknown)->QueryInterface(unknown, CFUUIDGetUUIDBytes(kAudioServerPlugInDriverInterfaceUUID), &driver), S_OK
  );
  (*unknown)->Release(unknown);
  m_driver = static_cast<AudioServerPlugInDriverRef>(driver);
  // the driver never calls back into its host
  static const AudioServerPlugInHostInterface host{};
  QCOMPARE((*m_driver)->Initialize(m_driver, &host), OSStatus(noErr));
}

void MicDriverTests::cleanupTestCase()
{
  if (m_driver != nullptr) {
    (*m_driver)->Release(m_driver);
  }
  if (m_plugIn != nullptr) {
    CFRelease(m_plugIn);
  }
}

void MicDriverTests::publishesFeedAndMicrophone()
{
  const auto devices = objects(m_driver, kAudioObjectPlugInObject, kAudioPlugInPropertyDeviceList);
  QCOMPARE(devices.size(), size_t(2));

  const auto feed = device(m_driver, QStringLiteral("Hopflow Mic Feed"));
  const auto mic = device(m_driver, QStringLiteral("Hopflow Microphone"));
  QVERIFY(feed != kAudioObjectUnknown);
  QVERIFY(mic != kAudioObjectUnknown);

  // only the microphone shows up in apps
  QCOMPARE(propertyValue<UInt32>(m_driver, feed, kAudioDevicePropertyIsHidden), 1u);
  QCOMPARE(propertyValue<UInt32>(m_driver, mic, kAudioDevicePropertyIsHidden), 0u);
  QCOMPARE(propertyValue<Float64>(m_driver, mic, kAudioDevicePropertyNominalSampleRate), 48000.0);
  QCOMPARE(propertyValue<UInt32>(m_driver, mic, kAudioDevicePropertyDeviceIsAlive), 1u);
}

void MicDriverTests::translatesFeedUid()
{
  // MacVirtualMicSink finds the hidden feed by this UID
  const auto where = address(kAudioPlugInPropertyTranslateUIDToDevice, kAudioObjectPropertyScopeGlobal);
  const auto uid = QString::fromLatin1(hopflow::stream::kMacMicFeedUid).toCFString();
  AudioObjectID found = kAudioObjectUnknown;
  UInt32 size = 0;
  QCOMPARE(
      (*m_driver)->GetPropertyData(
          m_driver, kAudioObjectPlugInObject, 0, &where, sizeof(uid), &uid, sizeof(found), &size, &found
      ),
      OSStatus(noErr)
  );
  CFRelease(uid);
  QCOMPARE(found, device(m_driver, QStringLiteral("Hopflow Mic Feed")));
  QCOMPARE(
      QString::fromCFString(propertyValue<CFStringRef>(m_driver, found, kAudioDevicePropertyDeviceUID)),
      QString::fromLatin1(hopflow::stream::kMacMicFeedUid)
  );
}

void MicDriverTests::streamsMatchDirection()
{
  const auto feed = device(m_driver, QStringLiteral("Hopflow Mic Feed"));
  const auto mic = device(m_driver, QStringLiteral("Hopflow Microphone"));

  QCOMPARE(objects(m_driver, feed, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput).size(), size_t(1));
  QVERIFY(objects(m_driver, feed, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput).empty());
  QCOMPARE(objects(m_driver, mic, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput).size(), size_t(1));
  QVERIFY(objects(m_driver, mic, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput).empty());

  const auto stream = objects(m_driver, mic, kAudioDevicePropertyStreams).front();
  QCOMPARE(propertyValue<UInt32>(m_driver, stream, kAudioStreamPropertyDirection), 1u);
  const auto format = propertyValue<AudioStreamBasicDescription>(m_driver, stream, kAudioStreamPropertyVirtualFormat);
  QCOMPARE(format.mSampleRate, 48000.0);
  QCOMPARE(format.mChannelsPerFrame, 2u);
  QVERIFY(format.mFormatFlags & kAudioFormatFlagIsFloat);
}

void MicDriverTests::loopsFeedIntoMicrophone()
{
  const auto feed = device(m_driver, QStringLiteral("Hopflow Mic Feed"));
  const auto mic = device(m_driver, QStringLiteral("Hopflow Microphone"));
  const auto feedStream = objects(m_driver, feed, kAudioDevicePropertyStreams).front();
  const auto micStream = objects(m_driver, mic, kAudioDevicePropertyStreams).front();
  QCOMPARE((*m_driver)->StartIO(m_driver, feed, 0), OSStatus(noErr));
  QCOMPARE((*m_driver)->StartIO(m_driver, mic, 0), OSStatus(noErr));
  QCOMPARE(propertyValue<UInt32>(m_driver, mic, kAudioDevicePropertyDeviceIsRunning), 1u);

  // three IO cycles played into the feed
  constexpr UInt32 kFrames = 512;
  const AudioServerPlugInIOCycleInfo cycle{};
  std::vector<float> played(kFrames * 2);
  for (UInt32 i = 0; i < 3 * kFrames; i += kFrames) {
    for (UInt32 frame = 0; frame < kFrames; ++frame) {
      played[frame * 2] = played[frame * 2 + 1] = float(i + frame);
    }
    QCOMPARE(
        (*m_driver)->DoIOOperation(
            m_driver, feed, feedStream, 0, kAudioServerPlugInIOOperationWriteMix, kFrames, &cycle, played.data(),
            nullptr
        ),
        OSStatus(noErr)
    );
  }

  // come out of the microphone in order
  std::vector<float> recorded(kFrames * 2, -1.0f);
  QCOMPARE(
      (*m_driver)->DoIOOperation(
          m_driver, mic, micStream, 0, kAudioServerPlugInIOOperationReadInput, kFrames, &cycle, recorded.data(), nullptr
      ),
      OSStatus(noErr)
  );
  QCOMPARE(recorded[0], 0.0f);
  QCOMPARE(recorded[2], 1.0f);
  QCOMPARE(recorded[kFrames * 2 - 1], float(kFrames - 1));

  QCOMPARE((*m_driver)->StopIO(m_driver, mic, 0), OSStatus(noErr));
  QCOMPARE((*m_driver)->StopIO(m_driver, feed, 0), OSStatus(noErr));
  QCOMPARE(propertyValue<UInt32>(m_driver, mic, kAudioDevicePropertyDeviceIsRunning), 0u);
}

void MicDriverTests::clockStartsAtZero()
{
  const auto mic = device(m_driver, QStringLiteral("Hopflow Microphone"));
  QCOMPARE((*m_driver)->StartIO(m_driver, mic, 0), OSStatus(noErr));
  Float64 sampleTime = -1;
  UInt64 hostTime = 0;
  UInt64 seed = 0;
  QCOMPARE((*m_driver)->GetZeroTimeStamp(m_driver, mic, 0, &sampleTime, &hostTime, &seed), OSStatus(noErr));
  QCOMPARE(sampleTime, 0.0);
  QVERIFY(hostTime > 0);
  QCOMPARE(seed, UInt64(1));
  QCOMPARE((*m_driver)->StopIO(m_driver, mic, 0), OSStatus(noErr));
}

QTEST_MAIN(MicDriverTests)
