/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXFilePasteboard.h"

#import <AppKit/AppKit.h>

namespace deskflow::osx {

namespace {

std::vector<std::string> filesOn(NSPasteboard *pasteboard)
{
  std::vector<std::string> paths;
  NSArray<NSURL *> *urls = [pasteboard readObjectsForClasses:@[ [NSURL class] ]
                                                     options:@{
                                                       NSPasteboardURLReadingFileURLsOnlyKey : @YES
                                                     }];
  for (NSURL *url in urls) {
    if (url.isFileURL && url.path != nil) {
      paths.emplace_back(url.path.UTF8String);
    }
  }
  return paths;
}

} // namespace

std::vector<std::string> clipboardFiles()
{
  @autoreleasepool {
    return filesOn([NSPasteboard generalPasteboard]);
  }
}

bool setClipboardFiles(const std::vector<std::string> &paths)
{
  @autoreleasepool {
    NSMutableArray<NSURL *> *urls = [NSMutableArray arrayWithCapacity:paths.size()];
    for (const auto &path : paths) {
      [urls addObject:[NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]]];
    }

    NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];
    return [pasteboard writeObjects:urls];
  }
}

long dragPasteboardChangeCount()
{
  @autoreleasepool {
    return [NSPasteboard pasteboardWithName:NSPasteboardNameDrag].changeCount;
  }
}

std::vector<std::string> dragPasteboardFiles()
{
  @autoreleasepool {
    return filesOn([NSPasteboard pasteboardWithName:NSPasteboardNameDrag]);
  }
}

} // namespace deskflow::osx
