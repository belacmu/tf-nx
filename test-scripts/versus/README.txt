xplay2 evidence (browser branch exp/xplay-versus, commit c3dee1e on 7f4d734 on 6028684)

script0.txt / script1.txt   exact script strings (frame:flags:moveX:moveY steps), 5,200 ticks, made by genscript.py 5200 <dir>
runonce.sh                  how a run is done: node tools/netplay-driver.mjs --url "http://localhost:18080/?mute&nointro&autoplay&mods=TF.EX&fortrise&tfexfps=60&command=<urlencoded: xplay2 <script0> <script1> 42 5000>" --players XV1 --port 19400
                            (page served by: python3 tools/serve.py 18080 from the worktree), then GET /XV1/console, keep the "[xplay2" lines
x-r9.txt, x-r10.txt         two fresh-browser runs, identical byte for byte (sha1 f59533b16aa4cb1c8c473447d57f71476bee3b49)
x-r11.txt, x-r12.txt        later builds of the harness (r12 = final): same lines as r9 except [xplay2setup] extras and the cfg line
x-roff.txt                  final build with replay=off: T/A/U lines identical to r9
x-ctl.txt                   negative control (impure=1): desync detected at T=11 (see negative-control-desync.txt)
sample-first700-last100.txt header, last 5 pre-origin ticks, T 0..700, T 4901..5000 of r11
