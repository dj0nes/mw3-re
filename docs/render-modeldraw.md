# The Direct3D model draw (0x53f2e0)

Mech3.exe 1.2, 2026-10-19. Draft for `docs/render.md` section 3. Port:
`src/gamez/zmodel/gmod_draw.{h,cpp}` (original file unknown: between
`gmod_const.c` and `gmod_cookie.cpp`). Check:
`tests/differential/test_modeldraw.py` (probe `tests/probe_modeldraw.cpp`).

## What is ported

| Exe | Port | |
|---|---|---|
| 0x53f2e0 | `draw_model(node, mask)` | types 0 (default), 1 (facades, modes 0 and 1), 2 (points); any other type as a default model |
| 0x53dcf0 | `facade_centre` | model flag 0x40: mean of every polygon vertex |
| 0x51ea90 | `billboard_y` | facade mode 0, yaw replaced by the camera's (camera data +0x3c) |
| 0x51efb0 | `billboard_sphere` | facade mode 1, via 0x525a50 / 0x525740 / 0x51e740 (quaternion between two vectors, to a 3x3, perpendicular fallback) |
| 0x5376c0 | `material_cycle` | (`gmod_matl.c`) |
| 0x535d20 | `screen_reject` | second-pass trivial reject |
| 0x4020d0 | `normalize` | |
| 0x53daa0, 0x545720 | `gmod_light` (`model_needs`, `shade_polygon`) | called, not duplicated |

Not ported, behind `draw_hooks()`: 0x544ed0 + 0x4ed980 (static light bake,
`refresh_static`), 0x541df0 (model lights / lens flares, `model_light`).
Ported since and the hooks' defaults: 0x547540 / 0x547740 (projected
lights, `projected_strip` / `projected_draw`) and the projected-shadow
receiver block (0x540552.., 0x540ef6..: `collect_shadows`),
render-shadows.md. Facade modes 2 and 3
(0x51f390, 0x51f750) are used by no shipped world and draw as default models.
The draw table is `draw_table()` (function pointers with the original's
arguments, FIX (A)); wiring it to `zvid::draw_flat` etc. is left to the
integration (signatures match `zvid_ddd3d.h`).

## Structure (asm, Ghidra drops ECX/EDX)

ECX node, EDX plane mask. Pushes a local slot ([ebp-0xd0]) and builds the top:
type 0 and >2: 0x51fb30 (`[0x74a3cc]` = 1 for type 0 only); type 1: the facade
top; type 2: 0x51fb30 then points, return. Then, only with polygons (+0x10 >
0): 0x53daa0 (needs), vertices (morph 0x5220e0 + 0x5227a0 when flag 8, factor
not 0/NaN and morph count; else the inline transform), 0x555a70 when needs &
1 (none above 1/255 clears the bit), normals rotated and normalised with
SetVertexShading. Model lights are animated for every model. Then
`[0x7402f8] = EDX`, the receiver / projected-light / bake decisions, and per
polygon:

1. reversed copy into 0x7476a4; strips (0x400) skip the backface test.
2. backface: `n = (b0-b1) x (b2-b1)`, `d = n.b0`; draw if `-thr > d`;
   two-sided (0x100) also if `d > thr`, n negated; otherwise skip.
3. primitive `6 - strip`; vertex normals reversed into 0x747ca8 when
   SetVertexShading, model normals and polygon flag 0x200.
4. fog factors reversed into record +8; `fogged` ([ebp-8]) when one is above
   1/255, `per_vertex` ([ebp-0xc]) = 2 when one differs from vertex 0's by
   more than 1/255.
5. Material +1 bit 0 decides:
   - **untextured**: lighting (needs & 6) or, fogged, darkness zeroed for
     vertices 0..count-1; fogged -> 0x527a30, project, 0x52aaf0, priority,
     `[0x72f3b8](colour u16, &material.r; fogged, alpha, count, prio, overlay)`;
     else 0x5275e0, project, 0x529ce0, priority,
     `[0x72f3b0](colour, alpha; count, prio, overlay)`. Both then the second
     pass. Strips are drawn whole, as strips, unculled.
   - **textured**: cycle (material flag 4), projected-light flag, lighting
     (shadowed = that flag), UVs and colours reversed into 0x72f434 /
     0x7481b4, then the fogged/lit path (0x53fffd) or the plain one (0x540b72).
6. alpha = `ftol(float(material alpha) * opacity)`; texture record = texture
   entry +4 (null without texture).

Argument order (stack after ECX, EDX):

| Table | ECX | EDX | stack |
|---|---|---|---|
| [0x72f3b0] | colour | alpha | count, priority, overlay |
| [0x72f3b8] | colour | &material.r | fogged, alpha, count, priority, overlay |
| [0x72f3c0] | count | texture | opacity, overlay |
| [0x72f3c4] | fogged | count | texture, priority, opacity, overlay |
| [0x72f3c8] | fogged | count | texture, opacity, overlay |
| [0x72f3d0] | count | texture | |
| [0x72f3d4] / [0x72f3d8] | fogged | count | texture |
| [0x72f3e0] | &screen (x, y, rhw) | colour | 0xff |

Corrections to render.md section 3: the per-vertex choice in the textured
path is the lighting's vary flags, not fog alone; [0x72f3c4] gets the priority,
[0x72f3c0] does not; the table's ECX/EDX are as above (count is ECX only for
[0x72f3c0] / [0x72f3d0]).

## Strips (0x400)

Fogged/lit path: split unless `mask == 0`, opacity >= 1, the image has no
alpha (zimg::Texture flags & 2) **and** no projected light; plain path: the
same without the projected-light term. A split saves the strip (positions,
UVs, records +0..+0x14 or only the colours, vertex normals) and draws
triangles `(k, k-1-p, k-2+p)` for k = n-1 down to 2, p the parity of
`n - k`, as 3-vertex fans; each faces the backface test (no two-sided case).
The saved vertex normals are copied back at the end (positions are not).

## Quirks

- **The polygon normal slot is shared.** `n` ([ebp-0x24]) is a local of the
  whole draw: a strip passes the previous polygon's normal (or, first in the
  model, whatever the slot held) to the lighting, and for a facade with flag
  0x40 that is the **facade's centre** (0x53dcf0 writes it there). Found by
  the test; the port keeps one `n` per draw.
- **Uninitialised needs**: with no polygons, `[ebp-0x68]` is never set but
  the bake test reads it (bit 2); the port uses 0.
- **Darkness zeroing off by one** (textured, fogged, unlit, per-vertex fog):
  slots count..1 are zeroed, slot 0 separately: slot 64 is the shadow count at
  0x7489a8 (harmless, reset before every draw). Untextured zeroes 1..count-1.
- **Uniform fill**: after a uniform-light clip, new vertices copy vertex 0's
  point light and fog (0x540946, 0x540a29).
- **Point models** take the colour of polygon 0's material even with no
  polygons (null read in the original; the port draws nothing). The inclusive
  point rect (0x74031c..) is `x .. x+w-0.001`.
- **Model lights**: type 1 increments the whole dword +0x14, so index 0xffff
  carries into the second colour; a non-zero index also draws the previous
  vertex in that colour; type 0 mode 1 swaps its two colours every period.
- **Material cycles**: a negative index is lifted by `trunc(|speed|) * count`,
  nothing for |speed| < 1; the texture shown is the one before the step.
- **No bound checks**: the copies are do-whiles (a 0-vertex polygon would run
  2^32 times) into 64-vertex buffers. The only polygons over 64 vertices are
  in `inout` (up to 242, c4), deleted at mission load; the port skips them
  (FIX (bug)). Counted 2026-11-04 on every world and mechlib, 1.0 and 1.2
  data: outside `inout` the largest is c4's concave 32-gon
  (`world1/clan/g21/tires01/healthy`), which reached 35 vertices in 300,000
  random 5-plane clips, then 20; mechlib's is 12. Static data only: polygons
  added at run time (craters) and Pirate's Moon's worlds are not in this count.
- **Facade mode >3** leaves the pushed slot uninitialised (draws as default
  in the port).
- **Second pass** ([0x74a32c], 0x740338): flat and lit untextured polygons are
  reject-tested (0x535d20: +4/+8/+0x10/+0x14), clipped again (0x529ce0:
  +4/+8/+0x1c/+0x20), scaled/offset by 0x74038c..0x740398, `sz += 1/near`,
  and drawn flat again. Not set by anything known.
- 0x53f2e0 is installed only with a device (0x52680d), so its software
  points branch (0x5234f0, 0x567420) is dead.

## Check

`test_modeldraw.py` (default 2000 draws on c1, c4b, t1, ~25 s; all six worlds
pass at 3000 each, 78,615 calls). Random world matrices, cameras inside, near,
far and edge-on, near 0.1..r, far 800 or short, four viewports, random plane
masks, opacity 1/0.5/0, overlay, SetVertexShading, fog with bands around the
model, the sun on or off, and the per-draw edits listed in the test's header.
Model-view, view and projection are inputs: the original's own view and
projection are copied into the port, the port's model-view into the original
(0x51fb30 stubbed; `--real-mv` shows the difference). Coverage (`--coverage`)
reaches all of 0x53dcf0, 0x525740, 0x4020d0 and all of 0x53f2e0 except the
dead identity copies, the software points, modes 2/3, projected lights and
shadow casters, and a null texture on a textured material.

Tolerances: 1e-4 relative; fog factors 1e-3 (gmod_fogcalc sums in double);
facades 1e-3 plus 0.01 px (zmth's sinf/cosf/atan2 against fsin/fcos/fpatan),
and their masks always carry the near bit. With an `-ffp-contract=off` build
of the probe everything but fog, lighting darkness, points and facades is bit
for bit.

Breaks (each a one-line change, run on c1 + t1, 4000 draws): caught —
backface sign, no vertex reversal, two-sided normal not negated, two-sided
threshold, priority without the 1, strip parity, uniform fill dropped,
darkness zeroed 0..count-1, fog spread `>=`, second-pass offset sign, blink
colours not swapped, cycle showing the stepped texture, billboard yaw sign.
Not caught: the projected-light split term (no projected lights in the test),
the lit untextured projection order `(fx'*w)*x` (last bit, inside the
tolerance), the point rect's inclusive edge (never hit exactly), the facade
centre as stale normal (rare; it was found by an earlier run on c3).

## Integration notes (for the owners)

- `zmth_matrix.cpp` has no `#pragma STDC FP_CONTRACT OFF`: on arm64 its sums
  fuse, so `transform_points_to`, `morph`, `transform_points`, `set_view`,
  `project_d3d` round differently from the x87 at 24 bits. gmod_draw writes
  the inline transform and the morph out itself for that reason.
- `zmth::load_model_view` (0x51fb30) sums translation z in order; the exe
  sums `(l11*v8 + l10*v5) + (l9*v2 + v11)` (0x51fcd8..0x51fd03). The
  billboards inline the same product; gmod_draw's `times_view` has the exe's
  order.
- `gmod_fogcalc` uses double "as the x87 keeps it"; at the 24-bit precision
  the game runs, float steps would be exact.
- The clippers keep separate copies of the shared buffers
  (`clip3d_buffers()` vs `clip2d()`); gmod_draw copies records, UVs and
  shadow sets around each camera-space clip. One set would remove that.
- `gmod_bake` (0x544ed0) has landed meanwhile; the draw calls it when
  `refresh_static` is unset (render-bake-textures.md).

## For docs/discoveries.md

- Tree billboards turned about their centre hand that centre to the lighting
  as if it were a surface normal when their first polygon is a strip (no
  shipped facade has the flag, so it never shows).
- Strips are never culled as a whole and never two-sided: their triangles
  that face away vanish.
