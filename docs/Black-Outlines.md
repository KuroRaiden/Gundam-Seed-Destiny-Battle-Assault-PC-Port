# Black fighter outlines

JP Local VS, Online VS and Training automatically draw a simple black outline
one native pixel thick around the mobile suits. There are no extra controls,
color choices or network settings. The original sprites and palettes are kept.

The PPU exposes a read-only tap of the frontmost object index and layer priority
at each raster row. The port identifies the two fighter tile/palette allocations
and draws the contour into a separate host display buffer. HUD planes, other
objects and foreground layers that cover the fighter remain protected.

The original framebuffer, guest RAM, CPU state and snapshots remain unchanged.
The effect sends no packets and changes no combat API, snapshot schema or room
protocol. An outlined copy can play an unoutlined copy with the same underlying
netplay protocol; the earlier stage-select protocol change is independent.

The presentation predicate reads cached match lifecycle flags, never guest-bus
accessors. Guest bus reads can update open-bus bookkeeping even when reading
RAM. Restoring a snapshot invalidates the local mask; a complete native raster
rebuilds it before another outline can be displayed.

Validation covers all 27 native fighters in both seats, movement and A/B/L/R,
Training, immutable raw pixels and exact before/after simulation snapshots.
Two mixed-DLL matches used the earlier unoutlined stage-select module and the
new outlined module, swapping host/guest roles and enabling custom palettes in
the second match. Both reached 360 frames with identical initial/final checksums,
matching health, exercised rollback and zero desyncs. These were loopback UDP
checks with live hidden SDL surfaces, including F4 remapping during play.
