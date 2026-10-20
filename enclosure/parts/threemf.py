"""Minimal 3MF writer and reader for Bambu Studio.

build123d's Mesher writes every solid as its own object, so each letter of the white text
became a separate tiny object. Bambu Studio then asked to rescale ("too small, meters or
inches?"), offered to merge objects "positioned at multiple heights", and a "Yes" blew a
letter up 25x. This writer uses the 3MF core components layout instead: one object per
print, with the case body and the white labels as two parts of it. Bambu Studio imports
that as one object with two parts (checked with its own loader), at millimetre scale.

No printer or filament settings go in the file, so your P2S presets stay as they are.
The part names say which filament each part wants; set the labels part to the white one.
"""

import re
import zipfile

CONTENT_TYPES = (
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
    '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
    '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>'
    '</Types>'
)
RELS = (
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
    '<Relationship Target="/3D/3dmodel.model" Id="rel0" '
    'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>'
)


def _mesh(shape, tol=0.01, ang=0.1):
    verts, tris = shape.tessellate(tol, ang)
    return [(v.X, v.Y, v.Z) for v in verts], tris


def _esc(s):
    return s.replace("&", "&amp;").replace('"', "&quot;").replace("<", "&lt;")


def write(path, objects):
    """objects: [(object name, [(part name, shape), ...]), ...] in plate coordinates (mm)."""
    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           '<model unit="millimeter" xml:lang="en-US" '
           'xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">',
           ' <resources>']
    items, nid = [], 1
    for name, parts in objects:
        ids = []
        for pname, shape in parts:
            verts, tris = _mesh(shape)
            out.append(f'  <object id="{nid}" name="{_esc(pname)}" type="model"><mesh><vertices>')
            out += [f'<vertex x="{x:.5f}" y="{y:.5f}" z="{z:.5f}"/>' for x, y, z in verts]
            out.append('</vertices><triangles>')
            out += [f'<triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in tris]
            out.append('</triangles></mesh></object>')
            ids.append(nid)
            nid += 1
        comps = "".join(f'<component objectid="{i}"/>' for i in ids)
        out.append(f'  <object id="{nid}" name="{_esc(name)}" type="model"><components>{comps}</components></object>')
        items.append(f'  <item objectid="{nid}"/>')
        nid += 1
    out += [' </resources>', ' <build>', *items, ' </build>', '</model>']
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", CONTENT_TYPES)
        z.writestr("_rels/.rels", RELS)
        z.writestr("3D/3dmodel.model", "\n".join(out))


def summary(path):
    """Read back: [(object name, [part names], (minx, miny, minz, maxx, maxy, maxz))]."""
    text = zipfile.ZipFile(path).read("3D/3dmodel.model").decode()
    meshes = {}
    for oid, pname, body in re.findall(r'<object id="(\d+)" name="([^"]*)" type="model"><mesh>(.*?)</mesh>', text, re.S):
        xyz = [tuple(map(float, v)) for v in re.findall(r'x="([^"]+)" y="([^"]+)" z="([^"]+)"', body)]
        meshes[oid] = (pname, xyz)
    result = []
    for oid, name, comps in re.findall(r'<object id="(\d+)" name="([^"]*)" type="model"><components>(.*?)</components>', text):
        cids = re.findall(r'objectid="(\d+)"', comps)
        pts = [p for c in cids for p in meshes[c][1]]
        bb = tuple(min(p[i] for p in pts) for i in range(3)) + tuple(max(p[i] for p in pts) for i in range(3))
        result.append((name, [meshes[c][0] for c in cids], bb))
    return result
