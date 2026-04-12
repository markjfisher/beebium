; Minimal test: just print "HELLO" and "DONE".
; No SCSI, no ADFS, no OSWORD. Just OSWRCH.

oswrch = &FFE3
osnewl = &FFE7

ORG &1F00

.start
    LDX #0
.loop
    LDA message,X
    BEQ done
    JSR oswrch
    INX
    BNE loop
.done
    JSR osnewl
    RTS

.message
    EQUS "HELLO DONE"
    EQUB 0

.end

SAVE "TEST", start, end, start
