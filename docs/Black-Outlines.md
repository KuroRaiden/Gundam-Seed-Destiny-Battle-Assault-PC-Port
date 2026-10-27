# Black fighter outlines

JP Local VS, Online VS and Training automatically draw a simple black outline
one native pixel thick around the mobile suits. On the launcher's **Play** page,
use **Black sprite outlines** to enable the border and **Outline opacity** to
set its strength: 0% is invisible, 100% is solid black. Changes save automatically
and apply when launching. Existing settings default to 100%. The original
sprites and palettes are kept.

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

Opacity blends black over only the contour pixels in the host display buffer.
Changing it keeps the completed raster mask, so the outline remains stable
while the next frame is being drawn. Validation checks every color channel at
0%, 25%, 50%, 75% and 100%, all 27 fighters, Training and unchanged snapshots.
A 240-frame UDP match with 25% on one peer and 75% on the other exercised
rollback and F4 remapping, with identical final checksums and zero desyncs.
