// Copyright 2017 The Brave Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MUON_APP_MUON_CRASH_REPORTER_CLIENT_H_
#define MUON_APP_MUON_CRASH_REPORTER_CLIENT_H_

#include "chrome/app/chrome_crash_reporter_client.h"

class MuonCrashReporterClient : public ChromeCrashReporterClient {
 public:
  MuonCrashReporterClient(
      const std::string& product_name,
      const std::string& product_version);
  ~MuonCrashReporterClient() override;

#if defined(OS_POSIX) && !defined(OS_MACOSX)
  void GetProductNameAndVersion(const char** product_name,
                                const char** version) override;
#endif

#if defined(OS_MACOSX)
  bool ShouldMonitorCrashHandlerExpensively() override;
#endif
  bool GetCollectStatsConsent() override;

 private:
  const std::string product_name_;
  const std::string product_version_;

  DISALLOW_COPY_AND_ASSIGN(MuonCrashReporterClient);
};

#endif  // MUON_APP_MUON_CRASH_REPORTER_CLIENT_H_
