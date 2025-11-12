#show link: set text(fill: blue)


#align(center, [
  = CSE 240A Project 1
  == Piyush Kumbhare
])\


== Custom Predictors

For this project, I ran through a variety of predictors in order to get a gauge
of how complicated each one would be to implement. I only submitted the one
with the best performance to Gradescope, but the others can be found within the
various branches in
#link("https://github.com/zSuperx/UCSD_CSE240A_F25_BP")[this github repo].

== Skew

As described in the YAGS paper, the Skew predictor performs the best with long
running programs, but takes a very long time to learn new patterns (i.e. after
a context switch). While implementing Skew, I referenced
#link("https://inria.hal.science/inria-00073720v1/document")[this paper].
Implementing the hashing functions and their associated inverses turned out to
be very complex, so my program likely has bugs that impact performance.

== YAGS

I also attempted to mimic the YAGS predictor based on my understanding of it. Unfortunately, I ran out of time near the end and could not achieve good performance. However, I did brainstorm a few different methods of serializing the direction tables. 

=== Fully Associative Lookup

With a fully associative lookup table, each invocation of the predict/train
function would involve searching through the entire table. While this may be
suboptimal in our C simulation, the hardware would most likely have a way to
perform all comparisons at the same time. This still left the complication of
LRU bits, since addresses need to be evicted for new ones to take their spot. 

Unfortunately, with a fully associative table, I could not think of a shortcut
to save bits when tracking a full LRU ordering. Thus, a direction table of size
$2^N$ would require $N$ bits per entry, which is terribly inefficient. 

=== Set Associative Lookup

The official YAGS implementation uses a $N$-way set-associative lookup. With
this approach, each entry in the direction table contains $N$ addresses. In
such a scheme, the LRU is tracked per-set, rather than throughout all
addresses. Hence, $log_2(N)$ bits are still needed per entry, but this is
dramatically smaller than the fully associative version. For example, in a
2-way set associative table, only 1 bit is required per entry, while a 4-way
set associative table requires 2 bits per entry. 

=== "Accurate" LRU

The previous methods are used to get an accurate LRU ordering. However, if we
relax our requirements for the true least recently used address to be evicted
each time, we can instead employ the use of
#link("https://en.wikipedia.org/wiki/Pseudo-LRU")[Pseudo LRU binary trees],
where...

- Index `i`'s left child exists at index `2i + 1`
- Index `i`'s right child exists at index `2i + 2`

And instead of tracking a full ordering of LRU nodes, we can instead have each
node track which of its children was the least recently used. As shown before,
since each node has only 2 children, just 1 bit is required for this
information.

The actual addresses will be stored in the leaf nodes of the tree.

We can then, for example, say that `0` indicates it is the least recently used
of the 2 children. Thus, to find "a" LRU address, we can simply start at the
root of the tree and follow the `0`s until we reach the leaf address node. 

Likewise, when a particular address gets used, we know its index (since it'll
be the lower bits of `pc`), meaning we know the path from the root to get
there. We can follow this path and flip each of the recency bits at each level
to indicate the new LRU branches of the tree. 

In theory, this scheme would require only $N - 1$ bits (total) to track $N$
addresses.

== Modifying Alpha 21264's Tournament

Due to the simplicity and effectiveness of this approach, I used this method. 

Since one of the biggest heuristics of branch predictors is to reduce
destructive aliasing, improving the original tournament predictor would likely
involve different addressing schemes into each of the tables. 

Here, I used a simple observation: a 1-level BHT indexed with the raw GHR
performs horribly, while G-Share (which simply uses $"pc" xor "GHR"$ to index)
performs really well. 

Combining this with the fact that the Alpha 21264 tournament predictor uses a
raw GHR to index into several tables, the obvious optimization is to just
change this to be $"pc" xor "GHR"$. This turned out to work really well,
improving the performance of the tournament predictor by a decent amount for
the given traces.

Another approach I explored was replacing the GHR with the PHR, which we
briefly learned about in class. Due to the limited information directly
available, I referenced
#link("https://cseweb.ucsd.edu/~dstefan/pubs/yavarzadeh:2024:pathfinder.pdf")[the
Pathfinder paper] to learn how the PHR is calculated.

After doing this and replacing all GHR indexing schemes with the PHR, I got
really good results on the given traces. However, the leaderboard results
showed that I was getting sub-par performance on the hidden traces.

#table(
  columns: (auto, auto, auto, auto, auto),
  inset: 10pt,
  align: horizon,
  table.header(
    [], [*Static*], [*Gshare*], [*Tournament*], [*Custom*],
  ),

  [U1_Blender.bz2], [], [33.654 ], [29.264], [],
  [U2_Leela.bz2  ], [], [101.729], [97.020], [],
  [U3_GCC.bz2    ], [], [19.608 ], [15.172], [],
  [U4_Cam4.bz2   ], [], [10.030 ], [6.537 ], [],
)

