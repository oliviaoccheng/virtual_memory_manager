"""Diagram helpers + all README figures, drawn with matplotlib for exact placement."""
import sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch
from matplotlib import font_manager as fm

for f in fm.findSystemFonts():
    if "Inter" in f:
        fm.fontManager.addfont(f)
plt.rcParams["font.family"] = "Inter"

OUT = sys.argv[1]

INK = "#222222"
MUTED = "#555555"
GRAY = ("#EEEEEE", "#888888")
GREEN = ("#D7F0DD", "#2E8B57")
AMBER = ("#FFE9C7", "#C98300")
BLUE = ("#D6E6FA", "#2F6FB5")
PURPLE = ("#E9E1F7", "#6B4FA8")
RED = ("#FBE0DE", "#B83A30")


def fig(w, h):
    f, ax = plt.subplots(figsize=(w, h))
    ax.set_xlim(0, w)
    ax.set_ylim(0, h)
    ax.axis("off")
    ax.set_aspect("equal")
    return f, ax


class Box:
    def __init__(self, ax, x, y, w, h, title, sub=None, color=GRAY, title_size=13, sub_size=9.5):
        self.x, self.y, self.w, self.h = x, y, w, h
        ax.add_patch(FancyBboxPatch((x - w / 2, y - h / 2), w, h,
                                    boxstyle="round,pad=0,rounding_size=0.12",
                                    fc=color[0], ec=color[1], lw=1.6, zorder=2))
        if sub:
            ax.text(x, y + h * 0.13, title, ha="center", va="center", fontsize=title_size,
                    fontweight="bold", color=INK, zorder=3)
            ax.text(x, y - h * 0.2, sub, ha="center", va="center", fontsize=sub_size,
                    color=MUTED, zorder=3, linespacing=1.3)
        else:
            ax.text(x, y, title, ha="center", va="center", fontsize=title_size,
                    fontweight="bold", color=INK, zorder=3)

    # anchor points
    def top(self, dx=0): return (self.x + dx, self.y + self.h / 2)
    def bot(self, dx=0): return (self.x + dx, self.y - self.h / 2)
    def left(self, dy=0): return (self.x - self.w / 2, self.y + dy)
    def right(self, dy=0): return (self.x + self.w / 2, self.y + dy)


def arrow(ax, p, q, label=None, color=MUTED, rad=0.0, lpos=None, lsize=10, style="-|>", ls="-",
          lha="center", lw=1.5):
    ax.add_patch(FancyArrowPatch(p, q, arrowstyle=style, mutation_scale=14, color=color, lw=lw,
                                 connectionstyle=f"arc3,rad={rad}", linestyle=ls, zorder=1,
                                 shrinkA=2, shrinkB=2))
    if label:
        if lpos is None:
            lpos = ((p[0] + q[0]) / 2, (p[1] + q[1]) / 2)
        ax.text(lpos[0], lpos[1], label, ha=lha, va="center", fontsize=lsize, color=color,
                zorder=4, linespacing=1.3,
                bbox=dict(fc="white", ec="none", pad=1.5, alpha=0.95))


def save(f, name):
    f.savefig(f"{OUT}/{name}.png", dpi=170, bbox_inches="tight", pad_inches=0.25, facecolor="white")
    plt.close(f)


# --------------------------------------------------------------------------------------
# 1. Page lifecycle (PFN state machine)
f, ax = fig(13, 6.2)
free = Box(ax, 1.9, 5.1, 3.0, 0.95, "Free lists", "5 shards (frame % 5)", GRAY)
cache = Box(ax, 6.5, 5.1, 3.0, 0.95, "Private cache", "32 frames per worker, no lock", GRAY)
act = Box(ax, 11.1, 5.1, 3.0, 0.95, "Active", "8 age buckets per PTE section", GREEN)
mod = Box(ax, 11.1, 1.9, 3.0, 0.95, "Modified", "unmapped, contents only in RAM", AMBER)
stb = Box(ax, 6.5, 1.9, 3.0, 0.95, "Standby", "contents in RAM and on disc", BLUE)
disc = Box(ax, 8.8, 0.35, 2.0, 0.6, "Disc (page file)", None, ("#F4F4F4", "#666666"), title_size=11)

arrow(ax, free.right(), cache.left(), "refill in\na batch", lpos=(4.2, 5.55))
arrow(ax, cache.right(), act.left(), "hard fault:\nmap frame", lpos=(8.8, 5.55))
arrow(ax, act.bot(0.55), mod.top(0.55), "trimmer:\nunmap cold pages", lpos=(12.35, 3.5), lha="left")
arrow(ax, mod.top(-0.55), act.bot(-0.55), "soft fault\n(rescue)", color=GREEN[1], lpos=(10.0, 3.5))
arrow(ax, mod.left(), stb.right(), "writer:\ncopy to disc", lpos=(8.8, 2.4))
arrow(ax, stb.top(0.8), act.bot(-1.2), "soft fault\n(rescue)", color=GREEN[1], lpos=(8.2, 3.55))
arrow(ax, stb.top(-0.8), free.bot(0.6), "repurpose a batch of 32:\n1 to the faulting thread,\n31 back to the free lists",
      color=BLUE[1], lpos=(3.55, 3.4))
arrow(ax, stb.bot(), disc.left(), None, color="#999999", ls="--", style="-", rad=0.25)
arrow(ax, mod.bot(), disc.right(), None, color="#999999", ls="--", style="-", rad=-0.25)
save(f, "page_flow")

# --------------------------------------------------------------------------------------
# 2. PTE state machine
f, ax = fig(12.5, 6.6)
zero = Box(ax, 1.8, 3.3, 2.6, 1.15, "Zero", "never touched\nV=0  T=0  D=0", GRAY)
val = Box(ax, 6.25, 5.5, 3.0, 1.15, "Valid", "V=1, frame number\n+ age, access bit", GREEN)
tra = Box(ax, 10.7, 3.3, 3.0, 1.15, "Transition", "V=0  T=1, frame number\n(page on modified/standby)", AMBER)
dsk = Box(ax, 6.25, 1.0, 3.0, 1.15, "Disc", "V=0  D=1\n40-bit disc slot index", BLUE)

arrow(ax, zero.top(), val.left(), "first touch:\nhard fault, zero the page", rad=-0.25, lpos=(2.5, 5.3))
arrow(ax, val.right(), tra.top(), "trimmer\nunmaps it", rad=-0.25, lpos=(10.0, 5.3))
arrow(ax, tra.left(0.25), val.bot(0.7), "soft fault:\nre-map same frame", color=GREEN[1], rad=0.05, lpos=(8.6, 4.35))
arrow(ax, tra.bot(), dsk.right(), "frame repurposed\nfrom standby", rad=-0.25, lpos=(10.1, 1.25))
arrow(ax, dsk.top(-0.5), val.bot(-0.5), "hard fault:\nread back from disc", rad=0.0, lpos=(4.6, 3.3))
ax.text(6.25, 6.45, "workers set the access bit;  the ager clears it", ha="center", fontsize=9.5,
        color=MUTED, style="italic")
save(f, "pte_states")

# --------------------------------------------------------------------------------------
# 3. Threads and events
f, ax = fig(13, 6.6)
wk = Box(ax, 2.2, 3.3, 3.2, 1.5, "Worker threads × 5", "generate accesses,\nresolve their own faults", GREEN)
tr = Box(ax, 7.2, 5.4, 3.0, 1.1, "Trimmer", "active → modified,\nsized by consumption rate", AMBER)
wr = Box(ax, 11.4, 3.3, 2.8, 1.0, "Writer", "modified → disc → standby", BLUE)
ag = Box(ax, 7.2, 1.0, 2.8, 1.0, "Ager", "sort pages into age buckets", PURPLE)
co = Box(ax, 11.4, 1.0, 2.8, 1.0, "Consumption", "every 3 ms: pages/tick", GRAY)

arrow(ax, wk.top(0.4), tr.left(), "trim_needed\n(supply low, or a worker ran dry)", rad=-0.2, lpos=(3.6, 5.6))
arrow(ax, tr.right(), wr.top(), "write_needed\n(≥ 500 modified, or emergency)", rad=-0.25, lpos=(11.3, 5.4))
arrow(ax, wk.right(0.25), wr.left(0.25), "write_needed", lpos=(6.6, 3.85))
arrow(ax, wr.left(-0.25), wk.right(-0.25), "pages_available", color=GREEN[1], lpos=(6.6, 2.85))
arrow(ax, co.left(), ag.right(), "age_needed\n+ how many sections", lpos=(9.3, 1.75), lsize=9.5)
save(f, "threads")

# --------------------------------------------------------------------------------------
# 4. get_free_page: where a hard fault gets its frame
f, ax = fig(13, 3.4)
steps = [
    ("1. Private cache", "32 frames, no lock", GREEN),
    ("2. Free lists", "refill cache from one\nshard (TryEnter)", GRAY),
    ("3. Standby", "repurpose a batch of 32", BLUE),
    ("4. Wait", "signal emergency trim,\nblock on pages_available", RED),
]
xs = [1.65, 4.95, 8.25, 11.55]
boxes = [Box(ax, x, 1.9, 2.55, 1.25, t, s, c) for x, (t, s, c) in zip(xs, steps)]
for a, b in zip(boxes, boxes[1:]):
    arrow(ax, a.right(), b.left(), "empty", lpos=((a.x + b.x) / 2, 2.3), lsize=9)
for b in boxes[:3]:
    arrow(ax, b.bot(), (b.x, 0.35), None, color=GREEN[1])
    ax.text(b.x + 0.12, 0.62, "got a frame", fontsize=9, color=GREEN[1], va="center")
arrow(ax, boxes[3].top(), (boxes[3].x, 3.25), None, color=RED[1], rad=0)
ax.text(boxes[3].x + 0.12, 3.05, "woken: retry from 1", fontsize=9, color=RED[1], va="center")
save(f, "get_free_page")

# --------------------------------------------------------------------------------------
# 5. Lock hierarchy
f, ax = fig(12.4, 6.2)
tiers = [
    ("PTE section lock", "64 sections, decides which list a page is on", GREEN),
    ("PFN page lock", "one CRITICAL_SECTION per physical frame", AMBER),
    ("List locks", "free shards, modified list, standby SRWLOCK + head lock", BLUE),
    ("Disc lock", "disc slot free list", GRAY),
]
ys = [5.3, 3.85, 2.4, 0.95]
tb = [Box(ax, 4.9, y, 5.6, 1.0, t, s, c) for y, (t, s, c) in zip(ys, tiers)]
for a, b in zip(tb, tb[1:]):
    arrow(ax, a.bot(), b.top(), None, color=INK)
arrow(ax, (1.85, 5.6), (1.85, 0.7), None, color=INK, lw=2.2)
ax.text(1.65, 3.15, "always\nacquire\ndownward", ha="right", va="center", fontsize=10.5, color=INK, fontweight="bold")
arrow(ax, tb[2].right(0.15), tb[1].right(-0.15), None, color=RED[1], ls="--", rad=0.6)
arrow(ax, tb[2].right(-0.15), tb[0].right(0.0), None, color=RED[1], ls="--", rad=0.45)
ax.text(9.0, 4.75, "going upward is TryEnter only:\nskip on failure, never block", fontsize=10.5,
        color=RED[1], fontweight="bold", va="center")
ax.text(9.0, 3.2, "• trimmer and writer: hold a list,\n   try each page lock\n"
                  "• standby batch: try every node\n"
                  "• repurpose: rewrites the old PTE\n   with a compare-exchange,\n   never takes its section lock",
        fontsize=9.8, color=MUTED, va="center", linespacing=1.4)
save(f, "lock_hierarchy")

# --------------------------------------------------------------------------------------
# 6. Standby RW list
f, ax = fig(13, 4.2)
ax.add_patch(FancyBboxPatch((0.3, 0.3), 12.4, 3.0, boxstyle="round,pad=0,rounding_size=0.15",
                            fc="#F7F9FC", ec=BLUE[1], lw=1.2, ls="--"))
ax.text(0.5, 3.0, "SRWLOCK held SHARED by every thread below", fontsize=10, color=BLUE[1],
        fontweight="bold", va="center")
names = ["head", "A", "B", "C", "D", "E", "F"]
nx = [1.3 + i * 1.75 for i in range(len(names))]
locked = {"head": RED, "F": RED, "B": AMBER, "C": AMBER, "D": AMBER}
nodes = []
for n, x in zip(names, nx):
    c = locked.get(n, BLUE)
    nodes.append(Box(ax, x, 1.8, 1.15, 0.75, n, None, c, title_size=12))
for a, b in zip(nodes, nodes[1:]):
    arrow(ax, a.right(0.12), b.left(0.12), None, color=MUTED, lw=1.1)
    arrow(ax, b.left(-0.12), a.right(-0.12), None, color=MUTED, lw=1.1)
ax.text(nx[0], 0.85, "Thread 1 inserts at the tail:\nlocks head + F", fontsize=9.5, color=RED[1], ha="left")
ax.text(nx[2] + 0.2, 0.85, "Thread 2 soft-faults C back:\nlocks C, B and D", fontsize=9.5, color=AMBER[1], ha="left")
ax.text(nx[5] - 0.5, 0.85, "after 30 failed tries:\nrelease, go EXCLUSIVE", fontsize=9.5, color=MUTED, ha="left")
ax.text(6.5, 3.75, "Both threads work on the standby list at the same time, because they lock different nodes",
        fontsize=11, ha="center", color=INK)
save(f, "standby_rw_list")
print("done")
