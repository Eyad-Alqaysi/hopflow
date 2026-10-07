/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2024 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QString>

// important: this is used for settings paths on some platforms,
// and must not be a url. qt automatically converts this to reverse domain
// notation (rdn), e.g. io.github.eyad-alqaysi
const auto kOrgDomain = QStringLiteral("eyad-alqaysi.github.io");

const auto kUrlApp = QStringLiteral("https://github.com/Eyad-Alqaysi/hopflow");
const auto kUrlHelp = QStringLiteral("%1/issues").arg(kUrlApp);
const auto kUrlDownload = QStringLiteral("%1/releases/latest").arg(kUrlApp);
const auto kUrlWiki = QStringLiteral("%1#readme").arg(kUrlApp);
const auto kUrlUpdateCheck =
    QStringLiteral("https://raw.githubusercontent.com/Eyad-Alqaysi/hopflow/main/LATEST_VERSION");
const auto kUrlDeskflow = QStringLiteral("https://github.com/deskflow/deskflow");

#if defined(Q_OS_LINUX)
const auto kUrlGnomeTrayFix = QStringLiteral("https://extensions.gnome.org/extension/615/appindicator-support/");
#endif
