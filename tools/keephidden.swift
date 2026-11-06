// keephidden <pid> <seconds> [bundle id]: keeps Ryujinx (or the private copy with that bundle id, tools/rj-slot.sh) hidden (re-hides within ~20 ms if it shows or activates) while <pid> runs, then exits.
// Hides every running Ryujinx (bundle org.ryujinx.Ryujinx), not only <pid>: run-probe only starts when none was running, so they are
// all this run's, and its launch stages (a short-lived first pid, then the real one) are covered from the first moment.
// Counts, after the first second, how often <pid> had un-hidden and how often it was active.
import AppKit
let pid = pid_t(CommandLine.arguments[1])!, secs = Double(CommandLine.arguments[2])!
let bundle = CommandLine.arguments.count > 3 ? CommandLine.arguments[3] : "org.ryujinx.Ryujinx"
let start = Date(); var unhid = 0, active = 0
while Date().timeIntervalSince(start) < secs {
  if kill(pid, 0) != 0 { break }                       // the process itself is gone
  let late = Date().timeIntervalSince(start) > 1
  for app in NSRunningApplication.runningApplications(withBundleIdentifier: bundle) {
    if app.processIdentifier == pid && late { if app.isActive { active += 1 }; if !app.isHidden { unhid += 1 } }
    if !app.isHidden || app.isActive { app.hide() }
  }
  usleep(20_000)
}
print("after startup: un-hidden \(unhid)x, active \(active)x")
