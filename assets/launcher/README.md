# The launcher's art (ours)

Made for the rebuild's launcher (docs/launcher.md) on 2026-10-30 with the
local Crucible image server: Qwen-Image 2.1 (`qwen-image-2.1`, mflux on
mlx-darwin), default steps (40) and guidance (1.0), the seeds below. Our own
art, no game files: every picture the launcher draws before an install is
found comes from here. Shown at 640x480 (the game's menu frame); text on
the plates is drawn by the launcher, not baked in.

| file | size | seed | prompt |
| --- | --- | --- | --- |
| bg_metal.png | 2048x512 | 11 | A seamless wide texture of dark gunmetal armour plating seen flat from the front: riveted steel panels with bevelled seams, worn edges, scratches, faint faded yellow and black hazard stripes on one panel, small vent grilles, dim cold blue-grey industrial light. Flat orthographic, evenly lit, no perspective, no text, no logos, no people. |
| bg_stars.png | 2048x512 | 12 | A wide deep-space starfield: many small sharp white and pale blue stars on near-black space, a faint rust-orange and dark red nebula drifting across the middle, a few brighter stars with soft glow. Dark, cinematic, evenly spread, no planets, no text, no spacecraft. |
| plate.png | 1024x256 | 13 | One wide rectangular menu button plate for a military science-fiction game, seen straight from the front: brushed dark steel with bevelled chamfered edges, a hex bolt in each of the four corners, a thin glowing amber line inset along the bottom edge, the face plain and empty in the middle. Centred on a pure black background, flat lighting, no text, no symbols. |
| emblem.png | 768x768 | 14 | A military unit insignia: an angular stylised visor of a giant armoured war robot inside a hexagonal shield, embossed brushed steel with amber enamel inlays and a thin red border, symmetrical, front view, centred on a pure black background, no text, no letters. |
| title.png | 1536x384 | 15 | The words "MW3 REBUILD" in large bold angular block capital letters made of embossed brushed steel with sharp bevels and a thin amber glow along the edges, centred on a pure black background, front view, nothing else in the picture. |
| badge_mw3.png | 768x768 | 21 | A round military medallion badge: the angular visor of a giant armoured war robot's head, front view, embossed brushed steel with amber enamel inlays, inside a thick circular steel rim with small rivets around it, symmetrical, centred on a pure black background, no text, no letters. |
| badge_pm.png | 768x768 | 22 | A round military medallion badge: a pale crescent moon above a jagged rocky horizon, embossed brushed steel with cold blue enamel inlays, inside a thick circular steel rim with small rivets around it, symmetrical, centred on a pure black background, no text, no letters. |
| badge_mw1.png | 768x768 | 23 | A round military medallion badge: the side view of a walking armoured war robot in simple retro pixel-art style with a few flat colours, inside a thick circular steel rim with small rivets around it, centred on a pure black background, no text, no letters. |

The three badges (2026-10-31, Owen: the ripple belongs to a circle) are
the games' entries: MechWarrior 3, Pirate's Moon (greyed until it is
supported) and MechWarrior (1989) (shown when its port is beside ours).

The starfield and the metal are not yet seamless at their ends: the
launcher cross-fades the last strip into the first when it scrolls (or they
are made again with an outpainted seam). "MW3 REBUILD" is a working title.
