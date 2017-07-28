// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

// REF(bridiver) ChromeMainDelegate

#include "atom/app/atom_main_delegate.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "atom/app/atom_content_client.h"
#include "atom/browser/atom_browser_client.h"
#include "atom/browser/relauncher.h"
#include "atom/common/atom_command_line.h"
#include "atom/common/atom_version.h"
#include "atom/common/google_api_key.h"
#include "atom/utility/atom_content_utility_client.h"
#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/debug/leak_annotations.h"
#include "base/debug/stack_trace.h"
#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/trace_event/trace_log.h"
#include "brave/common/brave_paths.h"
#include "brightray/browser/brightray_paths.h"
#include "brightray/common/application_info.h"
#include "browser/brightray_paths.h"
#include "chrome/child/child_profiling.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/crash_keys.h"
#include "chrome/common/logging_chrome.h"
#include "chrome/common/profiling.h"
#include "chrome/common/trace_event_args_whitelist.h"
#include "components/component_updater/component_updater_paths.h"
#include "components/content_settings/core/common/content_settings_pattern.h"
#include "components/crash/content/app/crashpad.h"
#include "content/public/common/content_switches.h"
#include "extensions/common/constants.h"
#include "extensions/features/features.h"
#include "muon/app/muon_crash_reporter_client.h"
#include "printing/features/features.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_switches.h"

#if defined(OS_WIN)
#include <atlbase.h>
#include <malloc.h>
#include <algorithm>
#include "base/debug/close_handle_hook_win.h"
#include "chrome/child/v8_breakpad_support_win.h"
#include "chrome/common/child_process_logging.h"
#include "chrome/install_static/install_details.h"
#include "chrome/install_static/install_util.h"
#include "chrome/install_static/product_install_details.h"
#include "sandbox/win/src/sandbox.h"
#include "ui/base/resource/resource_bundle_win.h"
#endif

#if defined(OS_MACOSX)
#include "base/mac/foundation_util.h"
#include "chrome/app/chrome_main_mac.h"
#include "chrome/browser/mac/relauncher.h"
#include "chrome/common/mac/cfbundle_blocker.h"
#include "components/crash/core/common/objc_zombie.h"
#include "ui/base/l10n/l10n_util_mac.h"
#endif

#if defined(OS_POSIX)
#include <locale.h>
#include <signal.h>
#endif

#if defined(OS_POSIX) && !defined(OS_MACOSX)
#include "components/crash/content/app/breakpad_linux.h"
#endif

#if defined(OS_MACOSX) || defined(OS_WIN)
#include "components/crash/content/app/crashpad.h"
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "brave/browser/brave_content_browser_client.h"
#include "brave/renderer/brave_content_renderer_client.h"
#endif

namespace atom {

namespace {

const char* kRelauncherProcess = "relauncher";

bool IsBrowserProcess(base::CommandLine* cmd) {
  std::string process_type = cmd->GetSwitchValueASCII(::switches::kProcessType);
  return process_type.empty();
}

#if defined(OS_WIN)
// If we try to access a path that is not currently available, we want the call
// to fail rather than show an error dialog.
void SuppressWindowsErrorDialogs() {
  UINT new_flags = SEM_FAILCRITICALERRORS |
                   SEM_NOOPENFILEERRORBOX;

  // Preserve existing error mode.
  UINT existing_flags = SetErrorMode(new_flags);
  SetErrorMode(existing_flags | new_flags);
}

bool IsSandboxedProcess() {
  typedef bool (*IsSandboxedProcessFunc)();
  IsSandboxedProcessFunc is_sandboxed_process_func =
      reinterpret_cast<IsSandboxedProcessFunc>(
          GetProcAddress(GetModuleHandle(NULL), "IsSandboxedProcess"));
  return is_sandboxed_process_func && is_sandboxed_process_func();
}

void InvalidParameterHandler(const wchar_t*, const wchar_t*, const wchar_t*,
                             unsigned int, uintptr_t) {
  // noop.
}

bool UseHooks() {
#if defined(ARCH_CPU_X86_64)
  return false;
#elif defined(NDEBUG)
  return false;
#else  // NDEBUG
  return true;
#endif
}

#endif  // defined(OS_WIN)

struct MainFunction {
  const char* name;
  int (*function)(const content::MainFunctionParams&);
};

void InitLogging(const std::string& process_type) {
  logging::OldFileDeletionState file_state =
      logging::APPEND_TO_OLD_LOG_FILE;
  if (process_type.empty()) {
    file_state = logging::DELETE_OLD_LOG_FILE;
  }
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  logging::InitChromeLogging(command_line, file_state);
}

base::FilePath InitializeUserDataDir() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  base::FilePath user_data_dir;

  // first check the env
  std::string user_data_dir_string;
  std::unique_ptr<base::Environment> environment(base::Environment::Create());
  if (environment->GetVar("BRAVE_USER_DATA_DIR", &user_data_dir_string) &&
      base::IsStringUTF8(user_data_dir_string)) {
    user_data_dir = base::FilePath::FromUTF8Unsafe(user_data_dir_string);
    command_line->AppendSwitchPath(switches::kUserDataDir, user_data_dir);
  }

  // next check the user-data-dir switch
  if (user_data_dir.empty() ||
      command_line->HasSwitch(switches::kUserDataDir)) {
    user_data_dir =
      command_line->GetSwitchValuePath(switches::kUserDataDir);
    if (!user_data_dir.empty() && !user_data_dir.IsAbsolute()) {
      base::FilePath app_data_dir;
      brave::GetDefaultAppDataDirectory(&app_data_dir);
      user_data_dir = app_data_dir.Append(user_data_dir);
    }
  }

  // On Windows, trailing separators leave Chrome in a bad state.
  // See crbug.com/464616.
  if (user_data_dir.EndsWithSeparator())
    user_data_dir = user_data_dir.StripTrailingSeparators();

  if (!user_data_dir.empty())
    PathService::OverrideAndCreateIfNeeded(brightray::DIR_USER_DATA,
          user_data_dir, false, true);

  // TODO(bridiver) Warn and fail early if the process fails
  // to get a user data directory.
  if (!PathService::Get(brightray::DIR_USER_DATA, &user_data_dir)) {
    brave::GetDefaultUserDataDirectory(&user_data_dir);
    PathService::OverrideAndCreateIfNeeded(brightray::DIR_USER_DATA,
            user_data_dir, false, true);
  }
  PathService::Override(chrome::DIR_USER_DATA, user_data_dir);

  // Setup NativeMessagingHosts to point to the default Chrome locations
  // because that's where native apps will create them
#if defined(OS_POSIX)
  base::FilePath default_user_data_dir;
  brave::GetDefaultUserDataDirectory(&default_user_data_dir);
  std::vector<base::FilePath::StringType> components;
  default_user_data_dir.GetComponents(&components);
  // remove "brave"
  components.pop_back();
  base::FilePath chrome_user_data_dir(FILE_PATH_LITERAL("/"));
  for (std::vector<base::FilePath::StringType>::iterator i =
           components.begin() + 1; i != components.end(); ++i) {
    chrome_user_data_dir = chrome_user_data_dir.Append(*i);
  }
  chrome_user_data_dir = chrome_user_data_dir.Append("Google/Chrome");
  PathService::OverrideAndCreateIfNeeded(
      chrome::DIR_USER_NATIVE_MESSAGING,
      chrome_user_data_dir.Append(FILE_PATH_LITERAL("NativeMessagingHosts")),
      false, true);

  base::FilePath native_messaging_dir;
#if defined(OS_MACOSX)
  native_messaging_dir = base::FilePath(FILE_PATH_LITERAL(
      "/Library/Google/Chrome/NativeMessagingHosts"));
#else
  native_messaging_dir = base::FilePath(FILE_PATH_LITERAL(
      "/etc/opt/chrome/native-messaging-hosts"));
#endif  // !defined(OS_MACOSX)
  PathService::OverrideAndCreateIfNeeded(chrome::DIR_NATIVE_MESSAGING,
      native_messaging_dir, false, true);
#endif  // defined(OS_POSIX)

  return user_data_dir;
}

#if defined(OS_POSIX) && !defined(OS_MACOSX)
void SIGTERMProfilingShutdown(int signal) {
  Profiling::Stop();
  struct sigaction sigact;
  memset(&sigact, 0, sizeof(sigact));
  sigact.sa_handler = SIG_DFL;
  CHECK_EQ(sigaction(SIGTERM, &sigact, NULL), 0);
  raise(signal);
}

void SetUpProfilingShutdownHandler() {
  struct sigaction sigact;
  sigact.sa_handler = SIGTERMProfilingShutdown;
  sigact.sa_flags = SA_RESETHAND;
  sigemptyset(&sigact.sa_mask);
  CHECK_EQ(sigaction(SIGTERM, &sigact, NULL), 0);
}
#endif  // !defined(OS_MACOSX) && !defined(OS_ANDROID)

}  // namespace

AtomMainDelegate::AtomMainDelegate()
    : AtomMainDelegate(base::TimeTicks()) {}

AtomMainDelegate::AtomMainDelegate(base::TimeTicks exe_entry_point_ticks) {
}

AtomMainDelegate::~AtomMainDelegate() {
}

bool AtomMainDelegate::BasicStartupComplete(int* exit_code) {
  auto command_line = base::CommandLine::ForCurrentProcess();

#if defined(OS_MACOSX)
  // Give the browser process a longer treadmill, since crashes
  // there have more impact.
  const bool is_browser = !command_line->HasSwitch(switches::kProcessType);
  ObjcEvilDoers::ZombieEnable(true, is_browser ? 10000 : 1000);

  SetUpBundleOverrides();
  chrome::common::mac::EnableCFBundleBlocker();
#endif

#if !defined(CHROME_MULTIPLE_DLL_BROWSER)
  ChildProfiling::ProcessStarted();
#endif
  Profiling::ProcessStarted();

  base::trace_event::TraceLog::GetInstance()->SetArgumentFilterPredicate(
      base::Bind(&IsTraceEventArgsWhitelisted));

#if defined(OS_WIN)
  v8_breakpad_support::SetUp();
#endif

#if defined(OS_WIN)
  if (UseHooks())
    base::debug::InstallHandleHooks();
  else
    base::win::DisableHandleVerifier();

  // Ignore invalid parameter errors.
  _set_invalid_parameter_handler(InvalidParameterHandler);
#endif

  chrome::RegisterPathProvider();

  ContentSettingsPattern::SetNonWildcardDomainNonPortScheme(
      extensions::kExtensionScheme);

  return brightray::MainDelegate::BasicStartupComplete(exit_code);
}

void AtomMainDelegate::PreSandboxStartup() {
  auto command_line = base::CommandLine::ForCurrentProcess();
  std::string process_type = command_line->GetSwitchValueASCII(
      ::switches::kProcessType);

  std::string product_name = ATOM_PRODUCT_NAME;
  if (command_line->HasSwitch("muon_product_name"))
    product_name = command_line->GetSwitchValueASCII("muon_product_name");

  std::string product_version = ATOM_VERSION_STRING;
  if (command_line->HasSwitch("muon_product_version"))
    product_version = command_line->GetSwitchValueASCII("muon_product_version");

  MuonCrashReporterClient* crash_client =
      new MuonCrashReporterClient(product_name, product_version);
  ANNOTATE_LEAKING_OBJECT_PTR(crash_client);
  crash_reporter::SetCrashReporterClient(crash_client);

#if defined(OS_MACOSX)
  InitMacCrashReporter(command_line, process_type);
#elif defined(OS_POSIX) && !defined(OS_MACOSX)
  // Zygote needs to call InitCrashReporter() in RunZygote().
  if (command_line->HasSwitch(switches::kEnableCrashReporter) &&
      process_type != switches::kZygoteProcess) {
    LOG(ERROR) << "enable crash reporter for " << process_type;
    breakpad::InitCrashReporter(process_type);
  }
#elif defined(OS_WIN)
  if (command_line->HasSwitch(switches::kEnableCrashReporter)
    breakpad::InitCrashReporter(process_type);

  install_static::InitializeProcessType();
  child_process_logging::Init();
#endif

  base::FilePath path = InitializeUserDataDir();
  if (path.empty()) {
    LOG(ERROR) << "Could not create user data dir";
  } else {
    PathService::OverrideAndCreateIfNeeded(
        component_updater::DIR_COMPONENT_USER,
        path.Append(FILE_PATH_LITERAL("Extensions")), false, true);
  }

#if !defined(OS_WIN)
  // For windows we call InitLogging when the sandbox is initialized.
  InitLogging(process_type);
#endif

  // Set google API key.
  std::unique_ptr<base::Environment> env(base::Environment::Create());
  if (!env->HasVar("GOOGLE_API_ENDPOINT"))
    env->SetVar("GOOGLE_API_ENDPOINT", GOOGLEAPIS_ENDPOINT);
  if (!env->HasVar("GOOGLE_API_KEY"))
    env->SetVar("GOOGLE_API_KEY", GOOGLEAPIS_API_KEY);

#if !defined(CHROME_MULTIPLE_DLL_BROWSER)
  if (process_type == switches::kUtilityProcess ||
      process_type == switches::kZygoteProcess) {
    AtomContentUtilityClient::PreSandboxStartup();
  }
#endif

#if defined(OS_LINUX)
  if (!IsBrowserProcess(command_line)) {
    // Disable setuid sandbox
    command_line->AppendSwitch(::switches::kDisableSetuidSandbox);
  }
#endif

  crash_keys::SetCrashKeysFromCommandLine(*command_line);
  brightray::MainDelegate::PreSandboxStartup();
}

#if defined(OS_POSIX) && !defined(OS_MACOSX)
void AtomMainDelegate::ZygoteForked() {
  ChildProfiling::ProcessStarted();
  Profiling::ProcessStarted();
  if (Profiling::BeingProfiled()) {
    base::debug::RestartProfilingAfterFork();
    SetUpProfilingShutdownHandler();
  }

  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  std::string process_type =
      command_line->GetSwitchValueASCII(
          switches::kProcessType);
  if (command_line->HasSwitch(switches::kEnableCrashReporter)) {
    breakpad::InitCrashReporter(process_type);
    // Reset the command line for the newly spawned process.
    crash_keys::SetCrashKeysFromCommandLine(*command_line);
  }
}
#endif

content::ContentBrowserClient* AtomMainDelegate::CreateContentBrowserClient() {
  browser_client_.reset(new brave::BraveContentBrowserClient);
  return browser_client_.get();
}

content::ContentRendererClient*
    AtomMainDelegate::CreateContentRendererClient() {
  renderer_client_.reset(new brave::BraveContentRendererClient);
  return renderer_client_.get();
}

content::ContentUtilityClient* AtomMainDelegate::CreateContentUtilityClient() {
  utility_client_.reset(new AtomContentUtilityClient);
  return utility_client_.get();
}

int AtomMainDelegate::RunProcess(
    const std::string& process_type,
    const content::MainFunctionParams& main_function_params) {
    static const MainFunction kMainFunctions[] = {
  #if BUILDFLAG(ENABLE_PRINT_PREVIEW) && !defined(CHROME_MULTIPLE_DLL_CHILD)
      { switches::kCloudPrintServiceProcess, CloudPrintServiceProcessMain },
  #endif

  #if defined(OS_MACOSX)
      { switches::kRelauncherProcess, relauncher::RelauncherMain },
  #else
      { kRelauncherProcess, relauncher::RelauncherMain },
  #endif
    };

    for (size_t i = 0; i < arraysize(kMainFunctions); ++i) {
      if (process_type == kMainFunctions[i].name)
        return kMainFunctions[i].function(main_function_params);
    }

    return -1;
}

void AtomMainDelegate::SandboxInitialized(const std::string& process_type) {
#if defined(OS_WIN)
  InitLogging(process_type);
  SuppressWindowsErrorDialogs();
#endif
}

#if defined(OS_MACOSX)
bool AtomMainDelegate::ShouldSendMachPort(const std::string& process_type) {
  return process_type != switches::kRelauncherProcess &&
      process_type != switches::kCloudPrintServiceProcess;
}

bool AtomMainDelegate::DelaySandboxInitialization(
    const std::string& process_type) {
  return process_type == kRelauncherProcess;
}

void AtomMainDelegate::InitMacCrashReporter(
    base::CommandLine* command_line,
    const std::string& process_type) {
  // TODO(mark): Right now, InitializeCrashpad() needs to be called after
  // CommandLine::Init() and chrome::RegisterPathProvider().  Ideally, Crashpad
  // initialization could occur sooner, preferably even before the framework
  // dylib is even loaded, to catch potential early crashes.

  const bool browser_process = process_type.empty();
  const bool install_from_dmg_relauncher_process =
      process_type == switches::kRelauncherProcess &&
      command_line->HasSwitch(switches::kRelauncherProcessDMGDevice);

  const bool initial_client =
      browser_process || install_from_dmg_relauncher_process;

  crash_reporter::InitializeCrashpad(initial_client, process_type);

  // Mac is packaged with a main app bundle and a helper app bundle.
  // The main app bundle should only be used for the browser process, so it
  // should never see a --type switch (switches::kProcessType).  Likewise,
  // the helper should always have a --type switch.
  //
  // This check is done this late so there is already a call to
  // base::mac::IsBackgroundOnlyProcess(), so there is no change in
  // startup/initialization order.

  // The helper's Info.plist marks it as a background only app.
  if (base::mac::IsBackgroundOnlyProcess()) {
    CHECK(command_line->HasSwitch(switches::kProcessType) &&
          !process_type.empty())
        << "Helper application requires --type.";
  } else {
    CHECK(!command_line->HasSwitch(switches::kProcessType) &&
          process_type.empty())
        << "Main application forbids --type, saw " << process_type;
  }
}
#endif  // OS_MACOSX

void AtomMainDelegate::ProcessExiting(const std::string& process_type) {
  brightray::MainDelegate::ProcessExiting(process_type);
  logging::CleanupChromeLogging();
}

std::unique_ptr<brightray::ContentClient>
AtomMainDelegate::CreateContentClient() {
  return std::unique_ptr<brightray::ContentClient>(new AtomContentClient);
}

}  // namespace atom
