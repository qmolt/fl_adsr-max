
# About

Audio external in C for the Windows version of Max 8 (Max/MSP). *fl_adsr~* receives a specific duration in milliseconds and plays inmediatly an envelope for the same duration. Last specified duration is saved and can be retriggered with a bang. The attack time is fixed. Attack time and curve shape can be set with 'attack' message. The decay, sustain and release shape can be specified with line format and will be scaled to last ```(total duration - attack time)``` milliseconds.
