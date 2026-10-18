"""Every enclosure dimension in one place. Units are mm and degrees.

Each value carries a status tag:
  [drawing]  from the LCDWiki/QDtech outline drawing V1.0 (docs/research/enclosure.md)
  [plan]     fixed by docs/PLAN.md or measured by the user
  [assumed]  a guess or a starting value: measure it (README calipers checklist) and update
  [design]   a design choice; change freely

Coordinates of the assembled device:
  Z = 0 is the front face (the glass), and the device extends toward -Z. "Depth" below
  means distance behind the front face, so a feature at depth d sits at Z = -d.
  X runs to the right and Y up as seen from the front; the origin is the board centre.
  The drawing's front-view coordinates (bx from the left PCB edge, by from the top PCB
  edge, USB-C at the bottom) convert with board_xy().
"""

# --- Board: Hosyond ES3C35P ------------------------------------------------------------
PCB_W = 54.5             # [drawing] PCB width
PCB_H = 101.5            # [drawing] PCB height
PCB_T = 1.6              # [drawing] PCB thickness
PCB_R = 3.5              # [drawing] PCB corner radius
REAR_PARTS_H = 4.7       # [drawing] tallest rear part above the PCB back (measure: 4.7 vs spec 10.0 total)

HOLE_D = 3.2             # [drawing] mounting hole diameter (M3)
PAD_D = 5.6              # [drawing] mounting pad / keep-out diameter
HOLE_DX = 47.9           # [drawing] hole pattern, X
HOLE_DY = 94.5           # [drawing] hole pattern, Y (pattern is centred on the PCB)

# --- Glass (cover glass + LCD + glue, all the same outline) ------------------------------
GLASS_W = 54.5           # [drawing] same width as the PCB
GLASS_H = 83.0           # [drawing]
GLASS_TOP_BY = 9.25      # [drawing] PCB top edge to glass top edge (9.25 strip at each end)
GLASS_CORNER_R = 0.3     # [assumed] glass corner radius (drawing shows square corners)
GLASS_TO_PCB = 4.2       # [drawing] CRITICAL: glass front face to PCB front face (1.0 + 2.2 + 1.0)
GLASS_FLUSH = 0.0        # [design] + makes the glass stand proud of the case face (aim 0.0..+0.1)

# --- Ports and board features (front-view board coordinates) ---------------------------
USB_BX = 27.25           # [drawing] USB-C centre from the left edge (bottom edge)
USB_Z_OFF = 1.65         # [assumed] USB-C centre behind the PCB back face
USB_OPEN_W = 12.5        # [assumed] opening sized for the cable overmold; measure your cable
USB_OPEN_H = 7.0         # [assumed]
USB_OPEN_R = 1.5         # [design]
USB_PLUG_W = 12.0        # [assumed] cable overmold, for the fit check only
USB_PLUG_H = 6.5         # [assumed]
USB_PLUG_L = 22.0        # [assumed] overmold + strain relief length

SD_BY = 31.22            # [drawing] microSD slot centreline from the top edge (right edge)
SD_Z_OFF = 1.0           # [assumed] card centre behind the PCB back face
SD_OPEN_L = 14.0         # [design] slot length along the edge
SD_OPEN_H = 3.0          # [design] slot height (depth direction)
SD_SCALLOP_DEPTH = 3.0   # [design] 45-degree finger/nail scallop cut into the outer wall
SD_SCALLOP_L = 18.0      # [design]

BAT_SOCK_BY = 59.89      # [drawing, unverified] battery socket, left edge
SPK_SOCK_BY = 74.39      # [drawing, unverified] speaker socket, right edge
LED_BX = 5.0             # [assumed] WS2812 (IO40) on the rear, pixel-measured
LED_BY = 47.4            # [assumed]
LED_H = 1.6              # [assumed] LED height above the PCB back
RESET_BX = 40.98         # [drawing] RESET button, rear side
BOOT_BX = 13.55          # [drawing] BOOT button, rear side
BTN_BY = 98.2            # [drawing]
MIC_BX = 44.68           # [drawing] bottom-port MEMS mic, hole through the PCB to the front
MIC_BY = 3.94            # [drawing]
MIC_HOLE = False         # [design] Ø1.2 hole in the front band; off (voice is not used in v1)
MIC_HOLE_D = 1.2         # [design]

# Front-side parts near the USB in the bottom strip: relieve the band there.
FRONT_RELIEF_HALF_W = 12.0   # [assumed] half-width of the relief, centred on the USB
FRONT_RELIEF_BY = 93.0       # [assumed] relief runs from here to the bottom edge
FRONT_RELIEF_DEPTH = 0.6     # [assumed]

# --- Battery + speaker pack (user's part, speaker included) -----------------------------
PACK_L = 66.0            # [plan] along the board's 101.5 length
PACK_W = 35.5            # [plan] across the board, centred side to side
PACK_T = 19.7            # [plan] including the speaker
PACK_CY = 0.0            # [design] pack centre in Y (0 = centred along the board)
PAD_T = 1.0              # [plan] insulating pad between rear parts and pack
PACK_REAR_GAP = 0.5      # [plan] pack back face to the rear-cover inner face
RIB_PRELOAD = 0.3        # [design] ribs reach this far past the 0.5 gap, squeezing the pad

# --- Speaker grille (rear cover) --------------------------------------------------------
GRILLE_CX = 0.0          # [assumed] speaker centre on the pack's back face, from the pack centre
GRILLE_CY = 0.0          # [assumed] (positive = toward the device top). MEASURE.
GRILLE_D = 22.0          # [assumed] grille diameter; set to the speaker cone/port diameter
GRILLE_HOLE_AF = 1.6     # [design] hex hole across flats
GRILLE_PITCH = 2.6       # [design] hole pitch (1.0 mm webs)

# --- Tolerances (tune with the fit coupon) ----------------------------------------------
GLASS_GAP = 0.2          # [design] per side, glass to window
BOARD_GAP = 0.2          # [design] per side, PCB to pocket
COVER_LIP_GAP = 0.2      # [design] cover locating lip to shell inner wall
SEAM_GAP = 0.2           # [design] cover to shell rim; the screws clamp via the standoffs
M3_CLEAR_D = 3.4         # [design] clearance hole for M3

# --- Walls and outer shape --------------------------------------------------------------
OUTER_W = 64.0           # [design]
OUTER_H = 112.0          # [design]
OUTER_R = 4.0            # [design] vertical corner radius
WALL = 2.0               # [design] side walls behind the board
COVER_T = 2.0            # [design] rear cover plate
FRONT_SKIN_MIN = 1.2     # [design] minimum plastic in front of an insert
EDGE_CHAMFER = 0.6       # [design] outer front and back edges
WINDOW_CHAMFER = 0.4     # [design] window edge on the front face
POCKET_LIP = 0.5         # [design] board pocket continues this far behind the PCB back
COVER_LIP_H = 2.0        # [design] locating lip height
COVER_LIP_T = 1.2        # [design] locating lip thickness

# --- Fasteners: 4x M3 through the cover, standoffs, PCB, into inserts in the front bands --
INSERT_HOLE_D = 4.0      # [assumed] follow your insert maker's hole spec
INSERT_L = 3.0           # [assumed] short M3 insert (M3 x 3); 4.2 band - 1.2 skin leaves 3.0 max
SCREW_L = 30.0           # [design] M3 x 30 button head (ISO 7380)
SCREW_HEAD_D = 5.7       # [design] ISO 7380 M3
SCREW_HEAD_H = 1.65      # [design]
CBORE_D = 6.2            # [design] counterbore for the head
CBORE_DEPTH = 2.0        # [design]
STANDOFF_OD = 5.4        # [design] bears on the Ø5.6 pad ring only
STANDOFF_BASE_OD = 8.0   # [design] thicker root at the cover, holds the counterbore
STANDOFF_BASE_H = 3.0    # [design]

# --- Power switch (SS12D00-style slide switch, on the rear cover) ------------------------
SW_BODY_L = 8.6          # [assumed] measure your switch
SW_BODY_W = 3.6          # [assumed]
SW_SLOT_L = 4.5          # [assumed] actuator + travel
SW_SLOT_W = 2.2          # [assumed]
SW_X = -10.0             # [design] switch centre (top area, clear of the stand)
SW_Y = 44.0              # [design]
SW_CRADLE_WALL = 1.2     # [design]
SW_CRADLE_H = 3.0        # [design]
SW_CRADLE_GAP = 0.15     # [design] body to cradle, per side (glue it in)

# --- WS2812 light pipe and button poke holes (rear cover) --------------------------------
LED_PIPE_HOLE_D = 3.2    # [design] for a Ø3 acrylic rod
LED_TUBE_OD = 5.2        # [design]
LED_TUBE_L = 8.0         # [design] guide tube length from the cover inner face
BTN_POKE = True          # [design] paperclip holes for RESET and BOOT
BTN_HOLE_D = 2.0         # [design]
BTN_TUBE_OD = 4.0        # [design]
BTN_TUBE_CLEAR = 1.5     # [design] tube end stops this far behind the rear-parts envelope

# --- Desk stand -------------------------------------------------------------------------
STAND_ANGLE = 60.0       # [design] screen angle from horizontal (55-70 sensible)
STAND_LIFT = 10.0        # [design] lowest point of the device above the counter (8-12)
STAND_LEDGE_L = 14.0     # [design] ledge under the device's bottom face
STAND_BACK_L = 58.0      # [design] backrest length up the device's back
STAND_BACK_T = 5.0       # [design] backrest thickness
STAND_BASE_BACK = 48.0   # [design] base extends this far behind the device's bottom edge
STAND_CHEEK_W = 14.0     # [design] each side cheek (centre stays open for the cable)
STAND_FENCE_H = 3.0      # [design] side fences that stop the device sliding sideways
STAND_FENCE_T = 2.0      # [design]
STAND_FIT_GAP = 0.3      # [design] device to fence
STAND_BAR_S = 24.0       # [design] cross bar reaches this far up the backrest
STAND_NOTCH_W = 14.0     # [design] cable notch through the cross bar
STAND_NOTCH_H = 12.0     # [design] notch height (45-degree pointed top, no supports)

# --- Fit coupon -------------------------------------------------------------------------
COUPON_EXTRA = 1.0       # [design] coupon depth = glass + PCB + this

# --- Printer ----------------------------------------------------------------------------
BUILD_VOLUME = (256.0, 256.0, 256.0)   # [plan] Bambu Lab P2S

# --- Derived (don't edit) ----------------------------------------------------------------
PCB_FRONT = GLASS_TO_PCB - GLASS_FLUSH           # depth of the PCB front face (= band face)
PCB_BACK = PCB_FRONT + PCB_T
REAR_ENVELOPE = PCB_BACK + REAR_PARTS_H          # back of the tallest rear part
PAD_FRONT = REAR_ENVELOPE
PACK_FRONT = PAD_FRONT + PAD_T
PACK_BACK = PACK_FRONT + PACK_T
COVER_INNER = PACK_BACK + PACK_REAR_GAP
DEPTH = COVER_INNER + COVER_T                    # overall depth, front face to back face
SHELL_DEPTH = COVER_INNER - SEAM_GAP             # front shell rim
POCKET_BACK = PCB_BACK + POCKET_LIP              # interior widens to the 2.0 walls here
RIB_H = PACK_REAR_GAP + RIB_PRELOAD
GLASS_Y = 0.0                                    # glass is centred: 9.25 strips at both ends
assert abs((PCB_H / 2 - GLASS_TOP_BY) - GLASS_H / 2) < 1e-6, "glass is not centred on the PCB"


def board_xy(bx, by):
    """Drawing front-view coordinates (from the left/top PCB edges) to case X/Y."""
    return bx - PCB_W / 2, PCB_H / 2 - by


HOLES = [(sx * HOLE_DX / 2, sy * HOLE_DY / 2) for sx in (-1, 1) for sy in (-1, 1)]
USB_X, _ = board_xy(USB_BX, PCB_H)
USB_DEPTH = PCB_BACK + USB_Z_OFF
_, SD_Y = board_xy(PCB_W, SD_BY)
SD_DEPTH = PCB_BACK + SD_Z_OFF
LED_X, LED_Y = board_xy(LED_BX, LED_BY)
RESET_X, BTN_Y = board_xy(RESET_BX, BTN_BY)
BOOT_X, _ = board_xy(BOOT_BX, BTN_BY)
MIC_X, MIC_Y = board_xy(MIC_BX, MIC_BY)
_, BAT_SOCK_Y = board_xy(0, BAT_SOCK_BY)
_, SPK_SOCK_Y = board_xy(PCB_W, SPK_SOCK_BY)
GRILLE_X = GRILLE_CX
GRILLE_Y = PACK_CY + GRILLE_CY
INNER_W = OUTER_W - 2 * WALL
INNER_H = OUTER_H - 2 * WALL
INNER_R = OUTER_R - WALL
