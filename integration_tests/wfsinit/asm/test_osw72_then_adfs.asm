; Test: OSWORD &72 (SCSI READ) then select ADFS, then OSWORD &72 again.
; Reproduces WFSINIT's sequence: PROCread (line 280), FNdrive (line 360),
; PROCto (line 460), PROCread (line 560).
;
; Prints status after each step. If any step hangs, the last printed
; message identifies which one.
;
; Assemble: beebasm -i test_osw72_then_adfs.asm -do test_osw72.ssd -boot TEST
; Run: Shift-Break boots automatically

osword = &FFF1
osbyte = &FFF4
oswrch = &FFE3
osnewl = &FFE7

ORG &1F00

.start
    ; Print "T1:" (test 1: SCSI READ sector 0, 512 bytes)
    JSR print_inline
    EQUS "T1:", 0

    ; Build OSWORD &72 control block for READ(6) sector 0, 2 sectors
    LDA #0:STA cb+0          ; result = 0
    LDA #<buf:STA cb+1       ; transfer address low
    LDA #>buf:STA cb+2
    LDA #0:STA cb+3:STA cb+4 ; transfer address high bytes
    LDA #&08:STA cb+5        ; CDB byte 0: READ(6)
    LDA #0:STA cb+6          ; CDB byte 1: LUN 0, addr high
    LDA #0:STA cb+7          ; CDB byte 2: addr mid
    LDA #0:STA cb+8          ; CDB byte 3: addr low = sector 0
    LDA #2:STA cb+9          ; CDB byte 4: count = 2 sectors
    LDA #0:STA cb+10         ; CDB byte 5: control
    LDA #<512:STA cb+11      ; transfer length low
    LDA #>512:STA cb+12
    LDA #0:STA cb+13:STA cb+14

    ; Execute OSWORD &72
    LDA #&72
    LDX #<cb
    LDY #>cb
    JSR osword

    ; Print result
    LDA cb+0
    JSR print_hex
    JSR osnewl

    ; Print "T2:" (test 2: MODE SENSE, 22 bytes)
    JSR print_inline
    EQUS "T2:", 0

    LDA #0:STA cb+0
    LDA #<buf:STA cb+1
    LDA #>buf:STA cb+2
    LDA #0:STA cb+3:STA cb+4
    LDA #&1A:STA cb+5        ; CDB byte 0: MODE SENSE(6)
    LDA #0:STA cb+6:STA cb+7:STA cb+8
    LDA #22:STA cb+9         ; allocation length
    LDA #0:STA cb+10
    LDA #22:STA cb+11        ; transfer length
    LDA #0:STA cb+12:STA cb+13:STA cb+14

    LDA #&72
    LDX #<cb
    LDY #>cb
    JSR osword

    LDA cb+0
    JSR print_hex
    JSR osnewl

    ; Print "T3:" (test 3: select ADFS via OSBYTE &8F)
    JSR print_inline
    EQUS "T3:", 0

    LDA #&8F
    LDX #&12
    LDY #8                   ; filing system 8 = ADFS
    JSR osbyte

    JSR print_inline
    EQUS "OK", 13, 0

    ; Print "T4:" (test 4: OSWORD &72 READ again after ADFS selected)
    JSR print_inline
    EQUS "T4:", 0

    LDA #0:STA cb+0
    LDA #<buf:STA cb+1
    LDA #>buf:STA cb+2
    LDA #0:STA cb+3:STA cb+4
    LDA #&08:STA cb+5
    LDA #0:STA cb+6:STA cb+7:STA cb+8
    LDA #2:STA cb+9
    LDA #0:STA cb+10
    LDA #<512:STA cb+11
    LDA #>512:STA cb+12
    LDA #0:STA cb+13:STA cb+14

    LDA #&72
    LDX #<cb
    LDY #>cb
    JSR osword

    LDA cb+0
    JSR print_hex
    JSR osnewl

    ; Print "DONE"
    JSR print_inline
    EQUS "DONE", 13, 0

    RTS

; Print inline string (null-terminated, follows JSR)
.print_inline
    PLA:STA &F6
    PLA:STA &F7
    LDY #0
.pi_loop
    INC &F6
    BNE pi_noinc
    INC &F7
.pi_noinc
    LDA (&F6),Y
    BEQ pi_done
    JSR oswrch
    JMP pi_loop
.pi_done
    LDA &F7:PHA
    LDA &F6:PHA
    RTS

; Print A as two hex digits
.print_hex
    PHA
    LSR A:LSR A:LSR A:LSR A
    JSR print_nybble
    PLA
    AND #&0F
.print_nybble
    CMP #10
    BCC ph_digit
    ADC #6
.ph_digit
    ADC #&30
    JMP oswrch

ALIGN &100
.cb SKIP 20
.buf SKIP 512

.end

SAVE "TEST", start, end, start
