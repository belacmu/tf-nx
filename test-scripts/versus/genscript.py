#!/usr/bin/env python3
# Generates the four xplay2 scripts (seats 0-3; 2 and 3 for 3-4 player lobbies) (frame:flags:moveX:moveY steps, flags hex: 1 jump, 2 shoot, 4 dodge, 8 alt shoot).
# Pseudo-random but fixed (python random.Random(seed)); every flagged step is followed by a one-tick release so that
# the next flagged step is a fresh press (pressed = rising edge).
import random, sys
ticks = int(sys.argv[1]) if len(sys.argv) > 1 else 2400
def gen(seed, facing):
    r = random.Random(seed)
    steps, t = [], 0
    while t < ticks:
        dur = r.choice([6, 8, 10, 14, 20, 30, 45])
        kind = r.random()
        mx = facing if r.random() < 0.55 else r.choice([-1, 0, 1])
        my = r.choice([0, 0, 0, -1, 1])
        flags = 0
        if kind < 0.30: flags |= 1          # jump
        if 0.45 < kind < 0.70: flags |= 2   # shoot
        if 0.70 < kind < 0.80: flags |= 4   # dodge
        if 0.80 < kind < 0.84: flags |= 8   # alt shoot
        steps.append((t, flags, mx, my))
        if flags:
            steps.append((t + 1, 0, mx, my))
        t += dur
    steps.append((ticks, 0, 0, 0))
    return ",".join(f"{f}:{fl:x}:{x}:{y}" for f, fl, x, y in steps)
scripts = [gen(1001, 1), gen(2002, -1), gen(3003, 1), gen(4004, -1)]
for i, s in enumerate(scripts):
    open(sys.argv[2] + f"/script{i}.txt", "w").write(s)
print(*map(len, scripts))
