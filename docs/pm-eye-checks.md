# Pirate's Moon: eye checks for Owen's PC

Each one: play the original at the place named, look at the port's picture beside it, answer the
question. A few minutes in all. The missions are by the numbers in their files (`readerm1`..`m10`:
world c1 has 1, 2, 5; c2 has 3, 4, 8; c3 has 6, 7; c4 has 9, 10). The places are names from each world's
`location.zrd`, so they should appear on the nav list; if one does not, near the mission's start will do.

Pictures: `claude-files/outbox/2026-11-04/pm-eye-checks/`. They come from `mw3pm --fly`, a free camera with
no cockpit, HUD, mechs or vehicles, so compare the land and sky only. The line under each makes the same
picture. Camera heights are chosen to be about a mech's eye, and the direction faced will differ.

1. **Is the first mission really this dark?** Mission 1, at the start, look at the sky and the ground.
   The port shows a nearly black sky with grey clouds, though the data calls it `day`.
   `1-c1-day-start.png`: `mw3pm --fly c1 --pos 4468 56 -4186 --look 0 5`
2. **How dark is a night mission?** Mission 2, near Allied Camp: can you see the ground at all, and are there
   large stars? The port's ground is almost black.
   `2-c1-night-camp.png`: `mw3pm --fly c1 --variant m2 --pos 3840 17 -3200 --look 0 5`
3. **Is the third world's light right?** Mission 6 or 7, near Ruins: red ground under a black sky with red clouds, a ringed planet and a large one low down?
   `3-c3-ruins.png`: `mw3pm --fly c3 --pos 1792 25 -896 --look 180 5`
4. **Is that green right?** Mission 9 or 10, walk to Drill Rig 2: are the grids and the plain beside them this
   bright a green, and is the plain flat? The sky is dark brown here too.
   `4-c4-rig.png`: `mw3pm --fly c4 --pos 2048 30 -2048 --look 0 15`
5. **Are there hard lines in the ground?** Mission 1, on open ground between the start and Pirate Base: where
   sand meets red ground, does the edge go in stair-steps? The port also shows a straight line in the ground
   texture in this picture (down the middle, a cell's edge). Does the original?
   `5-c1-tiles.png`: `mw3pm --fly c1 --pos 2432 30 -2688 --look 0 12`
6. **Is the orange ground's edge jagged?** Mission 3 or 4, between Neutral Camp and Germanium Camp, where the
   orange ground meets the dark ground, near the bridge. The picture also shows c2's fog and sky.
   `6-c2-bridge.png`: `mw3pm --fly c2 --pos 1536 40 -1792 --look 0 15`
7. **Are the textures this sharp?** Any of the above, up close. The game asks for sharper distant textures
   (`MipBias -1`), and the port does not do that yet. Is the original's far ground crisper than these pictures?
8. **A gap under the horizon? It may not be reachable.** Mission 3 or 4 (or 9 or 10), a mech with jump jets, on
   the highest ground you can find, at the top of a jump, looking level at the horizon: is there a band between
   the far ground and the sky, and what is in it (black, old frames smearing, nothing)? In the port it is black, with a ragged lower edge where whole cells of far ground stop. The port's pictures are
   from far higher than a jump may get, where it opens at about 96 units over the ground 600 units away;
   jets stop at 130 and 145. If a couple of jumps show nothing, the answer is "not seen", and that settles it.
   `7-c2-gap.png`: `mw3pm --fly c2 --pos 1536 150 -1792 --look 0 8`;
   `8-c4-gap.png`: `mw3pm --fly c4 --pos 1536 150 -1792 --look 0 8`
9. **Are the small props the same?** Mission 3 or 4 near the bridge, and mission 6 or 7 near Ruins: do the same
   small rocks and props lie on the ground in the original, about this many? They are laid at random, so their
   exact places will differ.
   `clutter-cl2.png`: `mw3pm --fly c2 --pos 1536 40 -1792 --look 0 15`;
   `clutter-cl3.png`: `mw3pm --fly c3 --pos 1792 25 -896 --look 180 5`
