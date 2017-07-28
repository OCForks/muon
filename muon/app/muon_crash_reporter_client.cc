// Copyright 2017 The Brave Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "muon/app/muon_crash_reporter_client.h"

#include "base/base_switches.h"
#include "base/command_line.h"
#include "chrome/browser/browser_process_impl.h"
#include "components/crash/content/app/crashpad.h"
#include "components/metrics/metrics_pref_names.h"
#include "components/prefs/pref_service.h"

MuonCrashReporterClient::MuonCrashReporterClient(
    const std::string& product_name, const std::string& product_version)
    : product_name_(product_name),
      product_version_(product_version) {}

MuonCrashReporterClient::~MuonCrashReporterClient() {}

#if defined(OS_POSIX) && !defined(OS_MACOSX)
void MuonCrashReporterClient::GetProductNameAndVersion(
    const char** product_name,
    const char** version) {
  DCHECK(product_name);
  DCHECK(version);
  *product_name = product_name_.c_str();
  *version = product_version_.c_str();
}
#endif

#if defined(OS_MACOSX)
bool MuonCrashReporterClient::ShouldMonitorCrashHandlerExpensively() {
  return false;
}
#endif

bool MuonCrashReporterClient::GetCollectStatsConsent() {
#if defined(OS_WIN) || defined(OS_MACOSX)
  return crash_reporter::GetUploadsEnabled();
#else
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  return command_line->HasSwitch(switches::kEnableCrashReporter) ||
      !command_line->HasSwitch(switches::kDisableBreakpad);
#endif
}
