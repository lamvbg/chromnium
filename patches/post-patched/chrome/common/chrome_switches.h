// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Defines the shared command-line switches used by code in the Chrome
// directory that don't have anywhere more specific to go.

#ifndef CHROME_COMMON_CHROME_SWITCHES_H_
#define CHROME_COMMON_CHROME_SWITCHES_H_

#include "build/build_config.h"
#include "chrome/common/buildflags.h"
#include "extensions/buildflags/buildflags.h"
#include "printing/buildflags/buildflags.h"
#include "ui/base/buildflags.h"

// Don't add more switch files here. This is linked into some places like the
// installer where dependencies should be limited. Instead, have files
// directly include your switch file.

namespace switches {

// -----------------------------------------------------------------------------
// Can't find the switch you are looking for? Try looking in
// media/base/media_switches.cc or ui/gl/gl_switches.cc or one of the
// .cc files corresponding to the *_switches.h files included above
// instead.
//
// Want to remove obsolete switches? Ensure that the switch isn't still in use
// by the Android Java code (ChromeSwitches.java.tmpl) under an aliased name.
// Also perform a string search to make sure the switch isn't in use only by a
// build-configuration, e.g. BUILDFLAG(GOOGLE_CHROME_BRANDING), that is not
// indexed for cross-reference or built by the CQ bots.
// -----------------------------------------------------------------------------

// All switches in alphabetical order. The switches should be documented
// alongside the definition of their values in the .cc file.
extern const char kAcceptLang[];
#if BUILDFLAG(IS_MAC)
extern const char kAllowAppShimSignatureMismatchForTests[];
#endif
extern const char kAllowCrossOriginAuthPrompt[];
extern const char kAllowHttpScreenCapture[];
extern const char kAllowRunningInsecureContent[];
extern const char kAllowSilentPush[];
extern const char kApp[];
extern const char kAppId[];
extern const char kAppLaunchUrlForShortcutsMenuItem[];
extern const char kAppRunOnOsLoginMode[];
extern const char kAppShim[];
extern const char kAppsGalleryUpdateURL[];
extern const char kAuthServerAllowlist[];
extern const char kAutoOpenDevToolsForTabs[];
extern const char kAutoSelectDesktopCaptureSource[];
extern const char kAutoSelectScreenCaptureSource[];
extern const char kAutoSelectTabCaptureSourceByTitle[];
extern const char kAutoSelectWindowCaptureSourceByTitle[];
extern const char kBrowserSigninAutoAccept[];
extern const char kBypassAccountAlreadyUsedByAnotherProfileCheck[];
extern const char kCaptureAutoReject[];
extern const char kCheckForUpdateIntervalSec[];
extern const char kCipherSuiteBlacklist[];
extern const char kCreateBrowserOnStartupForTests[];
extern const char kCredits[];
extern const char kCustomDevtoolsFrontend[];
extern const char kDebugPackedApps[];
extern const char kDevToolsFlags[];
extern const char kDiagnostics[];
extern const char kDiagnosticsFormat[];
extern const char kDiagnosticsRecovery[];
#if BUILDFLAG(IS_CHROMEOS)
extern const char kDisableAutoMaximizeForTests[];
#endif
extern const char kDisableAutoReload[];
extern const char kDisableBackgroundNetworking[];
extern const char kDisableClientSidePhishingDetection[];
extern const char kDisableComponentExtensionsWithBackgroundPages[];
extern const char kDisableComponentUpdate[];
extern const char kDisableCrashpadForTesting[];
extern const char kDisableDefaultApps[];
extern const char kDisableDomainReliability[];
extern const char kDisableLazyLoading[];
extern const char kDisablePrintPreview[];
extern const char kDisablePromptOnRepost[];
extern const char kDisableStackProfiler[];
extern const char kDisableUpdaterScheduler[];
extern const char kDisableZeroBrowsersOpenForTests[];
extern const char kDiskCacheDir[];
extern const char kDiskCacheSize[];
#if BUILDFLAG(IS_MAC)
extern const char kDoNotCreateNSAppForTests[];
#endif
extern const char kDoNotDeElevateOnLaunch[];
extern const char kDumpBrowserHistograms[];
extern const char kEnableAudioDebugRecordingsFromExtension[];
extern const char kEnableAutoReload[];
extern const char kEnableBookmarkUndo[];
extern const char kEnableDomainReliability[];
extern const char kEnableDevToolsGreenDevUi[];
#if BUILDFLAG(IS_CHROMEOS)
extern const char kEnableDevToolsPwaHandler[];
#endif
extern const char kEnableDownloadWarningImprovements[];
extern const char kEnableExtensionActivityLogging[];
extern const char kEnableExtensionActivityLogTesting[];
extern const char kEnableUnsafeExtensionDebugging[];
extern const char kEnableHangoutServicesExtensionForTesting[];
extern const char kEnableNetBenchmarking[];
extern const char kEnablePotentiallyAnnoyingSecurityFeatures[];
extern const char kExperimentalAiStableChannel[];
extern const char kExplicitlyAllowedPorts[];
extern const char kExtensionAiDataCollection[];
extern const char kExtensionContentVerification[];
extern const char kExtensionContentVerificationBootstrap[];
extern const char kExtensionContentVerificationEnforce[];
extern const char kExtensionContentVerificationEnforceStrict[];
extern const char kExtensionExperimentalActor[];
extern const char kForceAppMode[];
#if BUILDFLAG(IS_CHROMEOS)
extern const char kForceDevToolsAvailable[];
#endif
extern const char kForceFirstRun[];
extern const char kRefreshPlatformPolicy[];
extern const char kForceWhatsNew[];

// Chronium: path to a per-profile fingerprint JSON config file.
extern const char kFingerprintProfile[];

// Chronium (internal): 64-char hex seed propagated from browser to child
// processes so blink-side patches can apply noise synchronously from the
// first frame, without waiting on an async Mojo roundtrip. Browser sets
// this in AppendExtraCommandLineSwitches when a fingerprint profile is
// loaded.
extern const char kFingerprintSeedHex[];

// Chronium (internal): per-profile spoofed WebGL UNMASKED_VENDOR_WEBGL
// (37445) and UNMASKED_RENDERER_WEBGL (37446) strings, propagated from
// browser to renderers.
extern const char kFingerprintWebGLVendor[];
extern const char kFingerprintWebGLRenderer[];

// Chronium (internal): per-profile spoofed navigator.platform value.
// (navigator.userAgent and HTTP User-Agent are handled by the existing
// --user-agent flag, which Chronium auto-attaches from the profile JSON
// at browser startup.)
extern const char kFingerprintPlatform[];

// Chronium (internal): per-profile screen properties, formatted as
// "W,H,AW,AH,CD,DPR" (six comma-separated numbers).
extern const char kFingerprintScreen[];

// Chronium (internal): "concurrency,deviceMemory,maxTouchPoints".
extern const char kFingerprintHardware[];

// Chronium (internal): "level,charging(0|1),chargingTime,dischargingTime".
extern const char kFingerprintBattery[];

// Chronium (internal): WebRTC IP handling policy. 1=proxy_only,
// 2=public_only.
extern const char kFingerprintWebRtcPolicy[];

// Chronium (internal): Accept-Language / navigator.languages override.
// Comma-separated list, e.g. "en-US,en".
extern const char kFingerprintAcceptLanguage[];

// Chronium (internal): IANA timezone override, e.g. "America/New_York".
extern const char kFingerprintTimezone[];

// Chronium (internal): comma-separated font family allowlist. Empty/absent
// means no restriction.
extern const char kFingerprintFonts[];

// Chronium (internal): performance.memory.jsHeapSizeLimit override.
// Format: single integer (bytes), e.g. "4294705152" for 4GB.
extern const char kFingerprintMemory[];

// Chronium (internal): navigator.connection override.
// Format: "effective_type,downlink_mbps,rtt_msec,save_data(0|1)"
// e.g. "4g,10.0,75,0".
extern const char kFingerprintConnection[];

// Chronium (internal): navigator.storage.estimate() quota override (bytes).
// Single integer, e.g. "10737418240" for 10 GB.
extern const char kFingerprintStorageQuota[];

// Chronium (internal): WebGL GL_MAX_TEXTURE_SIZE / GL_MAX_VERTEX_ATTRIBS
// limits override. Format: "max_texture,max_vertex". e.g. "32768,16".
extern const char kFingerprintWebGLLimits[];

// Chronium (internal): speechSynthesis.getVoices() override.
// Format: newline-separated lines, each "name<tab>lang<tab>default(0|1)
// <tab>local(0|1)<tab>uri". URL-decoded; this flag must be base16-encoded
// in the command line to survive the platform argv parser.
extern const char kFingerprintSpeechVoices[];

// Chronium (internal): PublicKeyCredential.isUserVerifyingPlatformAuthenticatorAvailable
// override. "0" = force false, "1" = force true, absent = no override.
extern const char kFingerprintWebAuthnUvpa[];

// Chronium (internal): WebGL getSupportedExtensions() override. List of
// extension names joined by '\n' and hex-encoded over the CLI to dodge
// argv-splitting on whitespace.
extern const char kFingerprintWebGLExtensions[];

// Chronium (internal): WebGL getParameter() + getShaderPrecisionFormat()
// surface override. TSV blob (hex-encoded; same encoding rationale as
// the extension list above) where every line is one of:
//   i\t<pname-hex>\t<int-value>
//   ir\t<pname-hex>\t<int>\t<int>
//   fr\t<pname-hex>\t<float>\t<float>
//   sp\t<shader-hex>\t<precision-hex>\t<rmin>\t<rmax>\t<precision>
// Empty / absent = no override.
extern const char kFingerprintWebGLParams[];

// Chronium (internal): per-launch HMAC license token. Three switches
// together — the antidetect launcher generates them on each chronium
// spawn, the browser-process EnforceLicenseGate() validates them and
// silently exits if anything is off. See chrome/browser/license/.
// kLicenseTs    : decimal unix timestamp (seconds)
// kLicenseNonce : 32-char lowercase hex (16 random bytes)
// kLicenseToken : 32-char lowercase hex (16 truncated HMAC bytes)
extern const char kLicenseTs[];
extern const char kLicenseNonce[];
extern const char kLicenseToken[];

// Chronium (internal): account label rendered as a small chip in the
// LocationBarView (URL bar), to the left of the security icon. The
// antidetect launcher passes the per-profile `name` field here so users
// running 10+ chronium windows at once can tell them apart at a glance.
// Empty / absent = no chip rendered (vanilla LocationBarView).
extern const char kAccountLabel[];

// Chronium (internal): AudioContext sample noise toggle. "0" = disable,
// "1" = enable. Absent = enabled (legacy default). Set "0" when the
// profile pins a real-but-vanilla device that anti-bot traps would
// flag the noised samples on (creepjs / iphey).
extern const char kFingerprintAudioNoise[];

extern const char kHideCrashRestoreBubble[];
extern const char kHomePage[];
#if !BUILDFLAG(IS_ANDROID)
extern const char kImportPasswords[];
#endif
extern const char kIncognito[];
extern const char kInitIsolateAsForeground[];
extern const char kInstallAutogeneratedTheme[];
extern const char kInstallChromeApp[];
extern const char kInstallIsolatedWebAppFromFile[];
extern const char kInstallIsolatedWebAppFromUrl[];
extern const char kInstantProcess[];
extern const char kKeepAliveForTest[];
extern const char kKioskMode[];
extern const char kKioskModePrinting[];
extern const char kLaunchInProcessSimpleBrowserSwitch[];
extern const char kLaunchSimpleBrowserSwitch[];
extern const char kMakeDefaultBrowser[];
extern const char kNativeMessagingConnectHost[];
extern const char kNativeMessagingConnectExtension[];
extern const char kNativeMessagingConnectId[];
extern const char kNoDefaultBrowserCheck[];
extern const char kNoExperiments[];
extern const char kNoFirstRun[];
extern const char kNoPings[];
extern const char kNoProxyServer[];
extern const char kNoStartupWindow[];
extern const char kOnTheFlyMhtmlHashComputation[];
extern const char kOpenInNewWindow[];
extern const char kFocus[];
extern const char kFocusResultFile[];
extern const char kPackExtension[];
extern const char kPackExtensionKey[];
extern const char kPreCrashpadCrashTest[];
extern const char kPredictionServiceMockLikelihood[];
extern const char kPreinstalledWebAppsDir[];
extern const char kPrivetIPv6Only[];
extern const char kProductVersion[];
extern const char kProfileDirectory[];
extern const char kIgnoreProfileDirectoryIfNotExists[];
extern const char kProfileEmail[];
extern const char kCreateProfileEmailIfNotExists[];
extern const char kProxyAutoDetect[];
extern const char kProxyBypassList[];
extern const char kProxyPacUrl[];
extern const char kProxyServer[];
extern const char kRemoteDebuggingTargets[];
extern const char kRepairAllValidExtensions[];
extern const char kRestart[];
extern const char kRestoreLastSession[];
extern const char kSameTab[];
extern const char kSilentDebuggerExtensionAPI[];
extern const char kSilentLaunch[];
extern const char kSimulateBrowsingDataLifetime[];
extern const char kSimulateCriticalUpdate[];
extern const char kSimulateOutdated[];
extern const char kSimulateOutdatedNoAU[];
extern const char kSimulateUpgrade[];
extern const char kSimulateIdleTimeout[];
extern const char kSSLVersionMax[];
extern const char kSSLVersionMin[];
extern const char kSSLVersionTLSv12[];
extern const char kSSLVersionTLSv13[];
extern const char kStartMaximized[];
extern const char kStartStackProfiler[];
extern const char kStartStackProfilerBrowserTest[];
extern const char kStoragePressureNotificationInterval[];
extern const char kSystemAudioCaptureDefaultChecked[];
extern const char kTabCaptureAudioDefaultUnchecked[];
extern const char kThisTabCaptureAutoAccept[];
extern const char kThisTabCaptureAutoReject[];
extern const char kTestMemoryLogDelayInMinutes[];
extern const char kTrustedDownloadSources[];
extern const char kTtcBundleUrl[];
extern const char kUnlimitedStorage[];
extern const char kUnsafelyDisableDevToolsSelfXssWarnings[];
extern const char kUserDataDir[];
extern const char kUseSystemProxyResolver[];
extern const char kValidateCrx[];
extern const char kVersion[];
extern const char kWebRtcRemoteEventLogProactivePruningDelta[];
extern const char kWebRtcRemoteEventLogUploadDelayMs[];
extern const char kWebRtcRemoteEventLogUploadNoSuppression[];
extern const char kWebRtcIPHandlingPolicy[];
extern const char kWhatsNewUseStaging[];
extern const char kWindowName[];
extern const char kWindowPosition[];
extern const char kWindowSize[];
extern const char kWindowWorkspace[];
extern const char kWinHttpProxyResolver[];
extern const char kWinJumplistAction[];

#if BUILDFLAG(IS_ANDROID)
extern const char kAuthAndroidNegotiateAccountType[];
extern const char kDisableDefaultBrowserPromo[];
extern const char kForceDeviceOwnership[];
extern const char kForceEnableNightMode[];
extern const char kForceShowUpdateMenuBadge[];
extern const char kForceShowUpdateMenuItemCustomSummary[];
extern const char kForceEnableSigninFRE[];
extern const char kForceDisableSigninFRE[];
extern const char kForceUpdateMenuType[];
extern const char kMarketUrlForTesting[];
extern const char kRequestDesktopSites[];
#endif  // BUILDFLAG(IS_ANDROID)

#if !BUILDFLAG(IS_ANDROID) || BUILDFLAG(ENABLE_DESKTOP_ANDROID_EXTENSIONS)
extern const char kCastMirroringTargetPlayoutDelay[];
#endif

#if BUILDFLAG(IS_CHROMEOS)
extern const char kCroshCommand[];
extern const char kDisableLoggingRedirect[];
extern const char kDisableLoginScreenApps[];
extern const char kShortMergeSessionTimeoutForTest[];
#else
extern const char kSavePageAsMHTML[];
#endif  // BUILDFLAG(IS_CHROMEOS)

#if BUILDFLAG(IS_POSIX) && !BUILDFLAG(IS_MAC) && !BUILDFLAG(IS_CHROMEOS)
extern const char kHelp[];
extern const char kHelpShort[];
extern const char kWmClass[];
#endif

#if BUILDFLAG(IS_MAC)
extern const char kAppsKeepChromeAliveInTests[];
extern const char kEnableUserMetrics[];
extern const char kMetricsClientID[];
extern const char kRelauncherProcess[];
extern const char kRelauncherProcessDMGDevice[];
extern const char kMakeChromeDefault[];
extern const char kCodeSignCloneCleanupProcess[];
extern const char kUniqueTempDirSuffix[];
#endif  // BUILDFLAG(IS_MAC)

#if BUILDFLAG(IS_WIN)
extern const char kEnableProfileShortcutManager[];
extern const char kFromBrowserSwitcher[];
extern const char kFromInstaller[];
extern const char kHideIcons[];
extern const char kNoNetworkProfileWarning[];
extern const char kNoPreReadMainDll[];
extern const char kNotificationInlineReply[];
extern const char kNotificationLaunchId[];
extern const char kPrefetchArgumentBrowserBackground[];
extern const char kPwaLauncherVersion[];
extern const char kShowIcons[];
extern const char kSourceAppId[];
extern const char kSourceShortcut[];
extern const char kStartupForegroundLaunch[];
extern const char kUninstall[];
extern const char kUninstallAppId[];
extern const char kIsolated[];
#endif  // BUILDFLAG(IS_WIN)

#if BUILDFLAG(ENABLE_PRINT_PREVIEW) && !defined(OFFICIAL_BUILD)
extern const char kDebugPrint[];
#endif

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_MAC) || \
    BUILDFLAG(IS_WIN)
extern const char kGuest[];
#endif

extern const char kGlicGuestURL[];
extern const char kGlicAlwaysOpenFre[];
extern const char kGlicAlwaysSkipFre[];
extern const char kGlicFreURL[];
extern const char kGlicShortcutsLearnMoreURL[];
extern const char kGlicOpenOnStartup[];
extern const char kGlicAllowedOrigins[];
extern const char kGlicAutomation[];
extern const char kGlicDev[];
extern const char kGlicSkipReloadAfterNavigation[];
extern const char kGlicHostLogging[];
extern const char kGlicAdminRedirectPatterns[];
extern const char kGlicAlwaysShowWebActuationToggle[];
extern const char kGlicGuestUrlPresetAutopush[];
extern const char kGlicGuestUrlPresetStaging[];
extern const char kGlicGuestUrlPresetPreprod[];
extern const char kGlicGuestUrlPresetProd[];

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
extern const char kListApps[];
extern const char kProfileBaseName[];
extern const char kProfileManagementAttributes[];
#endif

#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
extern const char kWebApkServerUrl[];
#endif

#if !BUILDFLAG(IS_CHROMEOS) && !BUILDFLAG(IS_ANDROID)
extern const char kUseSystemDefaultPrinter[];
#endif

#if BUILDFLAG(ENABLE_DOWNGRADE_PROCESSING)
extern const char kUserDataMigrated[];
#endif

// DON'T ADD RANDOM STUFF HERE. Put it in the main section above in
// alphabetical order, or in one of the ifdefs (also in order in each section).

}  // namespace switches

#endif  // CHROME_COMMON_CHROME_SWITCHES_H_
