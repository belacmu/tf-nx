// TF.EX's MatchReport on the Switch (translate.toml [[rewrite]] / [[replace]]): where it meets the PC.
using System;
using System.Threading;
using System.Threading.Tasks;

namespace TfexGlue
{
    public static class Reports
    {
        static Thread worker;

        [Cpp("tfex::gen::ffi::Log(\"[tfex] MatchReport: an upload already runs: this report dropped\")", Include = "tfexgen_ffi.hpp")]
        static extern void LogBusy();

        // `_ = Task.Run(() => Upload(...))` (OnReplayExported): the upload (the zip, then a blocking POST) on a worker, as on the PC, off the
        // game thread. TFEX-DIFF S-native: a game System.Threading.Thread (managed, so the GC knows it: the translated Upload allocates),
        // one at a time (a second report while one uploads is dropped and logged: the runtime's zip buffer is single).
        public static void RunUpload(Func<Task> work)
        {
            if (worker != null && worker.IsAlive) { LogBusy(); return; }
            worker = new Thread(() => work());
            worker.IsBackground = true;
            worker.Start();
        }

        // MatchReport.ReadCurrentLog: TF.EX reads its own log file (the one this process started, from <game>/Logs, its last 4 MB).
        // TFEX-DIFF S-native: the Switch's session log is the hw log (sd:/xplay/hw-log.txt, hwlog.cpp: this boot's first 1 MB and a
        // rolling tail), flushed, then read whole (it is at most 1.6 MB, under TF.EX's 4 MB).
        [Cpp("tfex::gen::bcl::CurrentLog()", Include = "tfexgen_bcl.hpp")]
        public static extern byte[] CurrentLog();
    }
}
