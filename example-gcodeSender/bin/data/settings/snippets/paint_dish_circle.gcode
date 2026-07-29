; Paint dish circle - radius 20 mm (zone inner area 40 x 40 mm, margins already subtracted)
; Detour arrival = dish centre. Re-apply template if zone size changes.
; {penUpZ}/{penDownZ} expand from pen settings at inject time.
G91 ; relative XY from dish centre
G0 X20 Y0 ; start on +X of circle
G90
G1 Z{penDownZ} ; pen down into paint
G91
G2 X-40 Y0 I-20 J0 ; half circle (CW)
G2 X40 Y0 I20 J0 ; other half (CW)
G90
G0 Z{penUpZ} ; pen up
G91
G0 X-20 Y0 ; return to dish centre
G90 ; restore absolute
