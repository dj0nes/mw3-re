#pragma once
#include <cstdint>

#include "pm/gamez/zmodel/gmod_draw.h"

// C:\zipper\branchep1\gamez\zModel\gmod_matl.c's neighbours: Pirate's Moon's projected
// lights on a polygon (docs/pm-map.md, gmod_matl.c): a fan around its centre, split where
// a light's falloff crosses a triangle, each triangle drawn with light per vertex. MW3's
// 0x00547740, rewritten: the threshold follows the nearest light's far, the sets' colours
// are carried too, the light squares its falloff as MW3's does.
namespace pm::zmod {

// One triangle as 0x00562840 hands it to the device (0x008c4bf8, 0x008c4bfc).
struct ProjectedTriangle {
  struct Vertex {
    float sx, sy, sz, rhw;
    uint32_t diffuse, specular;
    float reserved;
    float tu, tv;
    float fog;
  } v[3];
  const ClipShadows* sets = nullptr;  // the three vertices' UVs and colours, with sets
  uint32_t flags = 0;
};

// Pirate's Moon 1.0 @ 0x00562620: 1 when a light with flags 0x10 and 0x40 (not 0x80 on a
// light-mapped polygon) reaches the polygon's sphere.
int projected_reach(const Record& r);
// Pirate's Moon 1.0 @ 0x00562840: 1 when it drew the polygon; `draw` takes each triangle
// (null to work it out only).
int draw_projected(const Record& r, void (*draw)(const ProjectedTriangle&, const ::zmod::Material&));

}  // namespace pm::zmod
