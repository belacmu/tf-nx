#!/bin/bash
# rematch-test.sh: the end of a lobby match and a rematch, as short as it gets. An INSTANT match (first kill wins) between the Switch (Ryujinx,
# hosts) and one page; at the results both vote REMATCH! at once (Switch: hw.txt lobbyvote 1, 3 s after its results finish; page: xlobby
# vote=rematch, right after its MatchEnded), the server sends RematchLobby, and the run ends as soon as the second match has synchronized:
# vote -> RematchLobby -> back to the lobby screen -> next match (new seed, new WebRTC room session, fresh undo log) -> synchronized.
# Pass "lobbypred 0" etc. to change the rest. Output: tools/lobby-auto.sh's.
cd "$(dirname "$0")/.."
LVB_XLOBBY_EXTRA="vote=rematch${LVB_XLOBBY_EXTRA:+ $LVB_XLOBBY_EXTRA}" LVB_UNTIL="lobby match #2 synchronized" \
  tools/lobby-auto.sh -s 300 "rngcapture 1" "lobbyui 1" "lobbyscript 1" "lobbypred 8" "lobbylength 0" "lobbyvote 1" "vsstop 9000" "$@"
