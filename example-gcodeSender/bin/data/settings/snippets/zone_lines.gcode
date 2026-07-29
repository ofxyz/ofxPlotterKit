; Horizontal lines across zone (50 x 80 mm), G91 from zone min corner (machine bed origin)
; Detour lands at zone centre; relocate once to min corner.
; {penUpZ}/{penDownZ} expand from pen settings at inject time.
; Optional: G0 X{random(-2,2)} Y{random(-2,2)} before the hatch to jitter.
G91 ; relative XY from detour position (centre)
G0 X-25 Y-40 ; centre -> min corner
G90
G1 Z{penDownZ} ; pen down
G91
G1 X50 Y0 ; draw across
G90
G0 Z{penUpZ} ; pen up
G91
G0 X-50 Y5 ; left edge, next line
G90
G1 Z{penDownZ} ; pen down
G91
G0 X0 Y0 ; draw from left edge
G1 X50 Y0 ; draw across
G90
G0 Z{penUpZ} ; pen up
G91
G0 X-50 Y5 ; left edge, next line
G90
G1 Z{penDownZ} ; pen down
G91
G0 X0 Y0 ; draw from left edge
G1 X50 Y0 ; draw across
G90
G0 Z{penUpZ} ; pen up
G91
G0 X-50 Y5 ; left edge, next line
G90
G1 Z{penDownZ} ; pen down
G91
G0 X0 Y0 ; draw from left edge
G1 X50 Y0 ; draw across
G90
G0 Z{penUpZ} ; pen up
G91
G0 X-50 Y5 ; left edge, next line
G90
G1 Z{penDownZ} ; pen down
G91
G0 X0 Y0 ; draw from left edge
G1 X50 Y0 ; draw across
G90
G0 Z{penUpZ} ; pen up
G91
G0 X-50 Y5 ; left edge, next line
G90
G1 Z{penDownZ} ; pen down
G91
G0 X0 Y0 ; draw from left edge
G1 X50 Y0 ; draw across
G90
G0 Z{penUpZ} ; pen up
G91
G0 X-50 Y5 ; left edge, next line
G90
G1 Z{penDownZ} ; pen down
G91
G0 X0 Y0 ; draw from left edge
G1 X50 Y0 ; draw across
G90
G0 Z{penUpZ} ; pen up
G91
G0 X-50 Y5 ; left edge, next line
G90
G1 Z{penDownZ} ; pen down
G91
G0 X0 Y0 ; draw from left edge
G1 X50 Y0 ; draw across
G90
G0 Z{penUpZ} ; pen up
G91
G0 X-50 Y5 ; left edge, next line
G90
G1 Z{penDownZ} ; pen down
G91
G0 X0 Y0 ; draw from left edge
G1 X50 Y0 ; draw across
G90
G0 Z{penUpZ} ; pen up
G91
G0 X-50 Y5 ; left edge, next line
G90
G1 Z{penDownZ} ; pen down
G91
G0 X0 Y0 ; draw from left edge
G1 X50 Y0 ; draw across
G90
G0 Z{penUpZ} ; pen up
G91
G0 X-50 Y5 ; left edge, next line
G90
G1 Z{penDownZ} ; pen down
G91
G0 X0 Y0 ; draw from left edge
G1 X50 Y0 ; draw across
G90
G0 Z{penUpZ} ; pen up
G91
G0 X-50 Y5 ; left edge, next line
G90
G1 Z{penDownZ} ; pen down
G91
G0 X0 Y0 ; draw from left edge
G1 X50 Y0 ; draw across
G90
G0 Z{penUpZ} ; pen up
G91
G0 X-50 Y5 ; left edge, next line
G90
G1 Z{penDownZ} ; pen down
G91
G0 X0 Y0 ; draw from left edge
G1 X50 Y0 ; draw across
G90
G0 Z{penUpZ} ; pen up
G91
G0 X-50 Y5 ; left edge, next line
G90
G1 Z{penDownZ} ; pen down
G91
G0 X0 Y0 ; draw from left edge
G1 X50 Y0 ; draw across
G90
G0 Z{penUpZ} ; pen up
G91
G0 X-50 Y5 ; left edge, next line
G90
G1 Z{penDownZ} ; pen down
G91
G0 X0 Y0 ; draw from left edge
G1 X50 Y0 ; draw across
G90
G0 Z{penUpZ} ; pen up
G91
G0 X-50 Y5 ; left edge, next line
G90
G1 Z{penDownZ} ; pen down
G91
G0 X0 Y0 ; draw from left edge
G1 X50 Y0 ; draw across
G90
G0 Z{penUpZ} ; pen up
G91
G0 X-50 Y5 ; left edge, next line
G90
G1 Z{penDownZ} ; pen down
G91
G0 X0 Y0 ; draw from left edge
G1 X50 Y0 ; draw across
G90
G0 Z{penUpZ} ; pen up
G90 ; restore absolute
