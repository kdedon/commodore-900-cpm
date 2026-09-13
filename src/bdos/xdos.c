/*  How many consoles.  proc.h PNCON, which is now bconcnt() -- BIOS
    function 29, asked at the moment of the check.  It was the literal 1
    until C3 gave the BIOS SCC channel A and made it 2, and it is a
    constant no longer: C4 sizes the BIOS's console table from the serial
    map the loader hands over, so the number is the machine's, not this
    file's.  Function 148 accepts exactly the consoles that exist on the
    machine it is running on, which is what a compiled-in number could
    only manage on one machine.  */
