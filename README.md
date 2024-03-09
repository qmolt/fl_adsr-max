
# About

Audio external in C for the Windows version of Max 8 (Max/MSP). 
*fl_adsr~* receives a specific duration in milliseconds and plays an already provided envelope scaled to the same duration. 
Last specified duration is saved and can be retriggered with a bang.
Envelope shape can be specified with line format: 
- first two breakpoints will be saved as a fixed attack curve
- last two will be saved as a fixed release curve.
- the curve inbetween is scaled so the total duration of the envelope matches the input duration triggered.
