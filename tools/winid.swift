// winid <pid>: the CGWindowID of that process's largest normal-layer window (screencapture -l; tools/panel-check.sh)
import CoreGraphics
import Foundation
let pid = Int(CommandLine.arguments[1])!
let list = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as! [[String: Any]]
var best = 0, area = 0.0
for w in list where (w[kCGWindowOwnerPID as String] as? Int) == pid && (w[kCGWindowLayer as String] as? Int) == 0 {
  let b = w[kCGWindowBounds as String] as! [String: Double]; let a = b["Width"]! * b["Height"]!
  if a > area { area = a; best = w[kCGWindowNumber as String] as! Int }
}
print(best)
